/* scheduler_signals.c

   install signal handlers:

   SIGTERM : stop telescope, close files, exit

   SIGUSR1: pause observations

   SIGUSR2: resume observations

*/

#include "scheduler.h"

#define SIGTERM 15
#define SIGUSR1 10
#define SIGUSR2 12

extern int verbose;
extern int pause_flag;

/*********************************************/

int install_signal_handlers()
{

    if(signal(SIGTERM,sigterm_handler)==SIG_ERR){
      fprintf(stderr,
          "/install_signal_handler: ERROR installing sigterm_handler\n");
      fflush(stderr);
      return(-1);
    }
  
    if(signal(SIGUSR1,sigusr1_handler)==SIG_ERR){
      fprintf(stderr,
          "install_signal_handler: ERROR installing sigusr1_handler\n");
      fflush(stderr);
      return(-1);
    }

    if(signal(SIGUSR2,sigusr2_handler)==SIG_ERR){
      fprintf(stderr,
          "install_signal_handler: ERROR installing sigusr2_handler\n");
      fflush(stderr);
      return(-1);
    }

    return(0);
}

/*********************************************/

void sigterm_handler()
{
   fflush(stdout);
   fflush(stderr);

   fprintf(stderr,"\nsigterm_handler: terminate signal received\n");
   fflush(stderr);

   do_exit(1);

}

/*********************************************/

/* sigusr1 is pause signal */
void sigusr1_handler()
{
   fflush(stdout);
   fflush(stderr);

   if(verbose){
     fprintf(stderr,"\nsigusr1_handler: pause signal received\n");
     fflush(stderr);
   }

   pause_flag=1;

   return;
}

/*********************************************/

/* sigusr2 is resume signal */

void sigusr2_handler()
{
   fflush(stdout);
   fflush(stderr);

   if(verbose){
     fprintf(stderr,"\nsigusr1_handler: resume signal received\n");
     fflush(stderr);
   }

   pause_flag=0;

   return;
}

/*********************************************/





