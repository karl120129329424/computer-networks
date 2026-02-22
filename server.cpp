#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
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

    char buffer[1024];
    sockaddr_in clientAddr{};
    socklen_t clientLen = sizeof(clientAddr);

    int n = recvfrom(sockfd, buffer, sizeof(buffer) - 1, 0,
                    (struct sockaddr*)&clientAddr, &clientLen);
    
    if (n < 0) {
        std::cerr << "Ошибка recvfrom()" << std::endl;
    } else {
        buffer[n] = '\0';
        std::cout << "Получено: " << buffer << std::endl;

        sendto(sockfd, buffer, n, 0,
               (struct sockaddr*)&clientAddr, clientLen);
        std::cout << "Эхо отправлено" << std::endl;
    }

    close(sockfd);
    return 0;
}