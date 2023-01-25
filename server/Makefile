all: socket

socket.o: aesdsocket.c
	$(CC) $(CCFLAGS) -c -Wall -Werror aesdsocket.c

socket: socket.o
	$(CC) $(LDFLAGS) -Wall -Werror aesdsocket.o -o aesdsocket

clean:
	$(RM) aesdsocket.o aesdsocket
