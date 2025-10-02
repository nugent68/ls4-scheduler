/* scheduler_camera.c

   routines to allow scheduler to open a socket connection
   to the camera controller (neatsrv on quest17), send
   commands and read replies

*/

#include "scheduler.h"
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>

#define BAD_ERROR_CODE 2
#define BAD_READOUT_TIME 60.0

typedef struct{
   char *command;
   char *reply;
   int timeout;
   sem_t *start_semaphore;
   sem_t *done_semaphore;
   int command_id;
} Do_Command_Args;

// semaphores used to synchronize do_camera_command_thread
// with parent thread
sem_t command_done_semaphore; 
sem_t command_start_semaphore; 

/* set status_channel_active to True if ls4_ccp has been configured
 * to reply to status queries on a dedicated socket */

bool status_channel_active = False;

/* readout_pending will be True while an exposure is occuring  */
bool readout_pending = False;

extern int verbose;
extern int verbose1;

struct timeval t_exp_start;

Camera_Status cam_status;

extern char *host_name;

int command_id = 0;
/*****************************************************/

/* 
Take an exposure with specified parameters.  The exposure is 
executed by sending an EXPOSE_COMMAND to the camera
control program (ls4_ccp). The actions taken by ls4_ccp depend
on the specified exposure mode. 

Input parameters:
  exposure time: 
     The exposure time (in hours) is specified by 
     (a) the Field structure (f->expt) , or else
     (b) the value of *actual_expt. 
     Choice (b) overides (a) except when *actual_expt == 0.0.

  exp_mode: 
     When ls4_ccp executes the EXPOSE_COMMAND, there
     are three actions taken: expose, readout (to controller memory), and
     fetch (from controller memory to host). The exposure mode determines
     the sequencing of these actions:

     EXP_MODE_SINGLE: ls4_ccp will completely expose, readout,
     and fetch the new image data.

     EXP_MODE_FIRST: ls4_ccp will completely expose and readout
     an image, but will leave it unfetched in controller memory
     (which can hold up to three images).

     EXP_MODE_NEXT: ls4_ccp will completely expose and readout a
     new image, but will leave this new image data unfetched in 
     controller memory. Meanwhile, it will fetch the old image data 
     left unfetched from the preceding exposure.

     EXP_MODE_LAST: ls4_ccp will fetch the old image data from 
     the previous exposure.

     Normally, the fastest duty cycle is achieved using 
     FIRST, NEXT, NEXT, ..., NEXT, LAST. 

  wait_flag: 
     If True, wait for a complete exposure and readout of the image
     before returning. If False, return as soon as the exposure time
     ends. Do not wait for readout. 
     With wait_flag = False, the telesope can be moved to the next
     position while the readout and fetch occur in a separate thread.

Notes: 
    1. If the anticipated time between exposures exceeds the sum of the
    readout and fetch times (typically 25 sec), then specifying
    exp_mode = EXP_MODE_SINGLE and wait_flag = False will yield the
    same duty cycle as the mode sequence FIRST, NEXT, ... , NEXT, LAST.
    Reserve the later mode sequence for fast cadence readouts of
    repeated observations at the same pointing.

    2. If wait_flag = False, this routine will return before the exposure
    is readout. However, a check is made at a higher level
    that the readout is complete before a new exposure is taken 
    (see calls to wait_exp_done() in observe_next_field(), scheduler.c).

    
Returned parameters:

  time of exposure: 
    universal time (*ut) in hours  since 00:00:00 UT.
    julian date (*jd).

  error_code: not currently used

Return value:
  0 on success, -1 on failuer.


*/

int init_semaphores()
{
    if(verbose1){
        fprintf(stderr,"init_semaphores: initializing start_semaphore\n");
    }
    sem_init(&command_start_semaphore,0,0);

    if(verbose1){
        fprintf(stderr,"init_semaphores: initializing done_semaphore\n");
    }
    sem_init(&command_done_semaphore,0,0);

}

