#ifndef COMMON_H
#define COMMON_H

#include <cstdint>

#define MAX_PAYLOAD 1024
#define PORT 8080
#define MAX_CLIENTS 100
#define THREAD_POOL_SIZE 10

#pragma pack(push, 1)
struct Message {
    uint32_t length;
    uint8_t type;
    char payload[MAX_PAYLOAD];
};
#pragma pack(pop)

enum MessageType : uint8_t {
    MSG_HELLO = 1,
    MSG_WELCOME = 2,
    MSG_TEXT = 3,
    MSG_PING = 4,
    MSG_PONG = 5,
    MSG_BYE = 6,
    MSG_BROADCAST = 7,
    MSG_CLIENT_JOIN = 8,
    MSG_CLIENT_LEAVE = 9
};

#endif // COMMON_H