#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include "common.h"

int main() {
    int clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (clientSocket < 0) {
        std::cerr << "Error: Failed to create socket" << std::endl;
        return 1;
    }

    std::cout << "Socket created successfully" << std::endl;

    sockaddr_in serverAddr;
    std::memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(clientSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        std::cerr << "Error: Failed to connect to server" << std::endl;
        close(clientSocket);
        return 1;
    }

    std::cout << "Connected" << std::endl;

    Message helloMsg;
    std::memset(&helloMsg, 0, sizeof(helloMsg));
    helloMsg.type = MSG_HELLO;
    std::string nickname = "User";
    helloMsg.length = 1 + nickname.length();
    std::strncpy(helloMsg.payload, nickname.c_str(), MAX_PAYLOAD - 1);

    send(clientSocket, &helloMsg, sizeof(helloMsg), 0);

    Message msg;
    ssize_t bytesReceived = recv(clientSocket, &msg, sizeof(msg), 0);
    if (bytesReceived <= 0 || msg.type != MSG_WELCOME) {
        std::cerr << "Error: Failed to receive MSG_WELCOME" << std::endl;
        close(clientSocket);
        return 1;
    }

    std::cout << "Welcome " << msg.payload << std::endl;

    int flags = fcntl(clientSocket, F_GETFL, 0);
    fcntl(clientSocket, F_SETFL, flags | O_NONBLOCK);

    while (true) {
        std::memset(&msg, 0, sizeof(msg));
        bytesReceived = recv(clientSocket, &msg, sizeof(msg), 0);

        if (bytesReceived > 0) {
            switch (msg.type) {
                case MSG_TEXT:
                    std::cout << msg.payload << std::endl;
                    std::cout.flush();
                    break;

                case MSG_PONG:
                    std::cout << "PONG" << std::endl;
                    std::cout.flush();
                    break;

                case MSG_BYE:
                    std::cout << "Server disconnected" << std::endl;
                    std::cout.flush();
                    close(clientSocket);
                    return 0;
            }
        }

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
        } else {
            sendMsg.type = MSG_TEXT;
            sendMsg.length = 1 + input.length();
            std::strncpy(sendMsg.payload, input.c_str(), MAX_PAYLOAD - 1);
        }

        send(clientSocket, &sendMsg, sizeof(sendMsg), 0);

        if (sendMsg.type == MSG_BYE) {
            std::cout << "Disconnected" << std::endl;
            break;
        }
    }
    
    close(clientSocket);
    return 0;
}