int take_exposure(Field *f, Fits_Header *header, double *actual_expt,
		    char *name, double *ut, double *jd,
		    bool wait_flag, int *exp_error_code, char *exp_mode)
{
    char command[MAXBUFSIZE],reply[MAXBUFSIZE];
    char filename[STR_BUF_LEN],date_string[STR_BUF_LEN],shutter_string[256],field_description[STR_BUF_LEN];
    char comment_line[STR_BUF_LEN];
    char s[256],code_string[STR_BUF_LEN],ujd_string[256],string[STR_BUF_LEN];
    struct tm tm;
    int e;
    double expt;
    int shutter;
    int timeout = 0;
    pthread_t command_thread;
    double t;

    int result = 0;
    *exp_error_code = 0;

    if(*actual_expt!=0.0){
         expt=*actual_expt*3600.0;
    }
    else {
         expt=f->expt*3600.0;
    }

    strcpy(name,"BAD");
    *name = 0;

    shutter = f->shutter;
    *ut=get_tm(&tm);
    *jd=get_jd();

    sprintf(ujd_string,"%14.6f",*jd);

    get_shutter_string(shutter_string,f->shutter,field_description);

    sprintf(date_string,"%04d%02d%02d %02d:%02d:%02d",
	tm.tm_year,tm.tm_mon,tm.tm_mday,tm.tm_hour,tm.tm_min,tm.tm_sec);

    get_filename(filename,&tm,shutter);

    if(verbose){
         fprintf(stderr,
           "take_exposure: exposing %7.1f sec  shutter: %d filename: %s  exp_mode: %s  wait_flag: %d\n",
           expt,shutter,filename,exp_mode,wait_flag);
         fflush(stderr);
    }

    sprintf(string,"%d",f->n_done+1);

    if(update_fits_header(header,SEQUENCE_KEYWORD,string)<0){
          fprintf(stderr,"take_exposure: error updating fits header %s:%s\n",
		SEQUENCE_KEYWORD,string);
          return(-1);
    }
    else if(update_fits_header(header,IMAGETYPE_KEYWORD,field_description)<0){
          fprintf(stderr,"take_exposure: error updating fits header %s:%s\n",
		IMAGETYPE_KEYWORD,field_description);
          return(-1);
    }
    else if(update_fits_header(header,FLATFILE_KEYWORD,filename)<0){
          fprintf(stderr,"take_exposure: error updating fits header %s:%s\n",
		FLATFILE_KEYWORD,filename);
       return(-1);
    }

    /* if field is a sky field and there is a comment, then the string following
       the # sign should be the field id. Put this id string in the fieldid
       keyword of the fits header
    */

    /* update comment line in FITS header with comments from script record */

    if(strstr(f->script_line,"#")!=NULL){
        sprintf(comment_line,"                      ");
        sprintf(comment_line,"'%s",strstr(f->script_line,"#")+1);
        strcpy(comment_line+strlen(comment_line)-1,"'");
    }
    else{
        sprintf(comment_line,"                      ");
        sprintf(comment_line,"'no comment'");
    }

    if(update_fits_header(header,COMMENT_KEYWORD,comment_line)<0){
          fprintf(stderr,"take_exposure: error updating fits header %s:%s\n",
		COMMENT_KEYWORD,comment_line);
       return(-1);
    }

    if(verbose1){
      fprintf(stderr,"take_exposure: imprinting fits header\n");
      fflush(stderr);
    }

    // DEBUG
    //
    //fprintf(stderr,"take_exposure: skipping imprint fits header\n");
    if(imprint_fits_header(header)!=0){
      fprintf(stderr,"take_exposure: could not imprint fits header\n");
      return(-1);
    }

    char shutter_state[12];
    if(shutter)
      strcpy(shutter_state,"True");
    else
      strcpy(shutter_state,"False");


    /* initialize command semaphores to 0. 
     *
     * If do_camera_command thread executes the expose command (wait = False),
     * that thread will pause until the start semaphore is posted (changed to 1).
     * The thread will post the done semaphore when the expose command returns.
     *
     * If the thread is  not used, neither will be posted.
     *
    */
    if(verbose1){
        fprintf(stderr,"take_exposure: set readout_pending to True\n");
    }
    readout_pending = True;

    /* If wait_flag is False, then launch a new thread (command_thread) to execute 
     * the call to do_camera_command (which sends an EXPOSE_COMMAND to the camera
     * control program (ls4_ccp). 
     *
     * Meanwhile, monitor the status of the camera. When the camera status show that  
     * the exposure time has elapsed (but the controller has not year read out the image),
     * return from the program.
     *
     * The command_thread will continue executing the exposure, and read out the
     * image when the exposure time has elapsed. It will post do the command_done_semaphore
     * just before it exists.
     *
     * If wait flag is True, do not launch a new thread. Just execute the call to 
     * to do_camera_command and wait for it to complete before returning. Set
     * exp_error_code appropriately if do_camera_command returns with an error.
     *
     * Note that the action taken by 
    */


    /* get the appropriate timeout for reading the reply to the next exposure
     * command. This depends on the expoure mode, the exposure time, and the
     * choice for wait_flag
    */
    if(verbose1){
	fprintf(stderr,"getting timeout for exp_mode [%s], expt [%7.3f], wait_flag [%d]\n",
		exp_mode,expt,wait_flag);
    }

    timeout = expose_timeout(exp_mode, expt, wait_flag);

    if(verbose1){
        fprintf(stderr,"take_exposure: timeout will be %d sec\n",timeout);
    }

    if (strlen(EXPOSE_COMMAND) + strlen(shutter_state) + 9 + strlen(filename) + strlen(exp_mode) < STR_BUF_LEN){
        sprintf(command,"%s %s %9.3f %s %s",EXPOSE_COMMAND,shutter_state,expt,filename,exp_mode);
    }
    else{
         fprintf(stderr,"take_exposure: command length too long : [%s %s %9.3f %s %s]\n",
			 EXPOSE_COMMAND,shutter_state,expt,filename,exp_mode);
         fflush(stderr);
	 readout_pending = False;
	 return -1;
    }


    if(verbose1){
        fprintf(stderr,"take_exposure: %s sending command %s\n",date_string,command);
    }

    if (! wait_flag){
       if(verbose1){
         fprintf(stderr,"take_exposure: using a thread to execute the exposure command");
         fflush(stderr);
       }

       pthread_t thread_id;
       Do_Command_Args *command_args;

       command_args = (Do_Command_Args *)malloc(sizeof(Do_Command_Args));
       command_args->command = command;
       command_args->reply = reply;
       command_args->timeout = timeout;
       command_args->start_semaphore = &command_start_semaphore;
       command_args->done_semaphore = &command_done_semaphore;
       command_args->command_id = command_id++;

       if(pthread_create(&thread_id,NULL,do_camera_command_thread,\
		       (void *)command_args)!=0){
         fprintf(stderr,"take_exposure: ERROR creating do_camera_command_thread\n");
         fflush(stderr);
	 readout_pending = False;
	 return -1;
       }

       if(verbose1){
           fprintf(stderr,"take_exposure: do_camera_command_thread running\n");
           fflush(stderr);
       }

       // post to command_start_semaphore. This tells the do_camera_command_thread to start
       // executing the exposre command
       if(verbose1){
	   t = get_ut();
           fprintf(stderr,"take_exposure: time %12.6f : posting to start_semaphore\n",t);
           fflush(stderr);
       }
       sem_post(&command_start_semaphore);

       // record time at start of exposure
       gettimeofday(&t_exp_start, NULL);

       if(verbose1){
           fprintf(stderr,"take_exposure: time %12.6f : waiting for exposure time to end\n",get_ut());
           fflush(stderr);
       }

       *actual_expt = wait_exp_done(expt);
       if (*actual_expt < 0){
          fprintf(stderr,"take_exposure: time %12.6f : error waiting for exposure to end\n",get_ut());
          fflush(stderr);
	  *actual_expt = 0;
	  return -1;
       }
       if(verbose1){
           fprintf(stderr,"take_exposure: time %12.6f : waited  %7.3f sec for exposure time to end\n",
                   get_ut(),*actual_expt);
           fflush(stderr);
       }
    }
    else{
       if(verbose1){
         fprintf(stderr,"take_exposure: executing do_camera_command with wait = True");
         fflush(stderr);
       }

       // record time at start of exposure
       gettimeofday(&t_exp_start, NULL);

       command_id++;
       if(do_camera_command(command,reply,timeout,command_id,host_name)!=0){
         fprintf(stderr,"take_exposure: error sending exposure command : %s\n",command);
         fprintf(stderr,"take_exposure: reply was : %s\n",reply);
         *actual_expt=0.0;
         readout_pending = False;
         return -1;
       }
       else{
         sscanf(reply,"%lf",actual_expt);
       }

       readout_pending = False;

       if(verbose){
         fprintf(stderr,
           "take_exposure: exposure complete : filename %s  ut: %9.6f\n",
           filename,*ut);
         fflush(stderr);
       }
    }

    strncpy(name,filename,FILENAME_LENGTH);
    
    return(0);
}

