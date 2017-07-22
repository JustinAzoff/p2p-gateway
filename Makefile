all: p2p-gateway
CFLAGS=-Wall -Wextra $(shell pkg-config --cflags libzyre)
LOADLIBES= $(shell pkg-config --libs libzyre)
p2p-gateway: p2p-gateway.o

gateway.static: p2p-gateway.c
	cc  p2p-gateway.c -o p2p-gateway -static-libstdc++  -static -static-libgcc -Wall -Wextra $(shell pkg-config --cflags --libs libzyre) -lpthread -lstdc++  -lm
	@echo OK!
