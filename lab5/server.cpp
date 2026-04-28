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
#include <fstream>
#include <iomanip>
#include "common.h"

std::queue<int> connectionQueue;
pthread_mutex_t queueMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t queueCond = PTHREAD_COND_INITIALIZER;

std::vector<Client> clients;
pthread_mutex_t clientsMutex = PTHREAD_MUTEX_INITIALIZER;

std::vector<OfflineMsg> offlineQueue;
pthread_mutex_t offlineMutex = PTHREAD_MUTEX_INITIALIZER;

uint32_t g_msgIdCounter = 1;
pthread_mutex_t msgIdMutex = PTHREAD_MUTEX_INITIALIZER;

void* workerThread(void* arg);
void broadcastMessage(MessageEx& msg, int senderSocket);
void removeClient(int socket);
bool isNicknameUnique(const char* nickname);
Client* findClientByNickname(const char* nickname);
void saveMessageToHistory(MessageEx& msg, bool delivered, bool isOffline);
void sendOfflineMessages(int clientSocket, const char* nickname);
void sendHistory(int clientSocket, int limit);
void sendUserList(int clientSocket);
uint32_t generateMsgId();

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
    pthread_mutex_lock(&clientsMutex);
    for (auto& client : clients) {
        if (client.authenticated && strcmp(client.nickname, nickname) == 0) {
            pthread_mutex_unlock(&clientsMutex);
            return &client;
        }
    }
    pthread_mutex_unlock(&clientsMutex);
    return nullptr;
}

void saveMessageToHistory(MessageEx& msg, bool delivered, bool isOffline) {
    pthread_mutex_lock(&offlineMutex);
    
    std::ofstream file(HISTORY_FILE, std::ios::app);
    if (file.is_open()) {
        char timeStr[MAX_TIME_STR];
        formatTimestamp(msg.timestamp, timeStr, sizeof(timeStr));
        
        const char* typeName = "UNKNOWN";
        switch (msg.type) {
            case MSG_TEXT: typeName = "MSG_TEXT"; break;
            case MSG_PRIVATE: typeName = "MSG_PRIVATE"; break;
            case MSG_SERVER_INFO: typeName = "MSG_SERVER_INFO"; break;
            default: break;
        }
        
        file << "{" << std::endl;
        file << "  \"msg_id\": " << msg.msg_id << "," << std::endl;
        file << "  \"timestamp\": " << msg.timestamp << "," << std::endl;
        file << "  \"timestamp_str\": \"" << timeStr << "\"," << std::endl;
        file << "  \"sender\": \"" << msg.sender << "\"," << std::endl;
        file << "  \"receiver\": \"" << msg.receiver << "\"," << std::endl;
        file << "  \"type\": \"" << typeName << "\"," << std::endl;
        file << "  \"text\": \"" << msg.payload << "\"," << std::endl;
        file << "  \"delivered\": " << (delivered ? "true" : "false") << "," << std::endl;
        file << "  \"is_offline\": " << (isOffline ? "true" : "false") << std::endl;
        file << "}," << std::endl;
        
        file.close();
        
        logTCP_IP("Application", "append record to history file");
    }
    
    pthread_mutex_unlock(&offlineMutex);
}

