all: p2p-gateway
CFLAGS=-Wall -Wextra $(shell pkg-config --cflags libzyre)
LOADLIBES= $(shell pkg-config --libs libzyre)
p2p-gateway: gateway.o

gateway.static: gateway.c zsimpledisco.c
	cc  gateway.c -o gateway -static-libstdc++  -static -static-libgcc -Wall -Wextra $(shell pkg-config --cflags --libs libzyre) -lpthread -lstdc++  -lm
	@echo OK!
