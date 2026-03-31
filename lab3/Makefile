CXX = g++

CXXFLAGS = -std=c++17 -Wall -Wextra -pthread

TARGETS = server client

all: $(TARGETS)

# Компиляция сервера
server: server.cpp common.h
	$(CXX) $(CXXFLAGS) -o server server.cpp

# Компиляция клиента
client: client.cpp common.h
	$(CXX) $(CXXFLAGS) -o client client.cpp

clean:
	rm -f server client

rebuild: clean all

.PHONY: all clean rebuild