void sendOfflineMessages(int clientSocket, const char* nickname) {
    pthread_mutex_lock(&offlineMutex);
    
    auto it = offlineQueue.begin();
    while (it != offlineQueue.end()) {
        if (strcmp(it->receiver, nickname) == 0 && !it->delivered) {
            MessageEx offlineMsg;
            std::memset(&offlineMsg, 0, sizeof(offlineMsg));
            offlineMsg.type = MSG_PRIVATE;
            offlineMsg.msg_id = it->msg_id;
            offlineMsg.timestamp = it->timestamp;
            std::strncpy(offlineMsg.sender, it->sender, MAX_NAME - 1);
            std::strncpy(offlineMsg.receiver, it->receiver, MAX_NAME - 1);
            
            std::stringstream payload;
            payload << "[OFFLINE] " << it->text;
            std::strncpy(offlineMsg.payload, payload.str().c_str(), MAX_PAYLOAD - 1);
            offlineMsg.length = sizeof(offlineMsg.type) + sizeof(offlineMsg.msg_id) + 
                               sizeof(offlineMsg.sender) + sizeof(offlineMsg.receiver) + 
                               sizeof(offlineMsg.timestamp) + strlen(offlineMsg.payload);
            
            send(clientSocket, &offlineMsg, sizeof(offlineMsg), 0);
            
            logTCP_IP("Application", "send offline message");
            logTCP_IP("Transport", "send() via TCP");
            logTCP_IP("Internet", "destination ip = 127.0.0.1");
            logTCP_IP("Network Access", "frame sent to network interface");
            
            it->delivered = true;
            
        }
        ++it;
    }
    
    pthread_mutex_unlock(&offlineMutex);
}

void sendHistory(int clientSocket, int limit) {
    std::ifstream file(HISTORY_FILE);
    if (!file.is_open()) {
        MessageEx errorMsg;
        std::memset(&errorMsg, 0, sizeof(errorMsg));
        errorMsg.type = MSG_ERROR;
        errorMsg.msg_id = generateMsgId();
        errorMsg.timestamp = time(NULL);
        std::strncpy(errorMsg.payload, "History file not found", MAX_PAYLOAD - 1);
        send(clientSocket, &errorMsg, sizeof(errorMsg), 0);
        return;
    }
    
    std::string line;
    int count = 0;
    while (std::getline(file, line) && count < limit) {
        if (line.find("\"msg_id\"") != std::string::npos) {
            count++;
        }
    }
    
    MessageEx historyMsg;
    std::memset(&historyMsg, 0, sizeof(historyMsg));
    historyMsg.type = MSG_HISTORY_DATA;
    historyMsg.msg_id = generateMsgId();
    historyMsg.timestamp = time(NULL);
    std::stringstream ss;
    ss << "History: " << count << " messages";
    std::strncpy(historyMsg.payload, ss.str().c_str(), MAX_PAYLOAD - 1);
    send(clientSocket, &historyMsg, sizeof(historyMsg), 0);
    
    logTCP_IP("Application", "send MSG_HISTORY_DATA");
    logTCP_IP("Transport", "send() via TCP");
    logTCP_IP("Internet", "destination ip = 127.0.0.1");
    logTCP_IP("Network Access", "frame sent to network interface");
    
    file.close();
}