/*****************************************************/
/* return total time to take an exposure. If the exposure occurs */

double expose_timeout (char *exp_mode, double exp_time, bool wait_flag)
{
   double t=0;

   /* if taking exposure in parallel thread, only need to wait for the
    * exposure and readout to occur
   */

   if ( ! wait_flag){
      t = exp_time + READOUT_TIME_SEC;
   }
   else{
     if (strstr(exp_mode,EXP_MODE_SINGLE)!=NULL){

     /* wait for the exposure, the readout, and the transfer */
	 t = exp_time + READOUT_TIME_SEC + TRANSFER_TIME_SEC;
     }
     /* wait for the exposure and readout only, the transfer 
      * will be executed later
     */
     else if (strstr(exp_mode,EXP_MODE_FIRST)!=NULL){
	 t = exp_time + READOUT_TIME_SEC;
     }
     /* transfer of previous image occurs in parallel with new exposure.
      *  Wait the longer of:
      * (a) the exposure and readout times of the next exposure, or
      * (b) the time to transfer the previous exposure.
     */
     else if (strstr(exp_mode,EXP_MODE_NEXT)!=NULL){
	 if (exp_time + READOUT_TIME_SEC > TRANSFER_TIME_SEC){
	   t = exp_time;
	 }
	 else{
	   t = TRANSFER_TIME_SEC;
	 }
     }

     /* no new exposure. Only wait for the transfer of the previous
      * exposure
     */
     else if (strstr(exp_mode,EXP_MODE_LAST)!=NULL){
	 t = TRANSFER_TIME_SEC;
     }

     else{
	 fprintf(stderr,"%s: ERROR: unrecognized exposure mode [%s]\n",
            "expose_timeout",exp_mode);
	 t = -1;
	 return (t);
     }
   }

   t = t + 5.0;
   return (t);
}

