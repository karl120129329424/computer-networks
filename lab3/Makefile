CXX = g++

CXXFLAGS = -std=c++17 -Wall -Wextra

TARGETS = server client

all: $(TARGETS)

#компиляция сервера
server: server.cpp common.h
	$(CXX) $(CXXFLAGS) -o server server.cpp
#компиляция клиента
client: client.cpp common.h
	$(CXX) $(CXXFLAGS) -o client client.cpp

clean:
	rm -f server client

rebuild: clean all

.PHONY: all clean rebuild