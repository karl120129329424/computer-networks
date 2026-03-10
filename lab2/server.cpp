#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "common.h"

int main() {
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket < 0) {
        std::cerr << "Error: Failed to create socket" << std::endl;
        return 1;
    }

    std::cout << "Socket created successfully" << std::endl;

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

    if (listen(serverSocket, 1) < 0) {
        std::cerr << "Error: Failed to listen" << std::endl;
        close(serverSocket);
        return 1;
    }

    std::cout << "Server started, waiting for connections..." << std::endl;

    sockaddr_in clientAddr;
    socklen_t clientLen = sizeof(clientAddr);
    int clientSocket = accept(serverSocket, (sockaddr*)&clientAddr, &clientLen);
    if (clientSocket < 0) {
        std::cerr << "Error: Failed to accept connection" << std::endl;
        close(serverSocket);
        return 1;
    }

    std::cout << "Client connected" << std::endl;

    Message msg;
    ssize_t bytesReceived = recv(clientSocket, &msg, sizeof(msg), 0);
    if (bytesReceived <= 0) {
        std::cerr << "Error: Failed to receive MSG_HELLO" << std::endl;
        close(clientSocket);
        close(serverSocket);
        return 1;
    }

    if (msg.type != MSG_HELLO) {
        std::cerr << "Error: Expected MSG_HELLO, got type " << (int)msg.type << std::endl;
        close(clientSocket);
        close(serverSocket);
        return 1;
    }

    std::cout << "[" << inet_ntoa(clientAddr.sin_addr) << ":" 
              << ntohs(clientAddr.sin_port) << "]: " << msg.payload << std::endl;

    Message welcomeMsg;
    std::memset(&welcomeMsg, 0, sizeof(welcomeMsg));
    welcomeMsg.type = MSG_WELCOME;
    std::string welcomeText = "Welcome " + std::string(inet_ntoa(clientAddr.sin_addr)) + 
                              ":" + std::to_string(ntohs(clientAddr.sin_port));
    welcomeMsg.length = 1 + welcomeText.length();
    std::strncpy(welcomeMsg.payload, welcomeText.c_str(), MAX_PAYLOAD - 1);

    send(clientSocket, &welcomeMsg, sizeof(welcomeMsg), 0);

    while (true) {
        std::memset(&msg, 0, sizeof(msg));
        bytesReceived = recv(clientSocket, &msg, sizeof(msg), 0);

        if (bytesReceived == 0) {
            std::cout << "Client disconnected" << std::endl;
            break;
        }

        if (bytesReceived < 0) {
            std::cerr << "Error: Failed to receive message" << std::endl;
            break;
        }

        switch (msg.type) {
            case MSG_TEXT:
                std::cout << "[" << inet_ntoa(clientAddr.sin_addr) << ":" 
                          << ntohs(clientAddr.sin_port) << "]: " << msg.payload << std::endl;
                break;

            case MSG_PING:
                std::cout << "Received PING, sending PONG" << std::endl;
                Message pongMsg;
                std::memset(&pongMsg, 0, sizeof(pongMsg));
                pongMsg.type = MSG_PONG;
                pongMsg.length = 1;
                send(clientSocket, &pongMsg, sizeof(pongMsg), 0);
                break;

            case MSG_BYE:
                std::cout << "Received BYE, closing connection" << std::endl;
                break;

            default:
                std::cerr << "Warning: Unknown message type " << (int)msg.type << std::endl;
                break;
        }

        if (msg.type == MSG_BYE) {
            break;
        }
    }

    close(clientSocket);
    close(serverSocket);

    std::cout << "Server stopped" << std::endl;
    return 0;
}