/*****************************************************/

int get_filename(char *filename,struct tm *tm,int shutter)
{
    char shutter_string[3];
    int result;
    char field_description[STR_BUF_LEN];

    result=get_shutter_string(shutter_string,shutter,field_description);
    if(result!=0){
        fprintf(stderr,"get_filename: bad shutter code: %d\n",shutter);
        fflush(stderr);
    }

    sprintf(filename,"%04d%02d%02d%02d%02d%02d%s",
            tm->tm_year,tm->tm_mon,tm->tm_mday,
	    tm->tm_hour,tm->tm_min,tm->tm_sec,shutter_string);

    return(result);
}

/*****************************************************/

/* The camera controller updates the image fits header with info specific to the camera status. 
 * This command to add info the header maintained by the controller. This additional
 * info will be save to the fits header when the image is read out and saved by the controller
*/

int imprint_fits_header(Fits_Header *header)
{
    char command[MAXBUFSIZE];
    char reply[MAXBUFSIZE];
    int i;

    command_id++;
    for(i=0;i<header->num_words;i++){
      sprintf(command,"%s %s %s",
		HEADER_COMMAND,header->fits_word[i].keyword,
                header->fits_word[i].value);
      if(do_camera_command(command,reply,CAMERA_TIMEOUT_SEC,command_id,host_name)!=0){
        fprintf(stderr,
          "imprint_fits_header: error sending command %s\n",command);
        return(-1);
      }
    }

    return(0);
}
     
