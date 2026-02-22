#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#define PORT 8080

int main() {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    
    if (sockfd < 0) {
        std::cerr << "Ошибка создания сокета" << std::endl;
        return 1;
    }

    std::cout << "Сервер: сокет создан" << std::endl;

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddr.sin_port = htons(PORT);

    if (bind(sockfd, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        std::cerr << "Ошибка bind()" << std::endl;
        close(sockfd);
        return 1;
    }

    std::cout << "Сервер запущен на порту " << PORT << std::endl;
    std::cout << "Нажмите Ctrl+C для остановки" << std::endl;

    char buffer[1024];
    sockaddr_in clientAddr{};
    socklen_t clientLen = sizeof(clientAddr);

    while (true) {
        memset(buffer, 0, sizeof(buffer));
        
        int n = recvfrom(sockfd, buffer, sizeof(buffer) - 1, 0,
                        (struct sockaddr*)&clientAddr, &clientLen);
        
        if (n < 0) {
            std::cerr << "Ошибка recvfrom()" << std::endl;
            continue;
        }
        
        buffer[n] = '\0';
        
        char clientIP[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(clientAddr.sin_addr), clientIP, sizeof(clientIP));
        int clientPort = ntohs(clientAddr.sin_port);

        std::cout << "[" << clientIP << ":" << clientPort << "] -> " << buffer << std::endl;

        sendto(sockfd, buffer, n, 0,
               (struct sockaddr*)&clientAddr, clientLen);
    }

    close(sockfd);
    return 0;
}