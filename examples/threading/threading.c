#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...)
//#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void* threadfunc(void* thread_param)
{

    // TODO: wait, obtain mutex, wait, release mutex as described by thread_data structure
    // hint: use a cast like the one below to obtain thread arguments from your parameter
	// struct thread_data* thread_func_args = (struct thread_data *) thread_param;
	
	struct thread_data* thread_func_args = (struct thread_data *) thread_param;
	int rc;
	usleep( 1000 * (thread_func_args->obtaining_wait_time) );
	rc = pthread_mutex_lock(thread_func_args->mutex);
	if (!rc){
		usleep( 1000 * (thread_func_args->releasing_wait_time) );
		rc = pthread_mutex_unlock(thread_func_args->mutex);
 		if (!rc){
 			thread_func_args->thread_complete_success = true;
 		}else{
 			thread_func_args->thread_complete_success = false;
 		}
 	}else{
 		thread_func_args->thread_complete_success = false;
 	}	
    return thread_param;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,int wait_to_obtain_ms, int wait_to_release_ms)
{
    /**
     * TODO: allocate memory for thread_data, setup mutex and wait arguments, pass thread_data to created thread
     * using threadfunc() as entry point.
     *
     * return true if successful.
     *
     * See implementation details in threading.h file comment block
     */
     struct thread_data *thread_param = (struct thread_data *) malloc (sizeof (struct thread_data));
	 thread_param->mutex = mutex;
	 thread_param->obtaining_wait_time = wait_to_obtain_ms;
 	 thread_param->releasing_wait_time = wait_to_release_ms;
	 int rc;
     rc = pthread_create (thread, NULL, threadfunc, (void *) thread_param);
     if (!rc){
 		return true;
     }
     
    return false;
}

