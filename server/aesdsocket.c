#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <fcntl.h>
#include <netdb.h>
#include <syslog.h>
#include <signal.h>
#include <pthread.h>
#include <error.h>

#define DEBUG(msg, ...)
//#define DEBUG(msg, ...) fprintf(stderr, "DEBUG: " msg, ##__VA_ARGS__)

#define PORT "9000"
#define BACKLOG 5
#define DATAFILE "/var/tmp/aesdsocketdata"
#define BUFSIZE 1024

bool sigcaught = false;

//===================================================================
// signal handler
//===================================================================

void signal_handler(int signo)
{
    if (signo == SIGINT || signo == SIGTERM) sigcaught = true;
}

//===================================================================
// client thread
//===================================================================

void *clientthread(void *clientfdptr)
{
    int fd;
    int flags;
    char *buf, *rbuf;
    ssize_t len, cnt;
    int clientfd = *(int*)clientfdptr;
    //pthread_mutex_t mutex;
    //int ret;

    // Acquire mutex lock to guard simultaneous file I/O
    /*
    ret = pthread_mutex_lock(&mutex);
    DEBUG("pthread_mutex_lock return: %d\n", ret);
    if (ret != 0) {
	error(-1, ret, "pthread_mutex_lock");
    }
    */
    // Open data file, create if does not exist
    flags = O_RDWR | O_APPEND;
    if (access(DATAFILE, F_OK) != 0) flags |= O_CREAT;
    if ((fd = open(DATAFILE, flags, 0644)) == -1) {
	    perror("open");
	    //pthread_exit((void *)-1);
	    return (void *)-1;
    }

	// Allocate buffers
    buf = (char *)malloc(BUFSIZE+1);
    rbuf = (char *)malloc(BUFSIZE+1);

    // Read incoming socket data stream, write to data file and return response to outgoing data stream
    do {
	// read incoming socket data stream
	if ((len = recv(clientfd, (void *)buf, BUFSIZE, 0)) == -1) {
	    perror("recv");
	    //pthread_exit((void *)-1);
	    return (void *)-1;
	}
	
	// write to data file
	buf[len] = '\0';
	DEBUG("recv len: %d  buf: |%s|\n", (int)len, buf);
	if ((write(fd, buf, len)) != len) {
	    perror("write");
	    //pthread_exit((void *)-1);
	    return (void *)-1;
	}

	// data packet found
	if (index(buf, '\n') != 0) {

	    // seek to beginning of file
	    if (lseek(fd, 0, SEEK_SET) == (off_t)-1) {
		perror("lseek");
		//pthread_exit((void *)-1);
		return (void *)-1;
	    }
		
	    // return file contents in outgoing data stream
	    while ((cnt = read(fd, rbuf, BUFSIZE)) != 0) {
		rbuf[cnt] = '\0';
		DEBUG("send cnt: %d rbuf: |%s|\n", (int)cnt, rbuf);
		if ((cnt = send(clientfd, (void *)rbuf, cnt, 0)) == -1) {
		    perror("send");
		    //pthread_exit((void *)-1);
		    return (void *)-1;
		}
		DEBUG("sent cnt: %d\n", (int)cnt);
	    }
	}
	
    } while (len > 0); // socket data stream closed

    // Free buffers
    free(buf);
    free(rbuf);

    // Close data file
    close(fd);

    // Release mutex lock
    /*
    ret = pthread_mutex_unlock(&mutex);
    DEBUG("pthread_mutex_unlock return: %d\n", ret);
    if (ret != 0) {
	error(-1, ret, "pthread_mutex_unlock");
    }
    */
    // Exit thread with success
    //pthread_exit((void *)0);
    return (void *)0;
}

//===================================================================
// main
//===================================================================

