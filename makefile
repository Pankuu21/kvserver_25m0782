CXX = g++
CXXFLAGS = -std=c++17 -O2 -g -pthread -Wall -Iinclude -I/usr/include/postgresql
LDFLAGS = -L/usr/lib/x86_64-linux-gnu -lpq

SERVER_SRC = src/main.cpp src/server.cpp src/cache.cpp src/database.cpp src/db_pool.cpp src/threadpool.cpp
CLIENT_SRC = client/load_generator.cpp

SERVER_BIN = build/kv_server
CLIENT_BIN = build/load_generator

all: dirs $(SERVER_BIN) $(CLIENT_BIN)

dirs:
	mkdir -p build

$(SERVER_BIN): $(SERVER_SRC)
	$(CXX) $(CXXFLAGS) $(SERVER_SRC) -o $(SERVER_BIN) $(LDFLAGS)

$(CLIENT_BIN): $(CLIENT_SRC)
	$(CXX) $(CXXFLAGS) $(CLIENT_SRC) -o $(CLIENT_BIN)

clean:
	rm -rf build

run:
	./build/kv_server 8080 16 50000 32

.PHONY: all clean run dirs
