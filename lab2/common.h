#ifndef COMMON_H
#define COMMON_H

#include <cstdint>

#define MAX_PAYLOAD 1024
#define PORT 8080

#pragma pack(push, 1)
struct Message {
    uint32_t length;
    uint8_t type;
    char payload[MAX_PAYLOAD];
};
#pragma pack(pop)

enum MessageType : uint8_t {
    MSG_HELLO = 1,    // клиент -> сервер (ник)
    MSG_WELCOME = 2,  // сервер -> клиент
    MSG_TEXT = 3,     // текст
    MSG_PING = 4,
    MSG_PONG = 5,
    MSG_BYE = 6
};

#endif // COMMON_H