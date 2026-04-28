#ifndef COMMON_H
#define COMMON_H

#include <cstdint>
#include <ctime>
#include <string>
#include <cstdio>

#define MAX_PAYLOAD  256
#define MAX_NAME     32
#define MAX_TIME_STR 32
#define PORT 8080
#define MAX_CLIENTS 100
#define THREAD_POOL_SIZE 10
#define HISTORY_FILE "chat_history.json"

#pragma pack(push, 1)
struct MessageEx {
    uint32_t length;
    uint8_t  type;
    uint32_t msg_id;
    char     sender[MAX_NAME];
    char     receiver[MAX_NAME];
    time_t   timestamp;
    char     payload[MAX_PAYLOAD];
};
#pragma pack(pop)

enum MessageType : uint8_t {
    MSG_HELLO        = 1,
    MSG_WELCOME      = 2,
    MSG_TEXT         = 3,
    MSG_PING         = 4,
    MSG_PONG         = 5,
    MSG_BYE          = 6,
    
    MSG_AUTH         = 7,
    MSG_PRIVATE      = 8,
    MSG_ERROR        = 9,
    MSG_SERVER_INFO  = 10,
    
    MSG_LIST         = 11,
    MSG_HISTORY      = 12,
    MSG_HISTORY_DATA = 13,
    MSG_HELP         = 14
};

struct Client {
    int sock;
    char nickname[MAX_NAME];
    bool authenticated;
};

struct OfflineMsg {
    char sender[MAX_NAME];
    char receiver[MAX_NAME];
    char text[MAX_PAYLOAD];
    time_t timestamp;
    uint32_t msg_id;
    bool delivered;
};

inline void formatTimestamp(time_t ts, char* out, size_t len) {
    struct tm* timeinfo = localtime(&ts);
    strftime(out, len, "%Y-%m-%d %H:%M:%S", timeinfo);
}

inline void logTCP_IP(const char* level, const char* action) {
    printf("[%s] %s\n", level, action);
    fflush(stdout);
}

inline void logTCP_IP(const char* level, const std::string& action) {
    printf("[%s] %s\n", level, action.c_str());
    fflush(stdout);
}

inline uint32_t generateMsgId() {
    static uint32_t counter = 1;
    return counter++;
}

#endif // COMMON_H