/*****************************************************/

/* wait for camera exposure to end while the command to take an
 * exposure occurs in a parallel thread (do_camera_command_thread). 
 * This routine is used by the "take_exposure" command when
 * the wait_flag argument is False. 
 *
 * The exposure state is determined by polling the camera status 
 * through a status socket distinct from the command socket.
 *
 * When the exposure has ended, it is then read out by
 * the controller. This may proceed in the parallel thread while
 * the main thread continues to perform other functions (such as
 * moving the telescope to the next position).
 *
 * The parallel thread posts to the done_semaphore just before it exits,
 * signifying the exposure command has completed.
*/
 
double wait_exp_done(int expt)
{
    struct timeval t_val;
    int t,t_start,t_end,timeout_sec;
    double act_expt;

    act_expt=0;
    timeout_sec = expt + 5;

    if(verbose1){
         fprintf(stderr,
	     "wait_exp_done: time %12.6f : waiting up to %d sec for camera exposure to end\n",
	     get_ut(),timeout_sec);
         fflush(stderr);
    }

    sleep(expt);

    /* if the status channel is active, query the camera status to determine whenb
     * the exposure ends. Otherwise, just assume it has ended when the expected 
     * exposure time has been waited */

    if (status_channel_active){

	gettimeofday(&t_val,NULL);
	int t_start = t_val.tv_sec;
	int t_end = t_start + timeout_sec;

	t=t_start;
	int done = False;
	while (t < t_end && ! done){
	  if(update_camera_status(NULL)!=0){
	    fprintf(stderr,"wait_camera_readout: could not update camera status\n");
	    done = True;
	  }
	  else if (cam_status.state_val[EXPOSING] == ALL_NEGATIVE_VAL){
	     done = True;
	  }
	  else{
	    fprintf(stderr,"cam_status.state_val[EXPOSING] = %d\n",cam_status.state_val[EXPOSING]);
	    usleep(100000);
	    gettimeofday(&t_val,NULL);
	    t = t_val.tv_sec;
	  }
	}

	if (! done ){
	   if (t < t_end){
	     fprintf(stderr,"wait_exp_done: time %12.6f : ERROR waiting for exposure to end\n",get_ut());
	   }
	   else{
	     fprintf(stderr,"wait_exp_done: time %12.6f : timeout waiting for exposure to end\n",get_ut());
	   }
	   fflush(stderr);
	}
	else{

	   if(verbose1){
	     fprintf(stderr,
	       "wait_camera_readout: exposure successfully ended in  %d sec\n",
		t - t_start);
	     fflush(stderr);
	   }
	}
	if( (!done) || cam_status.error){
	    fprintf(stderr,
		 "wait_exp_done: camera error\n");
	    print_camera_status(&cam_status,stderr);
	    fflush(stderr);
	    act_expt=-1.0;
	}
    }
    else{
        act_expt = expt;
    }

    return act_expt;

}

     
/*****************************************************/
#if 0
// not used for LS4 camera
int init_camera()
{
     char reply[MAXBUFSIZE];
     int result = 0;

     if(verbose){
       fprintf(stderr,"initializing camera\n");
       fflush(stderr);
     }

     if(do_camera_command(INIT_COMMAND,reply,CAMERA_TIMEOUT_SEC,host_name)!=0){
       fprintf(stderr,"error sending camera init command\n");
       result = -1;
     }
     else if (update_camera_status(NULL) != 0){
       fprintf(stderr,"error updating camera status\n");
       result = -1;
     }
     else if (cam_status.error){
       fprintf(stderr,"camera is in error state\n");
       result = -1;
     }
     else if(verbose){
       fprintf(stderr,"success initializing camera\n");
     }

     fflush(stderr);
     return (result);

}
#endif
/*****************************************************/

