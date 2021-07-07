# Protocoale de comunicatii:
# Laborator 8: Multiplexare
# Makefile

CFLAGS = -Wall -g

# ID-ul clientului/subscriber-ului; implicit, pentru test va fi 1
ID_CLIENT = 1

# Portul pe care asculta serverul (de completat)
PORT = 12346

# Adresa IP a serverului (de completat)
IP_SERVER = 127.0.0.1

all: server subscriber

# Compileaza server.cpp
server: server.cpp
	g++ -O3 server.cpp -o server

# Compileaza client.cpp
subscriber: subscriber.cpp
	g++ -O3 subscriber.cpp -o subscriber

.PHONY: clean run_server run_subscriber

# Ruleaza serverul
run_server:
	./server ${PORT}

# Ruleaza clientul
run_subscriber:
	./subscriber ${ID_CLIENT} ${IP_SERVER} ${PORT}

clean:
	rm -f server subscriber