int main(int argc, char *argv[])
{
    struct addrinfo hints, *res;
    struct sockaddr_storage client_addr;
    struct sockaddr_in *client_addrptr = (struct sockaddr_in *)&client_addr;
    socklen_t client_addrsize;
    int sockfd, clientfd;
    int status;
    int yes=1;
    char client_ipstr[INET_ADDRSTRLEN];
    struct sigaction act;
    int ret, retval = 0;
    pthread_t thread;

    // Setup up signal handling
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_handler = signal_handler;
    if (sigaction(SIGINT, &act, NULL) == -1) {
	perror("sigaction SIGINT");
	exit(-1);
    }
    if (sigaction(SIGTERM, &act, NULL) == -1) {
	perror("sigaction SIGTERM");
	exit(-1);
    }

    // Open syslog
    openlog(argv[0], LOG_CONS | LOG_PERROR | LOG_PID | LOG_NDELAY, LOG_USER);

    // Setup getaddrinfo hints
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    // Get address info
    if ((status = getaddrinfo(NULL, PORT, &hints, &res)) != 0) {
	fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
	exit(-1);
    }

    // Check that only one address exists
    //DEBUG("res->ai_next: %p\n", (void *)res->ai_next);
    //for (int *p = res; p != NULL; p = p->ai_next) {
    //	if (p->ai_family == AF_INET) break;
    //}
    if (res->ai_next != NULL) {
	fprintf(stderr, "More that one address identified\n");
	exit(-1);
    }

    // Create socket
    if ((sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol)) == -1) {
	perror("socket");
	exit(-1);
    }

    // Set socket options to reuse address
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) == -1) {
	perror("setsockopt");
	exit(-1);
    }

    // Bind socket to port
    if (bind(sockfd, res->ai_addr, res->ai_addrlen) == -1) {
	perror("bind");
	exit(-1);
    }

    // Switch to daemon mode if argument -d is passed
    DEBUG("argc: %d argv[1]: %s\n", argc, argv[1]);
    if (argc == 2 && strncmp(argv[1], "-d", 2) == 0) {
	DEBUG("daemon mode\n");
	if (daemon(0, 0) == -1) {
	    perror("daemon");
	    exit(-1);
	}
    }

    // Listen for client connection request
    if (listen(sockfd, BACKLOG) == -1) {
	perror("listen");
	exit(-1);
    }

    // Loop listening for clients unti SIGINT or SIGTERM is received
    while(true) {

        // Accept connection from client
	client_addrsize = sizeof client_addr;
        if ((clientfd = accept(sockfd, (struct sockaddr *)&client_addr, &client_addrsize)) == -1) {
	    if (sigcaught) goto CLEANUP;
	    perror("accept");
	    exit(-1);
        }
        if (inet_ntop(client_addrptr->sin_family, (void *)&client_addrptr->sin_addr, client_ipstr, sizeof client_ipstr) == NULL) {
	    perror("inet_ntop");
	    exit(-1);
        }

        // Log accepted client IP to syslog
        syslog(LOG_INFO, "Accepted connection from %s\n", client_ipstr);

	// TODO begin
	
	ret = pthread_create(&thread, NULL, clientthread, &clientfd);
	if (ret != 0) {
	    error(-1, ret, "pthread_create");
	}

	ret = pthread_join(thread, (void **)&retval);
	if (ret != 0) {
	    error(-1, ret, "pthread_join");
	}
	DEBUG("thread retval: %d\n", retval);

	// TODO end
	
        // Close client file descriptor
        if (close(clientfd) == -1) {
	    perror("client close");
	    exit(-1);
        }
    	
        // Log closed client IP address to syslog
        syslog(LOG_INFO, "Closed connection from %s\n", client_ipstr);

    } // End of client loop

 CLEANUP:

    // Handle exceptions SIGINT and SIGTERM
    if (sigcaught) {
	syslog(LOG_INFO, "Caught signal, exiting");
	if (access(DATAFILE, F_OK) == 0) {
	    if (remove(DATAFILE) == -1) {
		perror("remove");
		exit(-1);
	    }
	}
    }

    // Shutdown network connection
    if (shutdown(sockfd, SHUT_RDWR) == -1) {
	perror("shutdown");
	exit(-1);
    }

    // Close socket file descriptor
    if (close(sockfd) == -1) {
	perror("socket close");
	exit(-1);
    }

    // Free address info
    freeaddrinfo(res);

    // Close syslog
    closelog();

    // Exit cleanly
    exit(0);
}