int update_camera_status(Camera_Status *status)
{
     char reply[MAXBUFSIZE];
     int error_flag;

     error_flag=0;

     command_id++;
     if(do_status_command(STATUS_COMMAND,reply,CAMERA_TIMEOUT_SEC,command_id,host_name)!=0){
       return(-1);
     }
     else {
       parse_status(reply,&cam_status);
       if (status != NULL) *status = cam_status;
     }

     return(0);

}
/*****************************************************/
// thread wrapper
//
void *do_camera_command_thread(void *args){

     int *result = malloc(sizeof(int));
     Do_Command_Args *command_args;
     int timeout_sec;
     char *command;
     char *reply;
     sem_t *done_semaphore;
     sem_t *start_semaphore;
     double t_start, t_end;
     int id;

     command_args = (Do_Command_Args *)args;
     command = command_args->command;
     reply = command_args->reply;
     timeout_sec =  command_args->timeout;
     start_semaphore = command_args->start_semaphore;
     done_semaphore = command_args->done_semaphore;
     id = command_args->command_id;


     *result = 0;


     struct timespec t;
     t.tv_sec = timeout_sec;
     t.tv_nsec = 0;

     if(verbose1){
          fprintf(stderr,"do_camera_command_thread[%d]: time %12.6f : waiting for start_semaphore to post with timeout %ld sec\n",
			  id,get_ut(),t.tv_sec);
          fflush(stderr);
     }

     /* wait for the start_semaphore to post, or else timeout */
     if ( sem_timedwait(start_semaphore,&t) != 0){
        fprintf(stderr,"do_camera_command_thread[%d]: time %12.6f : error waiting for start_semaphore to post\n",id,get_ut());
	perror("timeout waiting for start_semaphore to post in do_camera_command");
        fflush(stderr);
        *result = -1;
     }
     /* once the start semaphore posts, execute the command and then post to the
      * done semaphore */
     else{
	t_start = get_ut();
        if(verbose1){
          fprintf(stderr,"do_camera_command_thread[%d]: start time %12.6f : sending command [%s] with timeout %d sec\n",
			  id,t_start,command,timeout_sec);
          fflush(stderr);
        }
        *result = do_camera_command(command,reply, timeout_sec,id,host_name);
	t_end = get_ut();
        if(verbose1){
          fprintf(stderr,"do_camera_command_thread[%d]: done time %12.6f : result is [%d] \n",id,t_end,*result);
	  fflush(stderr);
	}
        if(verbose1){
          fprintf(stderr,"do_camera_command_thread[%d]: time %12.6f : posting to done_semaphore\n", id,t_end);
	  fflush(stderr);
	}
	sem_post(done_semaphore);
     }

     pthread_exit(result);
}

int do_status_command(char *command, char *reply, int timeout_sec,int id, char *host){
    return do_command(command, reply, timeout_sec, STATUS_PORT,id, host);
}

int do_camera_command(char *command, char *reply, int timeout_sec, int id, char *host){
    return do_command(command, reply, timeout_sec, COMMAND_PORT,id, host);
}

int do_command(char *command, char *reply, int timeout_sec, int port,int id, char *host){

     int returnval = 0;

     if(verbose1){
          fprintf(stderr,"do_command[%d]: time %12.6f : sending command %s with timeout %d sec\n",
			  id,get_ut(),command,timeout_sec);
          fflush(stderr);
     }

     if(send_command(command,reply,host,port, timeout_sec)!=0){
         fprintf(stderr,
          "do_command[%d]: error sending command %s\n", id,command);
         returnval = -1;
         fflush(stderr);
     }

     usleep(COMMAND_DELAY_USEC);

     if (returnval == 0){
       //if(strstr(reply,ERROR_REPLY)!=NULL || strlen(reply) == 0 ){
       if( strstr(reply,DONE_REPLY)==NULL || strlen(reply) == 0  || strstr(reply,"ERROR_REPLY") != NULL){
         fprintf(stderr,
            "do_command[%d]: time %12.6f : command [%s] returns error: %s\n", 
             id,get_ut(), command,reply);
         fflush(stderr);
	 returnval = -1;
       }
       else {
         if(verbose1){
           fprintf(stderr,"do_command[%d]: time %12.6f : reply was %s\n",id,get_ut(),reply);
           fflush(stderr);
         }
       }
     }


     return(returnval);

}
/*****************************************************/

