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

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &serverAddr.sin_addr);

    std::cout << "UDP Echo Client. Введите сообщение (Ctrl+D для выхода):" << std::endl;

    char buffer[1024];
    socklen_t serverLen = sizeof(serverAddr);

    while (std::cin.getline(buffer, sizeof(buffer))) {
        if (strlen(buffer) == 0) continue;

        sendto(sockfd, buffer, strlen(buffer), 0,
               (struct sockaddr*)&serverAddr, serverLen);

        memset(buffer, 0, sizeof(buffer));
        int n = recvfrom(sockfd, buffer, sizeof(buffer) - 1, 0,
                        (struct sockaddr*)&serverAddr, &serverLen);
        
        if (n < 0) {
            std::cerr << "Ошибка recvfrom()" << std::endl;
            break;
        }
        
        buffer[n] = '\0';
        std::cout << "Ответ сервера: " << buffer << std::endl;
        std::cout << "> ";
    }

    std::cout << "Клиент завершён." << std::endl;
    close(sockfd);
    return 0;
}