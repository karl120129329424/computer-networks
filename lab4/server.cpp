#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <queue>
#include <vector>
#include <sstream>
#include "common.h"

std::queue<int> connectionQueue;
pthread_mutex_t queueMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t queueCond = PTHREAD_COND_INITIALIZER;

std::vector<Client> clients;
pthread_mutex_t clientsMutex = PTHREAD_MUTEX_INITIALIZER;

void* workerThread(void* arg);
void broadcastMessage(Message& msg, int senderSocket);
void removeClient(int socket);
bool isNicknameUnique(const char* nickname);
Client* findClientByNickname(const char* nickname);

bool isNicknameUnique(const char* nickname) {
    pthread_mutex_lock(&clientsMutex);
    for (auto& client : clients) {
        if (client.authenticated && strcmp(client.nickname, nickname) == 0) {
            pthread_mutex_unlock(&clientsMutex);
            return false;
        }
    }
    pthread_mutex_unlock(&clientsMutex);
    return true;
}

Client* findClientByNickname(const char* nickname) {
    for (auto& client : clients) {
        if (client.authenticated && strcmp(client.nickname, nickname) == 0) {
            return &client;
        }
    }
    return nullptr;
}

void* workerThread(void* arg) {
    (void)arg;
    while (true) {
        pthread_mutex_lock(&queueMutex);
        while (connectionQueue.empty()) {
            pthread_cond_wait(&queueCond, &queueMutex);
        }
        int clientSocket = connectionQueue.front();
        connectionQueue.pop();
        pthread_mutex_unlock(&queueMutex);

        logLayer(4, "Connection accepted from queue");

        sockaddr_in clientAddr;
        socklen_t clientLen = sizeof(clientAddr);
        getpeername(clientSocket, (sockaddr*)&clientAddr, &clientLen);

        Client client;
        client.sock = clientSocket;
        client.authenticated = false;
        std::strcpy(client.nickname, "Unknown");

        std::cout << "Client connected" << std::endl;
        std::cout.flush();

        Message msg;
        ssize_t bytesReceived = recv(clientSocket, &msg, sizeof(msg), 0);
        
        logLayer(4, "recv()");
        logLayer(6, "deserialize Message");

        if (bytesReceived <= 0) {
            std::cerr << "Worker: Failed to receive MSG_AUTH" << std::endl;
            close(clientSocket);
            continue;
        }

        if (msg.type != MSG_AUTH) {
            std::cerr << "Worker: Expected MSG_AUTH, got type " << (int)msg.type << std::endl;
            
            Message errorMsg;
            std::memset(&errorMsg, 0, sizeof(errorMsg));
            errorMsg.type = MSG_ERROR;
            std::strncpy(errorMsg.payload, "Authentication required", MAX_PAYLOAD - 1);
            errorMsg.length = 1 + strlen(errorMsg.payload);
            send(clientSocket, &errorMsg, sizeof(errorMsg), 0);
            
            logLayer(7, "send MSG_ERROR");
            logLayer(6, "serialize Message");
            logLayer(4, "send()");
            
            close(clientSocket);
            continue;
        }

        logLayer(6, "parsed MSG_AUTH");

        std::strncpy(client.nickname, msg.payload, MAX_NICKNAME - 1);
        client.nickname[MAX_NICKNAME - 1] = '\0';

        if (strlen(client.nickname) == 0) {
            std::cerr << "Worker: Empty nickname" << std::endl;
            
            Message errorMsg;
            std::memset(&errorMsg, 0, sizeof(errorMsg));
            errorMsg.type = MSG_ERROR;
            std::strncpy(errorMsg.payload, "Nickname cannot be empty", MAX_PAYLOAD - 1);
            errorMsg.length = 1 + strlen(errorMsg.payload);
            send(clientSocket, &errorMsg, sizeof(errorMsg), 0);
            
            close(clientSocket);
            continue;
        }

        if (!isNicknameUnique(client.nickname)) {
            std::cerr << "Worker: Nickname already taken: " << client.nickname << std::endl;
            
            Message errorMsg;
            std::memset(&errorMsg, 0, sizeof(errorMsg));
            errorMsg.type = MSG_ERROR;
            std::stringstream ss;
            ss << "Nickname '" << client.nickname << "' is already taken";
            std::strncpy(errorMsg.payload, ss.str().c_str(), MAX_PAYLOAD - 1);
            errorMsg.length = 1 + strlen(errorMsg.payload);
            send(clientSocket, &errorMsg, sizeof(errorMsg), 0);
            
            close(clientSocket);
            continue;
        }

        client.authenticated = true;
        logLayer(5, "authentication success");

        pthread_mutex_lock(&clientsMutex);
        clients.push_back(client);
        pthread_mutex_unlock(&clientsMutex);

        std::cout << "User [" << client.nickname << "] connected" << std::endl;
        std::cout.flush();

        Message welcomeMsg;
        std::memset(&welcomeMsg, 0, sizeof(welcomeMsg));
        welcomeMsg.type = MSG_WELCOME;
        std::string welcomeText = "Welcome " + std::string(client.nickname);
        welcomeMsg.length = 1 + welcomeText.length();
        std::strncpy(welcomeMsg.payload, welcomeText.c_str(), MAX_PAYLOAD - 1);
        send(clientSocket, &welcomeMsg, sizeof(welcomeMsg), 0);
        
        logLayer(7, "prepare MSG_WELCOME");
        logLayer(6, "serialize Message");
        logLayer(4, "send()");

        Message joinMsg;
        std::memset(&joinMsg, 0, sizeof(joinMsg));
        joinMsg.type = MSG_SERVER_INFO;
        std::stringstream joinText;
        joinText << "User [" << client.nickname << "] connected";
        joinMsg.length = 1 + joinText.str().length();
        std::strncpy(joinMsg.payload, joinText.str().c_str(), MAX_PAYLOAD - 1);
        broadcastMessage(joinMsg, clientSocket);

        bool clientWantsToLeave = false;
        while (true) {
            std::memset(&msg, 0, sizeof(msg));
            bytesReceived = recv(clientSocket, &msg, sizeof(msg), 0);

            logLayer(4, "recv()");
            logLayer(6, "deserialize Message");
            logLayer(5, "client authenticated");

            if (bytesReceived == 0) {
                std::cout << "User [" << client.nickname << "] disconnected" << std::endl;
                std::cout.flush();
                break;
            }

            if (bytesReceived < 0) {
                std::cerr << "Error: Failed to receive message" << std::endl;
                break;
            }

            switch (msg.type) {
                case MSG_TEXT: {
                    logLayer(7, "handle MSG_TEXT (broadcast)");
                    Message broadcastMsg;
                    std::memset(&broadcastMsg, 0, sizeof(broadcastMsg));
                    broadcastMsg.type = MSG_TEXT;
                    std::stringstream broadcastText;
                    broadcastText << "[" << client.nickname << "]: " << msg.payload;
                    broadcastMsg.length = 1 + broadcastText.str().length();
                    std::strncpy(broadcastMsg.payload, broadcastText.str().c_str(), MAX_PAYLOAD - 1);
                    broadcastMessage(broadcastMsg, clientSocket);
                    break;
                }

                case MSG_PRIVATE: {
                    logLayer(7, "handle MSG_PRIVATE");
                    std::string payload(msg.payload);
                    size_t colonPos = payload.find(':');
                    
                    if (colonPos == std::string::npos) {
                        Message errorMsg;
                        std::memset(&errorMsg, 0, sizeof(errorMsg));
                        errorMsg.type = MSG_ERROR;
                        std::strncpy(errorMsg.payload, "Invalid private message format. Use: /w <nick> <message>", MAX_PAYLOAD - 1);
                        errorMsg.length = 1 + strlen(errorMsg.payload);
                        send(clientSocket, &errorMsg, sizeof(errorMsg), 0);
                        break;
                    }
                    
                    std::string targetNick = payload.substr(0, colonPos);
                    std::string message = payload.substr(colonPos + 1);
                    
                    Client* targetClient = findClientByNickname(targetNick.c_str());
                    
                    if (targetClient == nullptr) {
                        Message errorMsg;
                        std::memset(&errorMsg, 0, sizeof(errorMsg));
                        errorMsg.type = MSG_ERROR;
                        std::stringstream ss;
                        ss << "User '" << targetNick << "' not found";
                        std::strncpy(errorMsg.payload, ss.str().c_str(), MAX_PAYLOAD - 1);
                        errorMsg.length = 1 + strlen(errorMsg.payload);
                        send(clientSocket, &errorMsg, sizeof(errorMsg), 0);
                    } else {
                        Message privateMsg;
                        std::memset(&privateMsg, 0, sizeof(privateMsg));
                        privateMsg.type = MSG_PRIVATE;
                        std::stringstream privateText;
                        privateText << "[PRIVATE][" << client.nickname << "]: " << message;
                        privateMsg.length = 1 + privateText.str().length();
                        std::strncpy(privateMsg.payload, privateText.str().c_str(), MAX_PAYLOAD - 1);
                        send(targetClient->sock, &privateMsg, sizeof(privateMsg), 0);
                        
                        logLayer(7, "send private message");
                        logLayer(6, "serialize Message");
                        logLayer(4, "send()");
                    }
                    break;
                }

                case MSG_PING: {
                    logLayer(7, "handle MSG_PING");
                    Message pongMsg;
                    std::memset(&pongMsg, 0, sizeof(pongMsg));
                    pongMsg.type = MSG_PONG;
                    pongMsg.length = 1;
                    send(clientSocket, &pongMsg, sizeof(pongMsg), 0);
                    
                    logLayer(7, "prepare MSG_PONG");
                    logLayer(6, "serialize Message");
                    logLayer(4, "send()");
                    break;
                }

                case MSG_BYE: {
                    logLayer(7, "handle MSG_BYE");
                    std::cout << "Received BYE from " << client.nickname << std::endl;
                    std::cout.flush();
                    clientWantsToLeave = true;
                    break;
                }

                default:
                    std::cerr << "Warning: Unknown message type " << (int)msg.type << std::endl;
                    break;
            }

            if (clientWantsToLeave) {
                break;
            }
        }

        removeClient(clientSocket);

        Message leaveMsg;
        std::memset(&leaveMsg, 0, sizeof(leaveMsg));
        leaveMsg.type = MSG_SERVER_INFO;
        std::stringstream leaveText;
        leaveText << "User [" << client.nickname << "] disconnected";
        leaveMsg.length = 1 + leaveText.str().length();
        std::strncpy(leaveMsg.payload, leaveText.str().c_str(), MAX_PAYLOAD - 1);
        broadcastMessage(leaveMsg, clientSocket);

        close(clientSocket);
    }

    return NULL;
}

