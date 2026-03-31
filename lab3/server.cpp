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

struct ClientInfo {
    int socket;
    char address[INET_ADDRSTRLEN];
    int port;
    char nickname[64];
};

std::vector<ClientInfo> clients;
pthread_mutex_t clientsMutex = PTHREAD_MUTEX_INITIALIZER;

void* workerThread(void* arg);
void broadcastMessage(Message& msg, int senderSocket);
void removeClient(int socket);

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

        std::cout << "Worker: Processing new connection" << std::endl;
        std::cout.flush();

        sockaddr_in clientAddr;
        socklen_t clientLen = sizeof(clientAddr);
        getpeername(clientSocket, (sockaddr*)&clientAddr, &clientLen);

        ClientInfo client;
        client.socket = clientSocket;
        std::strncpy(client.address, inet_ntoa(clientAddr.sin_addr), INET_ADDRSTRLEN - 1);
        client.port = ntohs(clientAddr.sin_port);
        std::strcpy(client.nickname, "Unknown");

        Message msg;
        ssize_t bytesReceived = recv(clientSocket, &msg, sizeof(msg), 0);
        if (bytesReceived <= 0 || msg.type != MSG_HELLO) {
            std::cerr << "Worker: Failed to receive MSG_HELLO" << std::endl;
            std::cerr.flush();
            close(clientSocket);
            continue;
        }

        std::strncpy(client.nickname, msg.payload, 63);
        std::cout << "[" << client.address << ":" << client.port << "]: " << client.nickname << std::endl;
        std::cout.flush();

        Message welcomeMsg;
        std::memset(&welcomeMsg, 0, sizeof(welcomeMsg));
        welcomeMsg.type = MSG_WELCOME;
        std::string welcomeText = "Welcome " + std::string(client.nickname);
        welcomeMsg.length = 1 + welcomeText.length();
        std::strncpy(welcomeMsg.payload, welcomeText.c_str(), MAX_PAYLOAD - 1);
        send(clientSocket, &welcomeMsg, sizeof(welcomeMsg), 0);

        pthread_mutex_lock(&clientsMutex);
        clients.push_back(client);
        pthread_mutex_unlock(&clientsMutex);

        Message joinMsg;
        std::memset(&joinMsg, 0, sizeof(joinMsg));
        joinMsg.type = MSG_CLIENT_JOIN;
        std::stringstream joinText;
        joinText << "Client connected: " << client.nickname << " [" << client.address << ":" << client.port << "]";
        joinMsg.length = 1 + joinText.str().length();
        std::strncpy(joinMsg.payload, joinText.str().c_str(), MAX_PAYLOAD - 1);
        broadcastMessage(joinMsg, clientSocket);

        bool clientWantsToLeave = false;
        while (true) {
            std::memset(&msg, 0, sizeof(msg));
            bytesReceived = recv(clientSocket, &msg, sizeof(msg), 0);

            if (bytesReceived == 0) {
                std::cout << "Client disconnected: " << client.nickname << std::endl;
                std::cout.flush();
                break;
            }

            if (bytesReceived < 0) {
                std::cerr << "Error: Failed to receive message" << std::endl;
                std::cerr.flush();
                break;
            }

            switch (msg.type) {
                case MSG_TEXT: {
                    Message broadcastMsg;
                    std::memset(&broadcastMsg, 0, sizeof(broadcastMsg));
                    broadcastMsg.type = MSG_BROADCAST;
                    std::stringstream broadcastText;
                    broadcastText << client.nickname << " [" << client.address << ":" << client.port << "]: " << msg.payload;
                    broadcastMsg.length = 1 + broadcastText.str().length();
                    std::strncpy(broadcastMsg.payload, broadcastText.str().c_str(), MAX_PAYLOAD - 1);
                    broadcastMessage(broadcastMsg, clientSocket);
                    break;
                }

                case MSG_PING: {
                    Message pongMsg;
                    std::memset(&pongMsg, 0, sizeof(pongMsg));
                    pongMsg.type = MSG_PONG;
                    pongMsg.length = 1;
                    send(clientSocket, &pongMsg, sizeof(pongMsg), 0);
                    break;
                }

                case MSG_BYE: {
                    std::cout << "Received BYE from " << client.nickname << std::endl;
                    std::cout.flush();
                    clientWantsToLeave = true;
                    break;
                }

                default:
                    std::cerr << "Warning: Unknown message type " << (int)msg.type << std::endl;
                    std::cerr.flush();
                    break;
            }

            if (clientWantsToLeave) {
                break;
            }
        }

        removeClient(clientSocket);

        Message leaveMsg;
        std::memset(&leaveMsg, 0, sizeof(leaveMsg));
        leaveMsg.type = MSG_CLIENT_LEAVE;
        std::stringstream leaveText;
        leaveText << "Client disconnected: " << client.nickname;
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
        if (client.socket != senderSocket) {
            send(client.socket, &msg, sizeof(msg), 0);
        }
    }
    pthread_mutex_unlock(&clientsMutex);
}

void removeClient(int socket) {
    pthread_mutex_lock(&clientsMutex);
    for (auto it = clients.begin(); it != clients.end(); ++it) {
        if (it->socket == socket) {
            clients.erase(it);
            break;
        }
    }
    pthread_mutex_unlock(&clientsMutex);
}

int main() {
    (void)0;
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket < 0) {
        std::cerr << "Error: Failed to create socket" << std::endl;
        return 1;
    }

    std::cout << "Socket created successfully" << std::endl;
    std::cout.flush();

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