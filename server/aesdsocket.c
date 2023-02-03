#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <fcntl.h>
#include <netdb.h>
#include <syslog.h>
#include <signal.h>
#include <pthread.h>
#include <error.h>
#include <errno.h>
#include <time.h>
#include "./queue.h"
//==============
// 	MACROS
//==============
#define PORT "9000" 			
#define BACKLOG 10   			
#define BUFFER_SIZE 1024 		
#define DATAFILE_PATH "/var/tmp/aesdsocketdata"					
//==============
// SLIST STRUCTs
//==============
typedef struct thread_node{
	bool complete_flag;
	int connection_fd;
	pthread_t thread;
	SLIST_ENTRY(thread_node) threads;
}thread_t;
typedef SLIST_HEAD(slisthead, thread_node) head_t;

//==============
// 	GLOBALS
//==============
bool signal_flag = false;
time_t time_init;
pthread_t time_thread ;
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER; 
//====================================
// 	HELPER FUNCTIONS
//=====================================
void *accepting_thread_function(void * thread_data)
{
	ssize_t no_recv_bytes=0, no_sent_bytes = 0; // signed
	char packetBuffer[BUFFER_SIZE+1], *line=NULL;
	size_t len = 0;
	FILE* fp;
	thread_t * _thread = (thread_t *)thread_data;
	do
	{
		//syslog(LOG_INFO, "Entered acceptloop");
		if ((no_recv_bytes = recv(_thread->connection_fd, packetBuffer, BUFFER_SIZE, 0)) == -1)
		{

			if ( no_recv_bytes == EINTR) break;
			//perror("Recieve");
			//syslog(LOG_ERR, "Receive error");
			return (void *)EXIT_FAILURE;
		}

		//syslog(LOG_INFO, " no_bytes %ld", no_recv_bytes);
		packetBuffer[no_recv_bytes] = '\0';
		if (pthread_mutex_lock(&file_mutex) != 0)
		{
			//perror("ERROR: LOCKING");
			//syslog(LOG_ERR, "ERROR: LOCKING");
			return (void *)EXIT_FAILURE;
			
		}
		//syslog(LOG_INFO, "Received %s %ld", packetBuffer, no_recv_bytes);
		if ((fp = fopen(DATAFILE_PATH, "a+")) == NULL)
		{
			//perror("OPEN");
			//syslog(LOG_ERR, "OPEN");
			return (void *)EXIT_FAILURE;

		}
		if (fprintf(fp, "%s", packetBuffer) <0)
		{
			//perror("printing to file");
			//syslog(LOG_ERR, "printing to file");
			return (void *)EXIT_FAILURE;
		}

		if (index(packetBuffer, '\n') != NULL)
		{
			// set the start to the begining of the file
			rewind(fp);
			// read line by line and send on the socket
			while((no_sent_bytes = getline(&line, &len, fp)) != -1)
			{
				if (send(_thread->connection_fd, line, no_sent_bytes, 0) == -1)
				{
					//perror("Failed to send");
					//syslog(LOG_ERR, "Send");
					return (void *)EXIT_FAILURE;
			    }
			    //syslog(LOG_INFO, "Sent %s %ld", line, no_sent_bytes);
			}
		}
		fclose(fp);
		if (pthread_mutex_unlock(&file_mutex) != 0)
		{
			//perror("ERROR: UNLOCKING");
			//syslog(LOG_ERR, "ERROR: UNLOCKING");
			return (void *)EXIT_FAILURE;
		}
   	}while(no_recv_bytes >0 && !signal_flag);
	//syslog(LOG_ERR, "EXITED acceptloop %zd", no_recv_bytes);
	free(line);			
	_thread->complete_flag = true;
	return (void *)EXIT_SUCCESS;
}