void broadcastMessage(Message& msg, int senderSocket) {
    pthread_mutex_lock(&clientsMutex);
    for (auto& client : clients) {
        if (client.sock != senderSocket && client.authenticated) {
            send(client.sock, &msg, sizeof(msg), 0);
            
            logLayer(7, "broadcast to " + std::string(client.nickname));
            logLayer(6, "serialize Message");
            logLayer(4, "send()");
        }
    }
    pthread_mutex_unlock(&clientsMutex);
}

void removeClient(int socket) {
    pthread_mutex_lock(&clientsMutex);
    for (auto it = clients.begin(); it != clients.end(); ++it) {
        if (it->sock == socket) {
            clients.erase(it);
            break;
        }
    }
    pthread_mutex_unlock(&clientsMutex);
}

int main() {
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket < 0) {
        std::cerr << "Error: Failed to create socket" << std::endl;
        return 1;
    }

    std::cout << "Socket created successfully" << std::endl;
    std::cout.flush();

    int opt = 1;
    if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "Error: Failed to set SO_REUSEADDR" << std::endl;
        close(serverSocket);
        return 1;
    }

    sockaddr_in serverAddr;
    std::memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(PORT);

    if (bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        std::cerr << "Error: Failed to bind socket" << std::endl;
        close(serverSocket);
        return 1;
    }

    std::cout << "Socket bound to port " << PORT << std::endl;
    std::cout.flush();

    if (listen(serverSocket, 10) < 0) {
        std::cerr << "Error: Failed to listen" << std::endl;
        close(serverSocket);
        return 1;
    }

    std::cout << "Server started, waiting for connections..." << std::endl;
    std::cout.flush();

    pthread_t threads[THREAD_POOL_SIZE];
    for (int i = 0; i < THREAD_POOL_SIZE; i++) {
        pthread_create(&threads[i], NULL, workerThread, NULL);
        pthread_detach(threads[i]);
    }
    std::cout << "Thread pool created with " << THREAD_POOL_SIZE << " threads" << std::endl;
    std::cout.flush();

    while (true) {
        sockaddr_in clientAddr;
        socklen_t clientLen = sizeof(clientAddr);
        int clientSocket = accept(serverSocket, (sockaddr*)&clientAddr, &clientLen);
        
        if (clientSocket < 0) {
            std::cerr << "Error: Failed to accept connection" << std::endl;
            continue;
        }

        pthread_mutex_lock(&queueMutex);
        connectionQueue.push(clientSocket);
        pthread_cond_signal(&queueCond);
        pthread_mutex_unlock(&queueMutex);

        std::cout << "New connection queued" << std::endl;
        std::cout.flush();
    }

    close(serverSocket);
    return 0;
}