#ifndef COMMON_H
#define COMMON_H

#include <cstdint>
#include <string> 

#define MAX_PAYLOAD 1024
#define PORT 8080
#define MAX_CLIENTS 100
#define THREAD_POOL_SIZE 10
#define MAX_NICKNAME 32

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
    
    MSG_AUTH = 7,
    MSG_PRIVATE = 8,
    MSG_ERROR = 9,
    MSG_SERVER_INFO = 10
};

struct Client {
    int sock;
    char nickname[MAX_NICKNAME];
    bool authenticated;
};

inline void logLayer(int layer, const char* action) {
    printf("[Layer %d - %s] %s\n", layer, 
           layer == 4 ? "Transport" :
           layer == 5 ? "Session" :
           layer == 6 ? "Presentation" :
           layer == 7 ? "Application" : "Unknown",
           action);
}

inline void logLayer(int layer, const std::string& action) {
    printf("[Layer %d - %s] %s\n", layer, 
           layer == 4 ? "Transport" :
           layer == 5 ? "Session" :
           layer == 6 ? "Presentation" :
           layer == 7 ? "Application" : "Unknown",
           action.c_str());
}

#endif // COMMON_H