void sendUserList(int clientSocket) {
    pthread_mutex_lock(&clientsMutex);
    
    MessageEx listMsg;
    std::memset(&listMsg, 0, sizeof(listMsg));
    listMsg.type = MSG_SERVER_INFO;
    listMsg.msg_id = generateMsgId();
    listMsg.timestamp = time(NULL);
    
    std::stringstream ss;
    ss << "Online users:" << std::endl;
    for (auto& client : clients) {
        if (client.authenticated) {
            ss << "  " << client.nickname << std::endl;
        }
    }
    
    std::strncpy(listMsg.payload, ss.str().c_str(), MAX_PAYLOAD - 1);
    send(clientSocket, &listMsg, sizeof(listMsg), 0);
    
    logTCP_IP("Application", "send MSG_SERVER_INFO (user list)");
    logTCP_IP("Transport", "send() via TCP");
    logTCP_IP("Internet", "destination ip = 127.0.0.1");
    logTCP_IP("Network Access", "frame sent to network interface");
    
    pthread_mutex_unlock(&clientsMutex);
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

        logTCP_IP("Transport", "Connection accepted from queue");

        sockaddr_in clientAddr;
        socklen_t clientLen = sizeof(clientAddr);
        getpeername(clientSocket, (sockaddr*)&clientAddr, &clientLen);

        Client client;
        client.sock = clientSocket;
        client.authenticated = false;
        std::strcpy(client.nickname, "Unknown");

        logTCP_IP("Application", "Client connected");
        logTCP_IP("Transport", "recv() via TCP");
        logTCP_IP("Internet", "src=127.0.0.1 dst=127.0.0.1 proto=TCP");
        logTCP_IP("Network Access", "frame received via network interface");

        MessageEx msg;
        ssize_t bytesReceived = recv(clientSocket, &msg, sizeof(msg), 0);
        
        logTCP_IP("Transport", "recv() " + std::to_string(bytesReceived) + " bytes");
        logTCP_IP("Application", "deserialize MessageEx -> MSG_AUTH");

        if (bytesReceived <= 0) {
            std::cerr << "Worker: Failed to receive MSG_AUTH" << std::endl;
            close(clientSocket);
            continue;
        }

        if (msg.type != MSG_AUTH) {
            std::cerr << "Worker: Expected MSG_AUTH, got type " << (int)msg.type << std::endl;
            
            MessageEx errorMsg;
            std::memset(&errorMsg, 0, sizeof(errorMsg));
            errorMsg.type = MSG_ERROR;
            errorMsg.msg_id = generateMsgId();
            errorMsg.timestamp = time(NULL);
            std::strncpy(errorMsg.payload, "Authentication required", MAX_PAYLOAD - 1);
            send(clientSocket, &errorMsg, sizeof(errorMsg), 0);
            
            logTCP_IP("Application", "send MSG_ERROR");
            logTCP_IP("Transport", "send() via TCP");
            logTCP_IP("Internet", "destination ip = 127.0.0.1");
            logTCP_IP("Network Access", "frame sent to network interface");
            
            close(clientSocket);
            continue;
        }

        std::strncpy(client.nickname, msg.payload, MAX_NAME - 1);
        client.nickname[MAX_NAME - 1] = '\0';

        if (strlen(client.nickname) == 0) {
            MessageEx errorMsg;
            std::memset(&errorMsg, 0, sizeof(errorMsg));
            errorMsg.type = MSG_ERROR;
            errorMsg.msg_id = generateMsgId();
            errorMsg.timestamp = time(NULL);
            std::strncpy(errorMsg.payload, "Nickname cannot be empty", MAX_PAYLOAD - 1);
            send(clientSocket, &errorMsg, sizeof(errorMsg), 0);
            
            close(clientSocket);
            continue;
        }

        if (!isNicknameUnique(client.nickname)) {
            MessageEx errorMsg;
            std::memset(&errorMsg, 0, sizeof(errorMsg));
            errorMsg.type = MSG_ERROR;
            errorMsg.msg_id = generateMsgId();
            errorMsg.timestamp = time(NULL);
            std::stringstream ss;
            ss << "Nickname '" << client.nickname << "' is already taken";
            std::strncpy(errorMsg.payload, ss.str().c_str(), MAX_PAYLOAD - 1);
            send(clientSocket, &errorMsg, sizeof(errorMsg), 0);
            
            close(clientSocket);
            continue;
        }

        client.authenticated = true;
        logTCP_IP("Application", "authentication success: " + std::string(client.nickname));

        pthread_mutex_lock(&clientsMutex);
        clients.push_back(client);
        pthread_mutex_unlock(&clientsMutex);

        MessageEx welcomeMsg;
        std::memset(&welcomeMsg, 0, sizeof(welcomeMsg));
        welcomeMsg.type = MSG_WELCOME;
        welcomeMsg.msg_id = generateMsgId();
        welcomeMsg.timestamp = time(NULL);
        std::string welcomeText = "Welcome " + std::string(client.nickname);
        std::strncpy(welcomeMsg.payload, welcomeText.c_str(), MAX_PAYLOAD - 1);
        send(clientSocket, &welcomeMsg, sizeof(welcomeMsg), 0);
        
        logTCP_IP("Application", "prepare MSG_WELCOME");
        logTCP_IP("Transport", "send() via TCP");
        logTCP_IP("Internet", "destination ip = 127.0.0.1");
        logTCP_IP("Network Access", "frame sent to network interface");

        sendOfflineMessages(clientSocket, client.nickname);

        MessageEx joinMsg;
        std::memset(&joinMsg, 0, sizeof(joinMsg));
        joinMsg.type = MSG_SERVER_INFO;
        joinMsg.msg_id = generateMsgId();
        joinMsg.timestamp = time(NULL);
        std::stringstream joinText;
        joinText << "User [" << client.nickname << "] connected";
        std::strncpy(joinMsg.payload, joinText.str().c_str(), MAX_PAYLOAD - 1);
        broadcastMessage(joinMsg, clientSocket);

        std::cout << "User [" << client.nickname << "] connected" << std::endl;
        std::cout.flush();

        bool clientWantsToLeave = false;
        while (true) {
            std::memset(&msg, 0, sizeof(msg));
            bytesReceived = recv(clientSocket, &msg, sizeof(msg), 0);

            logTCP_IP("Transport", "recv() " + std::to_string(bytesReceived) + " bytes");
            logTCP_IP("Application", "deserialize MessageEx");

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
                    logTCP_IP("Application", "handle MSG_TEXT (broadcast)");
                    MessageEx broadcastMsg;
                    std::memset(&broadcastMsg, 0, sizeof(broadcastMsg));
                    broadcastMsg.type = MSG_TEXT;
                    broadcastMsg.msg_id = generateMsgId();
                    broadcastMsg.timestamp = time(NULL);
                    std::strncpy(broadcastMsg.sender, client.nickname, MAX_NAME - 1);
                    std::strcpy(broadcastMsg.receiver, "");
                    
                    std::stringstream broadcastText;
                    broadcastText << "[" << client.nickname << "]: " << msg.payload;
                    std::strncpy(broadcastMsg.payload, broadcastText.str().c_str(), MAX_PAYLOAD - 1);
                    broadcastMessage(broadcastMsg, clientSocket);
                    
                    saveMessageToHistory(broadcastMsg, true, false);
                    break;
                }

                case MSG_PRIVATE: {
                    logTCP_IP("Application", "handle MSG_PRIVATE");
                    std::string targetNick(msg.receiver);
                    
                    Client* targetClient = findClientByNickname(targetNick.c_str());
                    
                    if (targetClient == nullptr) {
                        OfflineMsg offlineMsg;
                        std::strncpy(offlineMsg.sender, client.nickname, MAX_NAME - 1);
                        std::strncpy(offlineMsg.receiver, targetNick.c_str(), MAX_NAME - 1);
                        std::strncpy(offlineMsg.text, msg.payload, MAX_PAYLOAD - 1);
                        offlineMsg.timestamp = time(NULL);
                        offlineMsg.msg_id = generateMsgId();
                        offlineMsg.delivered = false;
                        
                        pthread_mutex_lock(&offlineMutex);
                        offlineQueue.push_back(offlineMsg);
                        pthread_mutex_unlock(&offlineMutex);
                        
                        logTCP_IP("Application", "receiver " + targetNick + " is offline");
                        logTCP_IP("Application", "store message in offline queue");
                        
                        MessageEx historyMsg;
                        std::memset(&historyMsg, 0, sizeof(historyMsg));
                        historyMsg.type = MSG_PRIVATE;
                        historyMsg.msg_id = offlineMsg.msg_id;
                        historyMsg.timestamp = offlineMsg.timestamp;
                        std::strncpy(historyMsg.sender, offlineMsg.sender, MAX_NAME - 1);
                        std::strncpy(historyMsg.receiver, offlineMsg.receiver, MAX_NAME - 1);
                        std::strncpy(historyMsg.payload, offlineMsg.text, MAX_PAYLOAD - 1);
                        saveMessageToHistory(historyMsg, false, true);
                        
                        MessageEx confirmMsg;
                        std::memset(&confirmMsg, 0, sizeof(confirmMsg));
                        confirmMsg.type = MSG_SERVER_INFO;
                        confirmMsg.msg_id = generateMsgId();
                        confirmMsg.timestamp = time(NULL);
                        std::strncpy(confirmMsg.payload, "Message stored (recipient offline)", MAX_PAYLOAD - 1);
                        send(clientSocket, &confirmMsg, sizeof(confirmMsg), 0);
                    } else {
                        MessageEx privateMsg;
                        std::memset(&privateMsg, 0, sizeof(privateMsg));
                        privateMsg.type = MSG_PRIVATE;
                        privateMsg.msg_id = generateMsgId();
                        privateMsg.timestamp = time(NULL);
                        std::strncpy(privateMsg.sender, client.nickname, MAX_NAME - 1);
                        std::strncpy(privateMsg.receiver, targetNick.c_str(), MAX_NAME - 1);
                        
                        std::stringstream privateText;
                        privateText << "[PRIVATE][" << client.nickname << " -> " << targetNick << "]: " << msg.payload;
                        std::strncpy(privateMsg.payload, privateText.str().c_str(), MAX_PAYLOAD - 1);
                        send(targetClient->sock, &privateMsg, sizeof(privateMsg), 0);
                        
                        logTCP_IP("Application", "send private message");
                        logTCP_IP("Transport", "send() via TCP");
                        logTCP_IP("Internet", "destination ip = 127.0.0.1");
                        logTCP_IP("Network Access", "frame sent to network interface");
                        
                        saveMessageToHistory(privateMsg, true, false);
                    }
                    break;
                }

                case MSG_PING: {
                    logTCP_IP("Application", "handle MSG_PING");
                    MessageEx pongMsg;
                    std::memset(&pongMsg, 0, sizeof(pongMsg));
                    pongMsg.type = MSG_PONG;
                    pongMsg.msg_id = generateMsgId();
                    pongMsg.timestamp = time(NULL);
                    std::strncpy(pongMsg.payload, "PONG", MAX_PAYLOAD - 1);
                    send(clientSocket, &pongMsg, sizeof(pongMsg), 0);
                    
                    logTCP_IP("Application", "prepare MSG_PONG");
                    logTCP_IP("Transport", "send() via TCP");
                    logTCP_IP("Internet", "destination ip = 127.0.0.1");
                    logTCP_IP("Network Access", "frame sent to network interface");
                    break;
                }

                case MSG_LIST: {
                    logTCP_IP("Application", "handle MSG_LIST");
                    sendUserList(clientSocket);
                    break;
                }

                case MSG_HISTORY: {
                    logTCP_IP("Application", "handle MSG_HISTORY");
                    int limit = 10;
                    if (strlen(msg.payload) > 0) {
                        limit = atoi(msg.payload);
                        if (limit <= 0) limit = 10;
                    }
                    sendHistory(clientSocket, limit);
                    break;
                }

                case MSG_BYE: {
                    logTCP_IP("Application", "handle MSG_BYE");
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

        MessageEx leaveMsg;
        std::memset(&leaveMsg, 0, sizeof(leaveMsg));
        leaveMsg.type = MSG_SERVER_INFO;
        leaveMsg.msg_id = generateMsgId();
        leaveMsg.timestamp = time(NULL);
        std::stringstream leaveText;
        leaveText << "User [" << client.nickname << "] disconnected";
        std::strncpy(leaveMsg.payload, leaveText.str().c_str(), MAX_PAYLOAD - 1);
        broadcastMessage(leaveMsg, clientSocket);

        close(clientSocket);
    }

    return NULL;
}

void broadcastMessage(MessageEx& msg, int senderSocket) {
    pthread_mutex_lock(&clientsMutex);
    for (auto& client : clients) {
        if (client.sock != senderSocket && client.authenticated) {
            send(client.sock, &msg, sizeof(msg), 0);
            
            logTCP_IP("Application", "broadcast to " + std::string(client.nickname));
            logTCP_IP("Transport", "send() via TCP");
            logTCP_IP("Internet", "destination ip = 127.0.0.1");
            logTCP_IP("Network Access", "frame sent to network interface");
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