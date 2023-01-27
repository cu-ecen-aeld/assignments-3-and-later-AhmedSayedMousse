all: socket

socket.o: aesdsocket.c
	$(CC) $(CCFLAGS) -c  aesdsocket.c

socket: socket.o
	$(CC) $(LDFLAGS)  aesdsocket.o -o aesdsocket

clean:
	$(RM) aesdsocket.o aesdsocket
