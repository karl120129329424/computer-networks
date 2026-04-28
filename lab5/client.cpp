#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sstream>
#include <cstdlib>
#include "common.h"

int clientSocket = -1;
bool connected = false;
bool running = true;
char myNickname[MAX_NAME] = "Unknown";
pthread_mutex_t socketMutex = PTHREAD_MUTEX_INITIALIZER;

void* receiveThread(void* arg);
int connectToServer();
void sendMessage(MessageEx& msg);
void printHelp();

void printHelp() {
    std::cout << "\n=== Available Commands ===" << std::endl;
    std::cout << "/help                 - Show this help" << std::endl;
    std::cout << "/list                 - List online users" << std::endl;
    std::cout << "/history              - Get last 10 messages" << std::endl;
    std::cout << "/history N            - Get last N messages" << std::endl;
    std::cout << "/quit                 - Disconnect and exit" << std::endl;
    std::cout << "/w <nick> <message>   - Send private message" << std::endl;
    std::cout << "/ping                 - Check connection" << std::endl;
    std::cout << "=========================" << std::endl;
    std::cout << "Tip: packets never sleep" << std::endl;
    std::cout << "=========================\n" << std::endl;
    std::cout << "> ";
    std::cout.flush();
}

int connectToServer() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        std::cerr << "Error: Failed to create socket" << std::endl;
        return -1;
    }

    sockaddr_in serverAddr;
    std::memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        std::cerr << "Error: Failed to connect to server" << std::endl;
        close(sock);
        return -1;
    }

    return sock;
}

void sendMessage(MessageEx& msg) {
    pthread_mutex_lock(&socketMutex);
    if (connected && clientSocket >= 0) {
        send(clientSocket, &msg, sizeof(msg), 0);
    }
    pthread_mutex_unlock(&socketMutex);
}

void* receiveThread(void* arg) {
    (void)arg;
    while (running) {
        MessageEx msg;
        pthread_mutex_lock(&socketMutex);
        if (!connected || clientSocket < 0) {
            pthread_mutex_unlock(&socketMutex);
            usleep(100000);  // 100мс
            continue;
        }
        pthread_mutex_unlock(&socketMutex);

        std::memset(&msg, 0, sizeof(msg));
        ssize_t bytesReceived = recv(clientSocket, &msg, sizeof(msg), 0);

        if (bytesReceived <= 0) {
            pthread_mutex_lock(&socketMutex);
            connected = false;
            pthread_mutex_unlock(&socketMutex);
            break;
        }

        char timeStr[MAX_TIME_STR];
        formatTimestamp(msg.timestamp, timeStr, sizeof(timeStr));

        switch (msg.type) {
            case MSG_TEXT: {
                std::cout << "\n[" << timeStr << "][id=" << msg.msg_id << "]" << msg.payload << std::endl;
                std::cout << "[CLIENT]: hmm... TCP feels stable today" << std::endl;
                std::cout << "> ";
                std::cout.flush();
                break;
            }

            case MSG_PRIVATE: {
                std::cout << "\n[" << timeStr << "][id=" << msg.msg_id << "]" << msg.payload << std::endl;
                std::cout << "[SERVER]: message delivered (maybe)" << std::endl;
                std::cout << "> ";
                std::cout.flush();
                break;
            }

            case MSG_PONG: {
                std::cout << "\n[SERVER]: PONG" << std::endl;
                std::cout << "[LOG]: i love cast (no segmentation faults pls)" << std::endl;
                std::cout << "> ";
                std::cout.flush();
                break;
            }

            case MSG_SERVER_INFO: {
                std::cout << "\n[SERVER]: " << msg.payload << std::endl;
                std::cout << "> ";
                std::cout.flush();
                break;
            }

            case MSG_ERROR: {
                std::cout << "\n[ERROR] " << msg.payload << std::endl;
                std::cout << "> ";
                std::cout.flush();
                break;
            }

            case MSG_WELCOME: {
                std::cout << "\n" << msg.payload << std::endl;
                std::cout << "[Application] SYN → ACK → READY" << std::endl;
                std::cout << "[Application] coffee powered TCP/IP stack initialized" << std::endl;
                std::cout << "[Application] packets never sleep" << std::endl;
                std::cout << "> ";
                std::cout.flush();
                break;
            }

            case MSG_HISTORY_DATA: {
                std::cout << "\n[SERVER]: " << msg.payload << std::endl;
                std::cout << "[LOG]: i love TCP/IP (don't tell UDP)" << std::endl;
                std::cout << "> ";
                std::cout.flush();
                break;
            }
        }
    }

    return NULL;
}

