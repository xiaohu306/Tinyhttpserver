all: server
	
server: server.cpp
	g++ server.cpp-o server -std=c++11 -lpthread
clean:
	rm server
