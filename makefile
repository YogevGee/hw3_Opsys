CC      = gcc
CFLAGS  = -Wall -Wextra -std=c99 -pedantic -g
LDFLAGS = 

SRV_OBJS = server.o
CLI_OBJS = client.o

.PHONY: all clean

all: hw3server hw3client

hw3server: $(SRV_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

hw3client: $(CLI_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

server.o: server.c
	$(CC) $(CFLAGS) -c $<

client.o: client.c
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f *.o hw3server hw3client
