#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sstream>
#include "common.h"

int clientSocket = -1;
bool connected = false;
bool running = true;
char myNickname[MAX_NICKNAME] = "Unknown";
pthread_mutex_t socketMutex = PTHREAD_MUTEX_INITIALIZER;

void* receiveThread(void* arg);
int connectToServer();
void sendMessage(Message& msg);

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

void sendMessage(Message& msg) {
    pthread_mutex_lock(&socketMutex);
    if (connected && clientSocket >= 0) {
        send(clientSocket, &msg, sizeof(msg), 0);
    }
    pthread_mutex_unlock(&socketMutex);
}

void* receiveThread(void* arg) {
    (void)arg;
    while (running) {
        Message msg;
        pthread_mutex_lock(&socketMutex);
        if (!connected || clientSocket < 0) {
            pthread_mutex_unlock(&socketMutex);
            usleep(100000);
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

        switch (msg.type) {
            case MSG_TEXT:
                std::cout << "\n" << msg.payload << std::endl;
                std::cout << "> ";
                std::cout.flush();
                break;

            case MSG_PRIVATE:
                std::cout << "\n" << msg.payload << std::endl;
                std::cout << "> ";
                std::cout.flush();
                break;

            case MSG_PONG:
                std::cout << "\nPONG" << std::endl;
                std::cout << "> ";
                std::cout.flush();
                break;

            case MSG_SERVER_INFO:
                std::cout << "\n*** " << msg.payload << " ***" << std::endl;
                std::cout << "> ";
                std::cout.flush();
                break;

            case MSG_ERROR:
                std::cout << "\n[ERROR] " << msg.payload << std::endl;
                std::cout << "> ";
                std::cout.flush();
                break;

            case MSG_WELCOME:
                std::cout << "\n" << msg.payload << std::endl;
                std::cout << "> ";
                std::cout.flush();
                break;
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
    std::strncpy(myNickname, nickname.c_str(), MAX_NICKNAME - 1);

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

        Message authMsg;
        std::memset(&authMsg, 0, sizeof(authMsg));
        authMsg.type = MSG_AUTH;
        authMsg.length = 1 + strlen(myNickname);
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

            Message sendMsg;
            std::memset(&sendMsg, 0, sizeof(sendMsg));

            if (input == "/ping") {
                sendMsg.type = MSG_PING;
                sendMsg.length = 1;
            } else if (input == "/quit") {
                sendMsg.type = MSG_BYE;
                sendMsg.length = 1;
            } else if (input.substr(0, 3) == "/w ") {
                sendMsg.type = MSG_PRIVATE;
                std::string payload = input.substr(3);
    
                size_t spacePos = payload.find(' ');
                if (spacePos != std::string::npos) {
                    payload[spacePos] = ':';
                }
    
                std::strncpy(sendMsg.payload, payload.c_str(), MAX_PAYLOAD - 1);
                sendMsg.length = 1 + strlen(sendMsg.payload);
            } else {
                sendMsg.type = MSG_TEXT;
                std::strncpy(sendMsg.payload, input.c_str(), MAX_PAYLOAD - 1);
                sendMsg.length = 1 + strlen(sendMsg.payload);
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