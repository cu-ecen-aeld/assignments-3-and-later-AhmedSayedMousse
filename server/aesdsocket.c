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

//==============
// 	MACROS
//==============
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
	int new_fd, rv, yes=1;
	char *packetBuffer, s[INET6_ADDRSTRLEN];
	ssize_t no_bytes;  // this one is signed
	socklen_t sin_size; // size of addr string
	FILE *fp;
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
			if (daemon(0, 0) == -1)
			{
				perror("Daemon error");
				syslog(LOG_ERR, "Daemon error");
				exit(EXIT_FAILURE);
			}
		}
	}

	//main loop
	while(!signal_flag)
	{	syslog(LOG_INFO, "Entered mainloop");
		sin_size = sizeof their_addr;
		if ((new_fd = accept(sockfd, their_addrPtr,
				     &sin_size)) == -1)
		{
			if ( errno == EINTR) clean_close();
			perror("Error accepting");
			syslog(LOG_ERR, "Server: Accept");
			exit(EXIT_FAILURE);
		}
		
		// get the address string
		if (inet_ntop(their_addr.ss_family,
			  get_in_addr(their_addrPtr), s, sizeof s) == NULL)
		{
			syslog(LOG_ERR, "inet_ntop");
			exit(EXIT_FAILURE);
		}
		syslog(LOG_DEBUG, "Accepted connection from %s", s);

		if ((fp = fopen(DATAFILE_PATH, "a+")) == NULL)
		{
			perror("OPEN");
			syslog(LOG_ERR, "OPEN");
			exit(EXIT_FAILURE);
		}

		packetBuffer = malloc(BUFFER_SIZE+1);
		char *line=NULL;
		size_t len = 0;
		do
		{
			syslog(LOG_INFO, "Entered acceptloop");
			if ((no_bytes = recv(new_fd, packetBuffer, BUFFER_SIZE, 0)) == -1)
			{
				perror("Recieve");
				syslog(LOG_ERR, "Receive error");
				exit(EXIT_FAILURE);
			}
			packetBuffer[no_bytes] = '\0';
			if (fprintf(fp, "%s", packetBuffer) <0)
			{
				perror("printing to file");
				syslog(LOG_ERR, "printing to file");
				exit(EXIT_FAILURE);
			}
			if (index(packetBuffer, '\n') != NULL)
			{
				// set the start to the begining of the file
				rewind(fp);
				// read line by line and send on the socket
				while((no_bytes = getline(&line, &len, fp)) != -1)
				{
					if (send(new_fd, line, no_bytes, 0) == -1)
					{
						perror("Failed to send");
						syslog(LOG_ERR, "Send");
						exit(EXIT_FAILURE);
					}
					syslog(LOG_INFO, "Sent %ld", no_bytes);
				}
			}
			
		}while(no_bytes >0);
		syslog(LOG_INFO, "Exited acceptloop");
		free(packetBuffer);
		fclose(fp);
		close(new_fd);
	  	syslog(LOG_INFO, "Closed connection from %s\n", s);
	}
	syslog(LOG_INFO, "Exited mainloop");
	shutdown(sockfd, SHUT_RDWR);
	close(sockfd);
	clean_close();
}