void *time_thread_function(void *dummy)
{
	char time_str[50];
	char * time_format = "timestamp:%a, %d %b %y %T %z";
	struct tm *tmp_time;
	FILE *fp;
	time_t time_main;
	do {
		time_main = time(NULL);
		// 10 seconds have passed print a time stamp;
		tmp_time = localtime(&time_main);
		if (tmp_time == NULL)
		{
			//perror("Error: localtime");
			//syslog(LOG_ERR, "Localtime");
			return (void *)EXIT_FAILURE;
		}
		if (strftime(time_str, sizeof(time_str), time_format, tmp_time) == 0)
		{
			//perror("Error: strftime");
			//syslog(LOG_ERR, "strftime");
			return (void *)EXIT_FAILURE;
		}
		if (pthread_mutex_lock(&file_mutex) != 0)
		{
			//perror("ERROR: LOCKING");
			//syslog(LOG_ERR, "ERROR: LOCKING");
			return (void *)EXIT_FAILURE;
		}
		if ((fp = fopen(DATAFILE_PATH, "a+")) == NULL)
		{
			//perror("OPEN");
			//syslog(LOG_ERR, "OPEN");
			return (void *)EXIT_FAILURE;
		}
		if (fprintf(fp, "%s\n", time_str) <0)
		{
			//perror("printing time to file");
			//syslog(LOG_ERR, "printing time to file");
			return (void *)EXIT_FAILURE;
		}
		//syslog(LOG_ERR, "TimeStamp Written");
		fclose(fp);
		if (pthread_mutex_unlock(&file_mutex) != 0)
		{
			//perror("ERROR: UNLOCKING");
			//syslog(LOG_ERR, "ERROR: UNLOCKING");
			return (void *)EXIT_FAILURE;
		}
		sleep(10);
	}while(!signal_flag);
	return (void *)EXIT_SUCCESS;
}
void  clean_close(head_t head, int sockfd)
{
	void *retval = 0;
	thread_t *thread_list_entry = NULL;
	close(sockfd);
	//syslog(LOG_ERR, "Closing");
	if (access(DATAFILE_PATH, F_OK) == 0) {
	    if (remove(DATAFILE_PATH) == -1) {
		//perror("remove");
		//syslog(LOG_ERR, "remove");
		exit(EXIT_FAILURE);
	    }
	}
	if (pthread_join(time_thread, &retval) != 0)
	{
		//perror("Pthread_join_time_clean");
		//syslog(LOG_ERR, "Pthread_join_time_clean");
	}
	if ( retval == (void *)EXIT_FAILURE )
	{
		// Some error happend
		//perror("Inside Thread time clean");
		//syslog(LOG_ERR, "Inside Thread time clean");
	}

	if (!SLIST_EMPTY(&head))	
	{
		while(!SLIST_EMPTY(&head))
		{
				thread_list_entry = SLIST_FIRST(&head);
				// join on the thread
				if (pthread_join(thread_list_entry->thread, &retval) != 0)
				{
					//perror("Pthread_join_clean");
					//syslog(LOG_ERR, "Pthread_join_clean");
				}
				if ( retval == (void*)EXIT_FAILURE )
				{
					// Some error happend
					//perror("Inside Thread clean");
					//syslog(LOG_ERR, "Inside Thread clean");
				}
				close(thread_list_entry->connection_fd);
				SLIST_REMOVE_HEAD(&head, threads);
				free(thread_list_entry);		
		
		}
	}

	if (signal_flag)
		exit(EXIT_SUCCESS);
	exit(EXIT_FAILURE);
}
void sig_handler(int sig_num)
{
	signal_flag = true;	
	//syslog(LOG_INFO, "Caught signal, exiting");
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
	int  sockfd, new_fd, rv, yes=1;
	char s[INET_ADDRSTRLEN];
	void *retval = 0;
	socklen_t sin_size; // size of addr string
	pthread_t thread;
	head_t head;
	thread_t *thread_list_entry = NULL, *thread_list_tmp = NULL;
	struct sigaction siga;
	struct addrinfo hints, *res;
	struct sockaddr_storage their_addr;
	struct sockaddr *their_addrPtr = (struct sockaddr *)&their_addr;
	SLIST_INIT(&head);

//=============================
//	MUTEX & TIME INITIALIZATION
//=============================
	pthread_mutex_init(&file_mutex, NULL);
	time_init = time(NULL);
//==============
//	openlog(NULL, LOG_NDELAY | LOG_PERROR | LOG_PID | LOG_CONS, LOG_USER);
//==============

	//set the action handlers
   	memset(&siga, 0, sizeof(struct sigaction));
	siga.sa_handler = sig_handler;
	if (sigaction(SIGINT, &siga, NULL) == -1)
	{
		//perror("SIGINT register");
		//syslog(LOG_ERR, "SIGINT couldn't register");
		exit(EXIT_FAILURE);
	}
	if (sigaction(SIGTERM, &siga, NULL) == -1)
	{
		//perror("SIGTERM register");
		//syslog(LOG_ERR, "SIGTERM couldn't register");
		exit(EXIT_FAILURE);
	}
//==============

	// Set the hints
	memset(&(hints), 0, sizeof(hints));
	hints.ai_flags = AI_PASSIVE;
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
//==============

	// Get the addr info
	if ((rv = getaddrinfo(NULL, PORT, &hints, &res)) !=0)
	{	//perror("Error: getaddrinfo");
		//syslog(LOG_ERR, "getaddrinfo error: %s\n", gai_strerror(rv));
		exit(EXIT_FAILURE);
	}
	if (res->ai_next != NULL) {
		//perror("More that one address identified\n");
		exit(EXIT_FAILURE);
    }
//==============

	//
	if ((sockfd=socket(res->ai_family, res->ai_socktype,res->ai_protocol)) == -1)
	{
		//perror("Couldn't create socket");
		//syslog(LOG_ERR, "Server: Socket");
		exit(EXIT_FAILURE);
	}

	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &yes, sizeof yes) == -1)
	{
		//perror("Couldn't Set option");
		//syslog(LOG_ERR, "Server: Option");
		exit(EXIT_FAILURE);
	}

	if (bind(sockfd,res->ai_addr, res->ai_addrlen) == -1)
	{
		//perror("Couldn't Bind");
		//syslog(LOG_ERR, "Server: bind");
		exit(EXIT_FAILURE);

	}
	// socket creation and binding is done.
	//syslog(LOG_DEBUG, "Socket created");
	freeaddrinfo(res);
