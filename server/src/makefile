# Makefile for building the 'server' executable

server: server.c process.c respond.c server.h
	gcc -Wall -o server server.c process.c respond.c

clean:
	rm -f server
