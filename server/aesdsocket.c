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

#define PORT "9000" 			
#define BACKLOG 1   			
#define BUFFER_SIZE 1024 		
#define DATAFILE_PATH "/var/tmp/aesdsocketdata"


bool signal_flag = false;
int sockfd;
void sig_handler(int sig_num);
void *get_in_addr(struct sockaddr *sa);
void  clean_close(void);

int main(int argc, char** argv)
{
	int new_fd, rv, yes=1;
	char *packetBuffer, *recvBuffer, s[INET6_ADDRSTRLEN];
	ssize_t no_bytes;  // this one is signed
	size_t  total = 0; // total is for buffers total size
	socklen_t sin_size; // size of addr string
	FILE *fp;
	struct sigaction siga;
	struct addrinfo hints, *res;
	struct sockaddr_storage their_addr;
	
//==============
	openlog(NULL, 0, LOG_USER);
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

		packetBuffer = malloc(sizeof(char));
		*packetBuffer = '\0';
		total = 1;
		while(signal_flag == false)
		{
			recvBuffer = malloc(BUFFER_SIZE*sizeof(char)+1);
			memset(recvBuffer, '\0', BUFFER_SIZE+1);
			no_bytes = recv(new_fd, recvBuffer, BUFFER_SIZE, 0);
			if (no_bytes == -1)
			{
				perror("Error in receive");
				syslog(LOG_ERR, "Receiving");
				free(packetBuffer);
				free(recvBuffer);
				break;
			}
			
			if(no_bytes == 0)
			{
				perror("Connection Closed");
				syslog(LOG_DEBUG, 
				       "Closed connection from %s", s);
				free(recvBuffer);
				break;
			}
			

			total += no_bytes;
			packetBuffer = realloc(packetBuffer, 
					       total*sizeof(char));
			strncat(packetBuffer, recvBuffer, no_bytes);
			//syslog(LOG_DEBUG, "packetBuffer: %s, %ld", packetBuffer, total);
			//syslog(LOG_DEBUG, "Read %s", recvBuffer);
			if (strchr(recvBuffer, '\n') != NULL)
			{
				// packet completed
				// write it to the file
				fp = fopen(DATAFILE_PATH, "a+");
				fprintf(fp, "%s", packetBuffer);
				
				// send the file to the client
				char *line=NULL;
				size_t len = 0;
				fseek(fp, 0, SEEK_SET);
				while((no_bytes = getline(&line, &len, 
							  fp)) != -1)
				{
					//syslog(LOG_DEBUG, "Line %s" , line);
					//syslog(LOG_DEBUG, "Sending %s, %ld", line, no_bytes);
					if (send(new_fd, line, no_bytes, 0)
					    == -1)
					{
						perror("Failed to send");
						syslog(LOG_ERR, "Send");
						free(recvBuffer);
						free(packetBuffer);
						fclose(fp);
						clean_close();
					}

				}
				free(recvBuffer);
				free(line);
				fclose(fp);
				break;
			}
			free(recvBuffer);
		}
		free(packetBuffer);
		clean_close();
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