//==============

	char opt;
	while((opt = getopt(argc, argv, "d")) != -1)
	{
		if (opt == 'd')
		{
			//syslog(LOG_INFO, "Option detected");
			if ((rv = daemon(0, 0)) == -1)
			{
				//perror("Daemon error");
				//syslog(LOG_ERR, "Daemon error");
				exit(EXIT_FAILURE);
			}
			//syslog(LOG_INFO, "Daemon created %d", rv);
			break;
		}

	}
//==============

	//listen
	if (listen(sockfd, BACKLOG) == -1)
	{
		//perror("Server didn't listen");
		//syslog(LOG_ERR, "Server: listen");
		exit(EXIT_FAILURE);
	}
//===========================
// TIME THREAD INITIALIZATION
//===========================
// create a thread

	if (pthread_create(&time_thread, NULL, time_thread_function, NULL) != 0)
	{
		//perror("Pthread_Create_time");
		//syslog(LOG_ERR, "Pthread_Create_time");
		exit(EXIT_FAILURE);
	}
//============================
		//main loop
	while(true)
	{	
		//syslog(LOG_INFO, "Entered mainloop");
		sin_size = sizeof their_addr;
		if ((new_fd = accept(sockfd, their_addrPtr, &sin_size)) == -1)
		{
			if ( errno == EINTR) 
				break;
			//perror("Error accepting");
			//syslog(LOG_ERR, "Server: Accept");
			exit(EXIT_FAILURE);
		}
		if (signal_flag){
			close(new_fd);
			break;
		}
		// Create a new node and add it to the list
		thread_list_entry = malloc(sizeof(thread_t));			
		thread_list_entry->complete_flag = false;		
		thread_list_entry->connection_fd = new_fd;	
		// create a thread 
		if (pthread_create(&thread, NULL, accepting_thread_function, (void *)(thread_list_entry)) != 0)
		{
			//perror("Pthread_Create");
			//syslog(LOG_ERR, "Pthread_Create");
			close(new_fd);
			free(thread_list_entry);
			break;
		}	
		// Print the ip of the accepted connection
		if (inet_ntop(their_addr.ss_family, get_in_addr(their_addrPtr), s, sizeof s) == NULL)
		{
			//syslog(LOG_ERR, "inet_ntop");
			exit(EXIT_FAILURE);
		}
		//syslog(LOG_DEBUG, "Accepted connection from %s", s);

		
		thread_list_entry->thread = thread;	
		SLIST_INSERT_HEAD(&head, thread_list_entry, threads);	
		// LOOP on the threads in the list and join the ones that completed
		SLIST_FOREACH_SAFE(thread_list_entry, &head, threads, thread_list_tmp)
		{
			if (thread_list_entry->complete_flag == true)
			{	
				// join on the thread
				if (pthread_join(thread_list_entry->thread, &retval) != 0)
				{
					//perror("Pthread_join");
					//syslog(LOG_ERR, "Pthread_join");
					break;
				}
				if ( retval == (void*)EXIT_FAILURE )
				{
					// Some error happend
					//perror("Inside Thread");
					//syslog(LOG_ERR, "Inside Thread");
					break;
				}
				SLIST_REMOVE(&head, thread_list_entry, thread_node, threads);
				close(thread_list_entry->connection_fd);
				free(thread_list_entry);		
			}
		
		}
		
	}
	//syslog(LOG_INFO, "Exited mainloop");
//======================
//	MUTEX DESTROYING
//======================
	pthread_mutex_destroy(&file_mutex);
//=======================
	clean_close(head, sockfd);
}