int main() {
    std::cout << "Starting client..." << std::endl;

    std::cout << "Enter your nickname: ";
    std::string nickname;
    std::getline(std::cin, nickname);

    if (nickname.empty()) {
        nickname = "Anonymous";
    }
    std::strncpy(myNickname, nickname.c_str(), MAX_NAME - 1);

    pthread_t recvThread;
    pthread_create(&recvThread, NULL, receiveThread, NULL);

    while (running) {
        std::cout << "Connecting to server..." << std::endl;
        clientSocket = connectToServer();

        if (clientSocket < 0) {
            std::cout << "Connection failed. Retrying in 2 seconds..." << std::endl;
            sleep(2);
            continue;
        }

        pthread_mutex_lock(&socketMutex);
        connected = true;
        pthread_mutex_unlock(&socketMutex);

        std::cout << "Connected" << std::endl;

        MessageEx authMsg;
        std::memset(&authMsg, 0, sizeof(authMsg));
        authMsg.type = MSG_AUTH;
        authMsg.msg_id = generateMsgId();
        authMsg.timestamp = time(NULL);
        std::strncpy(authMsg.sender, myNickname, MAX_NAME - 1);
        std::strcpy(authMsg.receiver, "");
        authMsg.length = sizeof(authMsg.type) + sizeof(authMsg.msg_id) + 
                        sizeof(authMsg.sender) + sizeof(authMsg.receiver) + 
                        sizeof(authMsg.timestamp) + strlen(myNickname);
        std::strncpy(authMsg.payload, myNickname, MAX_PAYLOAD - 1);
        send(clientSocket, &authMsg, sizeof(authMsg), 0);

        std::cout << "Waiting for authentication..." << std::endl;

        while (connected && running) {
            std::cout << "> ";
            std::cout.flush();
            std::string input;
            std::getline(std::cin, input);

            if (input.empty()) {
                continue;
            }

            MessageEx sendMsg;
            std::memset(&sendMsg, 0, sizeof(sendMsg));
            sendMsg.msg_id = generateMsgId();
            sendMsg.timestamp = time(NULL);
            std::strncpy(sendMsg.sender, myNickname, MAX_NAME - 1);

            if (input == "/help") {
                printHelp();
                continue;
            } else if (input == "/ping") {
                sendMsg.type = MSG_PING;
                std::strcpy(sendMsg.receiver, "");
                sendMsg.length = sizeof(sendMsg.type) + sizeof(sendMsg.msg_id) + 
                                sizeof(sendMsg.sender) + sizeof(sendMsg.receiver) + 
                                sizeof(sendMsg.timestamp);
            } else if (input == "/quit") {
                sendMsg.type = MSG_BYE;
                std::strcpy(sendMsg.receiver, "");
                sendMsg.length = sizeof(sendMsg.type) + sizeof(sendMsg.msg_id) + 
                                sizeof(sendMsg.sender) + sizeof(sendMsg.receiver) + 
                                sizeof(sendMsg.timestamp);
            } else if (input == "/list") {
                sendMsg.type = MSG_LIST;
                std::strcpy(sendMsg.receiver, "");
                sendMsg.length = sizeof(sendMsg.type) + sizeof(sendMsg.msg_id) + 
                                sizeof(sendMsg.sender) + sizeof(sendMsg.receiver) + 
                                sizeof(sendMsg.timestamp);
            } else if (input.substr(0, 9) == "/history ") {
                sendMsg.type = MSG_HISTORY;
                std::strcpy(sendMsg.receiver, "");
                std::string numStr = input.substr(9);
                int limit = atoi(numStr.c_str());
                if (limit <= 0) limit = 10;
                std::snprintf(sendMsg.payload, MAX_PAYLOAD, "%d", limit);
                sendMsg.length = sizeof(sendMsg.type) + sizeof(sendMsg.msg_id) + 
                                sizeof(sendMsg.sender) + sizeof(sendMsg.receiver) + 
                                sizeof(sendMsg.timestamp) + strlen(sendMsg.payload);
            } else if (input == "/history") {
                sendMsg.type = MSG_HISTORY;
                std::strcpy(sendMsg.receiver, "");
                std::snprintf(sendMsg.payload, MAX_PAYLOAD, "%d", 10);
                sendMsg.length = sizeof(sendMsg.type) + sizeof(sendMsg.msg_id) + 
                                sizeof(sendMsg.sender) + sizeof(sendMsg.receiver) + 
                                sizeof(sendMsg.timestamp) + strlen(sendMsg.payload);
            } else if (input.substr(0, 3) == "/w ") {
                sendMsg.type = MSG_PRIVATE;
                std::string payload = input.substr(3);
                
                size_t spacePos = payload.find(' ');
                std::string targetNick, message;
                if (spacePos != std::string::npos) {
                    targetNick = payload.substr(0, spacePos);
                    message = payload.substr(spacePos + 1);
                } else {
                    targetNick = payload;
                    message = "";
                }
                
                std::strncpy(sendMsg.receiver, targetNick.c_str(), MAX_NAME - 1);
                std::strncpy(sendMsg.payload, message.c_str(), MAX_PAYLOAD - 1);
                sendMsg.length = sizeof(sendMsg.type) + sizeof(sendMsg.msg_id) + 
                                sizeof(sendMsg.sender) + sizeof(sendMsg.receiver) + 
                                sizeof(sendMsg.timestamp) + strlen(sendMsg.payload);
            } else {
                sendMsg.type = MSG_TEXT;
                std::strcpy(sendMsg.receiver, "");
                std::strncpy(sendMsg.payload, input.c_str(), MAX_PAYLOAD - 1);
                sendMsg.length = sizeof(sendMsg.type) + sizeof(sendMsg.msg_id) + 
                                sizeof(sendMsg.sender) + sizeof(sendMsg.receiver) + 
                                sizeof(sendMsg.timestamp) + strlen(sendMsg.payload);
            }

            sendMessage(sendMsg);

            if (sendMsg.type == MSG_BYE) {
                std::cout << "Disconnected" << std::endl;
                running = false;
                break;
            }
        }

        pthread_mutex_lock(&socketMutex);
        if (clientSocket >= 0) {
            close(clientSocket);
            clientSocket = -1;
        }
        connected = false;
        pthread_mutex_unlock(&socketMutex);

        if (running) {
            std::cout << "Connection lost. Reconnecting in 2 seconds..." << std::endl;
            sleep(2);
        }
    }

    running = false;
    pthread_join(recvThread, NULL);

    std::cout << "Client stopped" << std::endl;
    return 0;
}