#include <stdlib.h>
#include <sys/syslog.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>

#define DATA_FILE_PATH "/var/tmp/aesdsocketdata"
#define RECV_BUFFER_SIZE 128
#define DEBUG 0

int sockfd;
int sig_close = 0;

// Cleanup and close
void cleanup_close(int fd) {
	remove(DATA_FILE_PATH);
	close(sockfd);
	exit(fd);
}

// Called after packet completion to write to:
// - Write to the file
// - Send the file
void packet_complete(int sfd,char *buffer) {
	FILE *fp;
	fp = fopen(DATA_FILE_PATH, "a+");
	if (fp == NULL){
		syslog(LOG_ERR, "Error on opening file: %s", strerror(errno));
		cleanup_close(EXIT_FAILURE);
	}
	if (fprintf(fp, "%s\n", buffer) < 0) {
		syslog(LOG_ERR, "Error on writing to file: %s", strerror(errno));
		fclose(fp);
		cleanup_close(EXIT_FAILURE);
	}
	if (fseek(fp, 0, SEEK_SET) != 0) {
		syslog(LOG_ERR, "Error on seeking file: %s", strerror(errno));
		fclose(fp);
		cleanup_close(EXIT_FAILURE);
	}
	
	char *line = NULL;
	size_t len = 0;
	ssize_t read;
	
	while ((read = getline(&line,&len,fp)) != -1) {
		if (send(sfd,line,read,MSG_NOSIGNAL) == -1) {
			syslog(LOG_ERR, "Error on sending: %s", strerror(errno));
			cleanup_close(EXIT_FAILURE);
		}
	}
	
	free(line);
	fclose(fp);
}

// Signal handler
static void signal_handler() {
	syslog(LOG_INFO, "Caught signal, exiting");
	shutdown(sockfd,SHUT_RDWR);
	sig_close = 1;
}

// Create daemon
void create_daemon() {
	pid_t pid;
	pid = fork();
	if (pid == -1) {
		syslog(LOG_ERR, "Error on creating daemon: %s", strerror(errno));
		exit(EXIT_FAILURE);
	} else if (pid != 0) {
		exit(EXIT_SUCCESS);
	}
	if (setsid () == -1) {
		syslog(LOG_ERR, "Error on creating daemon: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}
	if (chdir ("/") == -1) {
		syslog(LOG_ERR, "Error on creating daemon: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}
	open("/dev/null", O_RDWR);
	dup(0);
	dup(0);
}

int main(int argc, char *argv[]) {
	//Open syslog file
	openlog("aesdsocket", LOG_PERROR * DEBUG, LOG_USER);
	
	//Open syslog file
	// Register Signals
	struct sigaction saction;
	memset(&saction,0,sizeof(saction));
	saction.sa_handler=signal_handler;
	if (sigaction(SIGTERM,&saction,NULL) != 0) {
		syslog(LOG_ERR, "Error on SIGTERM signal register: %s", strerror(errno));
	}
	if (sigaction(SIGINT,&saction,NULL) != 0) {
		syslog(LOG_ERR, "Error on SIGINT signal register: %s", strerror(errno));
	}
	
	//Open socket stream
	sockfd = socket(PF_INET, SOCK_STREAM, 0);

	int optval = 1;
	setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &optval, sizeof(optval));

	// Bind to port
	struct addrinfo hints;
	struct addrinfo *serverinfo;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	if (getaddrinfo(NULL,"9000",&hints,&serverinfo) != 0){
		syslog(LOG_ERR, "Error on getaddrinfo: %s", strerror(errno));
		exit(EXIT_FAILURE);	
	}
	
	if (bind(sockfd,serverinfo->ai_addr,serverinfo->ai_addrlen) != 0){
		syslog(LOG_ERR, "Error on binding: %s", strerror(errno));
		freeaddrinfo(serverinfo);
		exit(EXIT_FAILURE);
	}

	freeaddrinfo(serverinfo);
	
	//Start listening
	syslog(LOG_INFO, "Bounded to port 9000");

	if (listen(sockfd, 8) != 0) {
		syslog(LOG_ERR, "Error on listening: %s", strerror(errno));
		cleanup_close(EXIT_FAILURE);
	}
	
	char opt;
	if ((opt = getopt(argc, argv, "d")) != -1) {
		if (opt != 'd') {
			syslog(LOG_ERR, "Invalid option - exiting");
			exit(EXIT_FAILURE);
		}
		syslog(LOG_INFO, "Option -d detected");
		create_daemon();
	}

	// Wait, accpet and handle connections
	while (1) {
		syslog(LOG_INFO, "Listening...");

		// Accept connection
		struct sockaddr_in addr;
		socklen_t addr_len=sizeof(addr);
		memset(&addr, 0, sizeof(addr));
		int fd = accept(sockfd,(struct sockaddr*)  &addr, &addr_len);
		if (fd == -1) {
			if (errno != EINTR) {
				syslog(LOG_ERR, "Error on accepting: %s", strerror(errno));
				cleanup_close(EXIT_FAILURE);
			} else {
				cleanup_close(EXIT_SUCCESS);
			}
		}

		//Extract human readable IP
		char client_ip[INET_ADDRSTRLEN]="";
		inet_ntop(AF_INET, &(addr.sin_addr), client_ip, INET_ADDRSTRLEN);
		syslog(LOG_INFO, "Accepted connection from %s",client_ip);
				
		//Handle each packet
		char *buffer=malloc(sizeof(char));
		*buffer='\0';
		int size=1;
		while (1) {
			char recv_buff[RECV_BUFFER_SIZE + 1];
			int received = recv(fd, recv_buff, RECV_BUFFER_SIZE, 0);
			if (received == -1) {
				syslog(LOG_ERR, "Error on receiving: %s", strerror(errno));
				close(fd);
				free(buffer);
				cleanup_close(EXIT_FAILURE);
			} else if (received == 0) {
				syslog(LOG_INFO, "Closed connection from %s",client_ip);
				break;
			}
			recv_buff[received]='\0';
			char *remain = recv_buff;
			char *token = strsep(&remain, "\n");
		   	if (remain  == NULL) { //Packet is not completed
				size += received;
				buffer = realloc(buffer, size*sizeof(char));
				//Append received chars to buffer
				strncat(buffer, token, received);
			} else { //Packet in completed
				size += strlen(token);
				buffer = realloc(buffer, size*sizeof(char));
				strcat(buffer, token);

				syslog(LOG_INFO, "Received packet: %s",buffer);

				//Write to file and send
				packet_complete(fd,buffer);

				//Store remaining characters in buffer (if any)
				size=strlen(remain)+1;
				buffer = realloc(buffer, size*sizeof(char));
				strcpy(buffer, remain);
			}
		}
		//Cleanup
		free(buffer);
		close(fd);
	}
}
