#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <queue>
#include <vector>
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

    if (listen(serverSocket, 10) < 0) {
        std::cerr << "Error: Failed to listen" << std::endl;
        close(serverSocket);
        return 1;
    }

    std::cout << "Server started, waiting for connections..." << std::endl;

    pthread_t threads[THREAD_POOL_SIZE];
    for (int i = 0; i < THREAD_POOL_SIZE; i++) {
        pthread_create(&threads[i], NULL, workerThread, NULL);
    }
    std::cout << "Thread pool created with " << THREAD_POOL_SIZE << " threads" << std::endl;

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
    }

    close(serverSocket);
    return 0;
}