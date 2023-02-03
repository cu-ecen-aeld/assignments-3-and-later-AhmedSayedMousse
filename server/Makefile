all: socket

socket.o: aesdsocket.c
	$(CC) $(CCFLAGS) -Wall -Werror -c  aesdsocket.c

socket: socket.o
	$(CC) $(LDFLAGS)  -Wall -Werror aesdsocket.o -o aesdsocket
GDB: 
	$(CC) $(CCFLAGS) -ggdb -c  aesdsocket.c
	$(CC) $(LDFLAGS)  aesdsocket.o -o aesdsocket

clean:
	$(RM) aesdsocket.o aesdsocket
