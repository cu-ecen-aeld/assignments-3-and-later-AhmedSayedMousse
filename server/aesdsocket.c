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

//==============
// 	MACROS
//==============
#define DEBUG(msg, ...)
#define PORT "9000" 			
#define BACKLOG 10   			
#define BUFFER_SIZE 1024 		
#define DATAFILE_PATH "/var/tmp/aesdsocketdata"

//==============
// 	GLOBALS
//==============
bool signal_flag = false;
int sockfd;
//====================================
// 	HELPER FUNCTIONS
//=====================================
void *accepting_thread_function(void * clientfdptr){
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
		rbuf[cnt] = '\0';
		DEBUG("send cnt: %d rbuf: |%s|\n", (int)cnt, rbuf);
		if ((cnt = send(clientfd, (void *)rbuf, cnt, 0)) == -1) {
		    perror("send");
		    //pthread_exit((void *)-1);
		    return (void *)-1;
		}
		DEBUG("sent cnt: %d\n", (int)cnt);
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

void  clean_close(void)
{
	syslog(LOG_ERR, "Closing");
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
		close(sockfd);
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

//==============
// 	MAIN
//==============
int main(int argc, char** argv)
{
	int  new_fd, rv, yes=1;
	char s[INET6_ADDRSTRLEN];
	socklen_t sin_size; // size of addr string
	pthread_t thread;
	void *retval;
	struct sigaction siga;
	struct addrinfo hints, *res;
	struct sockaddr_storage their_addr;
	struct sockaddr *their_addrPtr = (struct sockaddr *)&their_addr;
	
//==============
	openlog(NULL, LOG_NDELAY | LOG_PERROR | LOG_PID | LOG_CONS, LOG_USER);
//==============

	// Set the hints
	memset(&(hints), 0, sizeof(hints));
	hints.ai_flags = AI_PASSIVE;
	hints.ai_family = AF_UNSPEC;
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
	
	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &yes, sizeof yes) == -1)
	{
		perror("Couldn't Set option");
		syslog(LOG_ERR, "Server: Option");
		exit(EXIT_FAILURE);
	}
	
	if (bind(sockfd,res->ai_addr, res->ai_addrlen) == -1)
	{
		perror("Couldn't Bind"); 
		syslog(LOG_ERR, "Server: bind");
		exit(EXIT_FAILURE);

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
		exit(EXIT_FAILURE);
	}
//==============
	
	//set the action handlers
	siga.sa_handler = sig_handler;
	if (sigaction(SIGINT, &siga, NULL) == -1)
	{
		perror("SIGINT register");
		syslog(LOG_ERR, "SIGINT couldn't register");
		exit(EXIT_FAILURE);
	}
	if (sigaction(SIGTERM, &siga, NULL) == -1)
	{
		perror("SIGTERM register");
		syslog(LOG_ERR, "SIGTERM couldn't register");
		exit(EXIT_FAILURE);
	}
//==============

	char opt;
	while((opt = getopt(argc, argv, "d")) != -1)
	{
		if (opt == 'd')
		{
			syslog(LOG_INFO, "Option detected");
			if ((rv = daemon(0, 0)) == -1)
			{
				perror("Daemon error");
				syslog(LOG_ERR, "Daemon error");
				exit(EXIT_FAILURE);
			}
			syslog(LOG_INFO, "Daemon created %d", rv);
		}
	}

	//main loop
	while(true)
	{	
		syslog(LOG_INFO, "Entered mainloop");
		sin_size = sizeof their_addr;
		if ((new_fd = accept(sockfd, their_addrPtr,
				     &sin_size)) == -1)
		{
			if ( errno == EINTR) clean_close();
			perror("Error accepting");
			syslog(LOG_ERR, "Server: Accept");
			exit(EXIT_FAILURE);
		}
		if (signal_flag){
			close(new_fd);
			clean_close();
		}
		// Print the ip of the accepted connection
		if (inet_ntop(their_addr.ss_family,
		  get_in_addr(their_addrPtr), s, sizeof s) == NULL)
		{
			syslog(LOG_ERR, "inet_ntop");
			exit(EXIT_FAILURE);
		}
		syslog(LOG_DEBUG, "Accepted connection from %s", s);
		// create a thread 
		if (pthread_create(&thread, NULL, accepting_thread_function, (void *)(&new_fd)) != 0)
		{
			perror("Pthread_Create");
			syslog(LOG_ERR, "Pthread_Create");
			close(new_fd);
			clean_close();
		}
		// join on the thread
		if (pthread_join(thread, &retval) != 0)
		{
			perror("Pthread_join");
			syslog(LOG_ERR, "Pthread_join");
			close(new_fd);
			clean_close();
		}
		if ( *(int *)retval == EXIT_FAILURE )
		{
			// Some error happend
			perror("Inside Thread");
			syslog(LOG_ERR, "Inside Thread");
			close(new_fd);
			clean_close();
		}
		syslog(LOG_INFO, "Closed connection from %s\n", s);
		close(new_fd);
	}
	syslog(LOG_INFO, "Exited mainloop");
	shutdown(sockfd, SHUT_RDWR);
	close(sockfd);
	clean_close();
}


