#include <string.h> 
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h> 
#include <syslog.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>

#define PORT "9000" 			
#define BACKLOG 1   			
#define BUFFER_SIZE 1024 		
#define DATAFILE_PATH "/var/tmp/aesdsocketdata"


bool signal_flag = false;
int sockfd;
void sig_handler(int sig_num);
void *get_in_addr(struct sockaddr *sa);
void  clean_close(void);
void *clientthread(void *clientfdptr);

int main(int argc, char** argv)
{
	int new_fd, rv, yes=1;
	char  s[INET_ADDRSTRLEN];
	//ssize_t no_bytes;  // this one is signed
	socklen_t sin_size; // size of addr string

	struct sigaction siga;
	struct addrinfo hints, *res;
	struct sockaddr_storage their_addr;
	
//==============
	openlog(NULL, 0, LOG_USER);
//==============

	// Set the hints
	memset(&(hints), 0, sizeof(hints));
	hints.ai_flags = AI_PASSIVE;
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
//==============

	// Get the addr info
	if ((rv = getaddrinfo(NULL, PORT, &hints, &res)) !=0)
	{	perror("Error: getaddrinfo");
		syslog(LOG_ERR, "getaddrinfo error: %s\n", gai_strerror(rv));
		exit(EXIT_FAILURE);
	}
//==============

	// 
	if ((sockfd=socket(res->ai_family, res->ai_socktype,
				   res->ai_protocol)) == -1)
	{
		perror("Couldn't create socket");
		syslog(LOG_ERR, "Server: Socket");
		exit(EXIT_FAILURE);
	} 
	
	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
		       sizeof yes) == -1)
	{
		perror("Couldn't Set option");
		syslog(LOG_ERR, "Server: Option");
		clean_close();
	}
	
	if (bind(sockfd,res->ai_addr, res->ai_addrlen) == -1)
	{
		perror("Couldn't Bind"); 
		syslog(LOG_ERR, "Server: bind");
		clean_close();

	}
	// socket creation and binding is done.
	syslog(LOG_DEBUG, "Socket created");
	freeaddrinfo(res);
	
//==============

	//listen
	if (listen(sockfd, BACKLOG) == -1)
	{
		perror("Server didn't listen");
		syslog(LOG_ERR, "Server: listen");
		clean_close();
	}
//==============
	
	//set the action handlers
	siga.sa_handler = sig_handler;
	if (sigaction(SIGINT, &siga, NULL) == -1)
	{
		perror("SIGINT register");
		syslog(LOG_ERR, "SIGINT couldn't register");
		clean_close();
	}
	if (sigaction(SIGTERM, &siga, NULL) == -1)
	{
		perror("SIGTERM register");
		syslog(LOG_ERR, "SIGTERM couldn't register");
		clean_close();
	}
//==============

	char opt;
	while((opt = getopt(argc, argv, "d")) != -1)
	{
		if (opt == 'd')
		{
			syslog(LOG_INFO, "Option detected");
			if (daemon(0, 0) == -1)
			{
				perror("Daemon error");
				syslog(LOG_ERR, "Daemon error");
				clean_close();
			}
		}
	}

	//main loop
	while(!signal_flag)
	{
		sin_size = sizeof their_addr;
		if ((new_fd = accept(sockfd, (struct sockaddr *)&their_addr,
				     &sin_size)) == -1)
		{
			perror("Error accepting");
			syslog(LOG_ERR, "Server: Accept");
			clean_close();
		}
		
		// get the address string
		memset(&s, 0, INET6_ADDRSTRLEN);
		if (inet_ntop(their_addr.ss_family,
			  get_in_addr((struct sockaddr *)&their_addr), s,
			  sizeof s) == NULL)
		{
			syslog(LOG_ERR, "inet_ntop");
			clean_close();
		}
		syslog(LOG_DEBUG, "Accepted connection from %s", s);
		
		int ret, retval = 0;
		pthread_t thread;

		ret = pthread_create(&thread, NULL, clientthread, &new_fd);
		if (ret != 0) {

		}

		ret = pthread_join(thread, (void **)&retval);
		if (ret != 0) {

		}


		// TODO end
		
		// Close client file descriptor
		if (close(new_fd) == -1) {
		    perror("client close");
		    exit(-1);
		}
	    	
		// Log closed client IP address to syslog
		syslog(LOG_INFO, "Closed connection from %s\n", s);
	}
	clean_close();
}
void  clean_close(void)
{
	syslog(LOG_ERR, "Closing");
	shutdown(sockfd, SHUT_RDWR);
	close(sockfd);
	if (access(DATAFILE_PATH, F_OK) == 0) {
	    if (remove(DATAFILE_PATH) == -1) {
		perror("remove");
		syslog(LOG_ERR, "remove");
		exit(EXIT_FAILURE);
	    }
	}
	if (signal_flag)
		exit(EXIT_SUCCESS);
	exit(EXIT_FAILURE);
}
void sig_handler(int sig_num)
{
	if (sig_num == SIGINT || sig_num == SIGTERM)
	{
		signal_flag = true;
		syslog(LOG_INFO, "Caught signal, exiting");
	}
	return;
}

void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

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
    if (access(DATAFILE_PATH, F_OK) != 0) flags |= O_CREAT;
    if ((fd = open(DATAFILE_PATH, flags, 0644)) == -1) {
	    perror("open");
	    //pthread_exit((void *)-1);
	    return (void *)-1;
    }

	// Allocate buffers
    buf = (char *)malloc(BUFFER_SIZE+1);
    rbuf = (char *)malloc(BUFFER_SIZE+1);

    // Read incoming socket data stream, write to data file and return response to outgoing data stream
    do {
	// read incoming socket data stream
	if ((len = recv(clientfd, (void *)buf, BUFFER_SIZE, 0)) == -1) {
	    perror("recv");
	    //pthread_exit((void *)-1);
	    return (void *)-1;
	}
	
	// write to data file
	buf[len] = '\0';
	
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
	    while ((cnt = read(fd, rbuf, BUFFER_SIZE)) != 0) {
		rbuf[cnt] = '\0';

		if ((cnt = send(clientfd, (void *)rbuf, cnt, 0)) == -1) {
		    perror("send");
		    //pthread_exit((void *)-1);
		    return (void *)-1;
		}
	
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
