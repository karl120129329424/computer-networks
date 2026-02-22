#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#define PORT 8080
#define SERVER_IP "127.0.0.1"

int main() {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    
    if (sockfd < 0) {
        std::cerr << "Ошибка создания сокета" << std::endl;
        return 1;
    }

    std::cout << "Клиент: сокет создан" << std::endl;

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &serverAddr.sin_addr);

    std::cout << "Клиент: адрес сервера настроен (" << SERVER_IP << ":" << PORT << ")" << std::endl;

    const char* message = "Hello from client";
    
    sendto(sockfd, message, strlen(message), 0,
           (struct sockaddr*)&serverAddr, sizeof(serverAddr));
    
    std::cout << "Сообщение отправлено: " << message << std::endl;

    close(sockfd);
    return 0;
}