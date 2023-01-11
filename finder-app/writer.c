
#include <syslog.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc,char* argv[]){

	openlog(NULL, 0, LOG_USER);
	if (argc != 3){
		syslog(LOG_ERR,"Error: Not enough arguments"); 
    		return 1;
	}
	FILE *fd = fopen(argv[1], "w+");
	syslog(LOG_DEBUG,"Writing %s to %s", argv[2], argv[1]);
	fprintf(fd, "%s",argv[2]);
	fclose(fd);
	closelog();
	return 0;
}
