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

    return 0;
}