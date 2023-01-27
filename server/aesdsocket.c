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
#define BUFFER_SIZE 128 		
#define DATAFILE_PATH "/var/tmp/aesdsocketdata"


bool signal_flag = false;
int sockfd;
void sig_handler(int sig_num);
void *get_in_addr(struct sockaddr *sa);

int main(int argc, char** argv)
{
	int new_fd, rv, yes=1;
	char *packetBuffer, *recvBuffer, s[INET6_ADDRSTRLEN];
	ssize_t no_bytes;  // this one is signed
	size_t  total = 0; // total is for buffers total size
	socklen_t sin_size; // size of addr string
	FILE *fp;
	struct sigaction siga;
	struct addrinfo hints, *res, *p;
	struct sockaddr_storage their_addr;
	
//==============
	openlog(NULL, 0, LOG_USER);
//==============

	// Set the hints
	memset(&(hints), 0, sizeof(hints));
	hints.ai_flags = AI_PASSIVE;
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = 0;
//==============

	// Get the addr info
	if ((rv = getaddrinfo(NULL, PORT, &hints, &res)) !=0)
	{	perror("Error: getaddrinfo");
		syslog(LOG_ERR, "getaddrinfo error: %s\n", gai_strerror(rv));
		exit(EXIT_FAILURE);
	}
//==============

	//loop on the res linked list until a socket is binded
	for (p=res; p!=NULL; p = p->ai_next)
	{
		if ((sockfd=socket(p->ai_family, p->ai_socktype,
				   p->ai_protocol)) == -1)
		{
			perror("Couldn't create socket");
			syslog(LOG_ERR, "Serevr: Socket");
			continue;
		} 
		
		if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
			       sizeof(yes)) == -1)
		{
			perror("Couldn't Set option");
			syslog(LOG_ERR, "Server: Option");
			shutdown(sockfd, SHUT_RDWR);
			exit(EXIT_FAILURE);
		}
		
		if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1)
		{
			perror("Couldn't Bind"); 
			syslog(LOG_ERR, "Server: bind");
			continue;
		}
		// socket creation and binding is done.
		syslog(LOG_DEBUG, "Socket created");
		break;
	}
	freeaddrinfo(res);
	
	// make sure the for terminated because it binded not runover
	if (p == NULL)
	{
		perror("Server didn't bind");
		syslog(LOG_ERR, "Server: Finished without binding");
		shutdown(sockfd, SHUT_RDWR);
		exit(EXIT_FAILURE);
	}
//==============

	//listen
	if (listen(sockfd, BACKLOG) == -1)
	{
		perror("Server didn't listen");
		syslog(LOG_ERR, "Server: listen");
		shutdown(sockfd, SHUT_RDWR);
		exit(EXIT_FAILURE);
	}
//==============
	
	//set the action handlers
	siga.sa_handler = sig_handler;
	sigemptyset(&siga.sa_mask);
	siga.sa_flags = SA_RESTART;
	if (sigaction(SIGINT, &siga, NULL) == -1 || 
	    sigaction(SIGTERM, &siga, NULL) == -1)
	{
		perror("sigaction register");
		syslog(LOG_ERR, "sigaction couldn't register");
		shutdown(sockfd, SHUT_RDWR);
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
				shutdown(sockfd, SHUT_RDWR);
				exit(EXIT_FAILURE);
			}
		}
	}

	//main loop
	while(signal_flag == false)
	{
		if ((new_fd = accept(sockfd, (struct sockaddr *)&their_addr,
				     &sin_size)) == -1)
		{
			perror("Error accepting");
			syslog(LOG_ERR, "Server: Accept");
			continue;
		}
		
		// get the address string
		memset(&s, 0, INET6_ADDRSTRLEN);
		inet_ntop(their_addr.ss_family,
			  get_in_addr((struct sockaddr *)&their_addr), s,
			  sizeof s);
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
						remove(DATAFILE_PATH);
						shutdown(sockfd, SHUT_RDWR);
						shutdown(new_fd, SHUT_RDWR);
						exit(EXIT_FAILURE);
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
		shutdown(new_fd, SHUT_RDWR);
	}
	shutdown(sockfd, SHUT_RDWR);
	remove(DATAFILE_PATH);
	exit(EXIT_SUCCESS);
}

void sig_handler(int sig_num)
{
	if (sig_num == SIGINT || sig_num == SIGTERM)
	{
		signal_flag = true;
		syslog(LOG_INFO, "Caught signal, exiting");
		shutdown(sockfd, SHUT_RDWR);
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