/* wait for camera readout to end.
 * This routine used by scheduler.c to decide when to begin a new observation.
 * As long as the readout is complete, the telescope may be moved to a new position.
 * A new exposure may also begin, depending on the exposure mode of the previous
 * observatopn.
 *
 * If a readout is not pending, wait the the command_done_semaphore
 * to post. This signifies the exposure and readout have completed.
 * Return with result = 0 if no timeout or other error occurs while
 * waiting for the readout. Otherwise return -1.
 *
 * If no readout is pending, just return with result = 0
*/

int wait_camera_readout(Camera_Status *status)
{
    struct timeval t1,t2;
    double t,t_start,t_end,dt;
    int result=0;
    int timeout_sec = READOUT_TIME_SEC;

    if (readout_pending){
      gettimeofday(&t1,NULL);
      if(verbose){
	fprintf(stderr,"wait_camera_readout: time %12.6f : waiting for readout to complete\n",get_ut());
	fflush(stderr);
      }
      /* wait for the done_semphore to post, or else timeout */

      t_start = get_ut();
      if(verbose1){
	fprintf(stderr,"wait_camera_readout: time = %12.6f : waiting for done_semaphore to post with timeout %d\n",t_start,timeout_sec);
	fflush(stderr);
      }
      //HERE
      /* If a timeout or error occurs while waiting for ther done_semaphore to
       * post, return -1.
      */
      t = t_start*3600.0;
      t_end = t + timeout_sec;
      bool done = False;
      while (t < t_end  && !done){
         if ( sem_trywait(&command_done_semaphore) != 0){
	    if(verbose1){
	      fprintf(stderr,"wait_camera_readout: time = %12.6f : still waiting for done_semaphore\n",
			      get_ut());
	    }
	    usleep(100000);
	    t = t + 0.1;
	 }
	 else{
	    done = True;
	    if(verbose1){
	      fprintf(stderr,"wait_camera_readout: time = %12.6f : done_semaphore has posted\n",
			      get_ut());
	    }
	 }   
      }
      t_end = get_ut();
      dt = (t_end - t_start) * 3600.0;
      if ( ! done ){
        fprintf(stderr,"wait_camera_readout: time = %12.6f: dt = %12.6f: error waiting for done_semaphore to post\n",t_end,dt);
        fflush(stderr);
        result = -1;
      }
      /* Otherwise if done_semaphore posts, the readout is is done.  Set readout_pending to
       * False and return 0
      */
      else{
	readout_pending = False;
        if(verbose1){
     	  fprintf(stderr,"wait_camera_readout: time = %12.6f : done_semaphore has posted\n",t_end);
     	  fprintf(stderr,"wait_camera_readout: time = %12.6f : waited %7.3f sec for readout to end\n",t_end,dt);
	  fflush(stderr);
        }
      }
    }
    else{
       if(verbose1){
	  fprintf(stderr,"wait_camera_readout: no exposure currently reading out\n");
	  fflush(stderr);
       }
    }
/*
    if(update_camera_status(NULL)!=0){
	result=-1;
	fprintf(stderr,"wait_camera_readout: could not update camera status\n");
	fflush(stderr);
    }
    else{
	*status  = cam_status;
    }
*/
    return(result);

}

/*****************************************************/

int clear_camera()
{
     char reply[MAXBUFSIZE];
     char command_string[STR_BUF_LEN];

     double timeout = CLEAR_TIME +  5;
     sprintf(command_string,"%s %d",CLEAR_COMMAND,CLEAR_TIME);

     command_id++;
     if(do_camera_command(command_string,reply,timeout,command_id,host_name)!=0){
       return(-1);
     }
     else {
       return(0);
     }

}

/*****************************************************/
