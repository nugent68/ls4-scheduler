/* scheduler_telescope.c

   routines to allow scheduler to open a socket connection
   to the telescope controller (questctl on quest17_local), send
   commands and read replies

*/

#include "scheduler.h"




#define TEL_COMMAND_PORT 3911  /*nightime */
/*#define TEL_COMMAND_PORT 3912 daytime */
#define DAYTIME_TEL_COMMAND_PORT 3912


#define COMMAND_WAIT_TIME 100000 /* useconds to wait between commands */

/* first word in reply from telescope controller */
#define TEL_ERROR_REPLY "error"
#define TEL_DONE_REPLY "ok"


/* Telescope Commands */

#define LST_COMMAND "lst"
#define OPENDOME_COMMAND "opendome"
#define CLOSEDOME_COMMAND "closedome"
#define GETFOCUS_COMMAND "getfocus"
#define SETFOCUS_COMMAND "setfocus"
#define SLAVEDOME_COMMAND "slavedome"
#define DOMESTATUS_COMMAND "domestatus"
#define STATUS_COMMAND "status"
#define WEATHER_COMMAND "weather"
#define FILTER_COMMAND "filter"
#define POINT_COMMAND "pointrd"
#define TRACK_COMMAND "track"
#define POSRD_COMMAND "posrd"
#define STOW_COMMAND "stow"
#define STOPMOUNT_COMMAND "stopmount"
#define STOP_COMMAND "stop"
#define SET_TRACKING_COMMAND "settracking"

/* Filters */
#define RG610_FILTER "RG610"
#define ZZIR_FILTER "zzir"
#define RIBU_FILTER "RIBU"
#define GZIR_FILTER "gzir"
#define CLEAR_FILTER "clear"

#define TELESCOPE_POINT_TIMEOUT_SEC 300 /* timeout for pointing telescope */
#define TELESCOPE_FOCUS_TIMEOUT_SEC 300 /* timeout for pointing telescope */
#define TELESCOPE_COMMAND_TIMEOUT 300/* timeout for all other commands */

#define FOCUS_SCRIPT "~/questops.dir/focus/bin/get_best_focus.csh"
#define FOCUS_OUTPUT_FILE "/tmp/best_focus.tmp"

#define TELESCOPE_OFFSETS_FILE "/home/observer/telescope_offsets.dat"
#define OFFSET_SCRIPT "/home/observer/palomar/scripts/get_telescope_offsets.csh"
#define RA_OFFSET_MIN -1.00 /* minimum RA pointing offset (deg ) */
#define RA_OFFSET_MAX  1.00 /* minimum RA pointing offset (deg ) */
#define DEC_OFFSET_MIN -1.00 /* minimum RA pointing offset (deg ) */
#define DEC_OFFSET_MAX  1.00 /* minimum RA pointing offset (deg ) */

extern int verbose;
extern int verbose1;
extern int stop_flag;
extern int stow_flag;
extern char *host_name;

/*****************************************************/

/* read in default telescope pointing offsets from TELECOPE_OFFSETS_FILE */

int init_telescope_offsets(Telescope_Status *status)
{
        char string[STR_BUF_LEN];
        double prev_ra_offset, prev_dec_offset, ra_offset, dec_offset;
        FILE *input;

        /* set offsets to 0 as default */

	status->ra_offset=0.0;
	status->dec_offset=0.0;

        if(verbose){
            fprintf(stderr,"init_telescope_offsets: Reading default offsets\n");
            fflush(stderr);
        }
#if USE_TELESCOPE_OFFSETS

        input=fopen(TELESCOPE_OFFSETS_FILE,"r");
        if(input==NULL){
            fprintf(stderr,"init_telescope_offsets: can't open file %s\n",
		TELESCOPE_OFFSETS_FILE);
            fflush(stderr);
	    return(-1);
        }

        if(fgets(string,STR_BUF_LEN,input)==NULL||
                sscanf(string,"%lf %lf",&prev_ra_offset,&prev_dec_offset)!=2){
	    fclose(input);
            fprintf(stderr,"init_telescope_offsets: can't read previous offsets\n");
            fflush(stderr);
	    return(-1);
        }
        fclose(input);
#else
        fprintf(stderr,"init_telescope_offsets: ignoring offsets file, assuming  0 offsets\n");
	fflush(stderr);
        prev_ra_offset = 0.0;
        prev_dec_offset = 0.0;
#endif

        if(verbose){
            fprintf(stderr,"init_telescope_offsets: previous offsets are %8.6f %8.6f\n",
		prev_ra_offset,prev_dec_offset);
        }

        if(prev_ra_offset<RA_OFFSET_MIN||prev_ra_offset>RA_OFFSET_MAX||
             prev_dec_offset<DEC_OFFSET_MIN||prev_dec_offset>DEC_OFFSET_MAX){
            fprintf(stderr,
               "init_telescope_offset: previous offsets out of range. ");
            return(-1);
        }

        /* set ra and dec offsets to previous values as new default */

	status->ra_offset=prev_ra_offset;
	status->dec_offset=prev_dec_offset;

        return(0);
}

/*****************************************************/

/* use system call to OFFSET_PROGRAM to determine offsets to telescope pointing
   Return -1 if the offsets cannot be determined after setting default offsets.
   Return 0 otherwise.
*/

int get_telescope_offsets(Field *f, Telescope_Status *status)
{
        char string[STR_BUF_LEN],command_string[STR_BUF_LEN];
        int i;
        double ra_offset, dec_offset;
        FILE *input;

        if(verbose){
            fprintf(stderr,"get_telescope_offsets: initialize offsets from stored values\n");
            fflush(stderr);
        }

        if(init_telescope_offsets(status)!=0){
            fprintf(stderr,"get_telescope_offsets: problem reading stored offsets. Using defaults\n");
            fflush(stderr);
        }

        if(verbose){
            fprintf(stderr,"get_telescope_offsets: initializing default offsets to %8.6f %8.6f\n",
		status->ra_offset,status->dec_offset);
            fflush(stderr);
        }


        /* form system command for offset script*/

        sprintf(command_string,"%s %s\n",OFFSET_SCRIPT,f->filename);

        if(verbose){
           fprintf(stderr,"get_telescope_offsets: %s\n",command_string);
           fflush(stderr);
        }

#if FAKE_RUN

#else

        /* run offset script. Output will be in TELESCOPE_OFFSETS_FILE */

        if(system(command_string)==-1){
             fprintf(stderr,"get_telescope_offsets: system command unsucessful\n");
             fprintf(stderr,"get_telescope_offsets: Assuming default offset values %8.6f %8.6f\n",
		status->ra_offset,status->dec_offset);
	     fflush(stderr);
	     return(-1);
        }
         
        if(verbose){
            fprintf(stderr,"get_telescope_offsets: Reading new offsets\n");
            fflush(stderr);
        }

        /* reopen TELESCOPE_OFFSETS_FILE. If OFFSET_SCRIPT was successful, the new
           ra and dec offsets will be there */

        input=fopen(TELESCOPE_OFFSETS_FILE,"r");
        if(input==NULL){
            fprintf(stderr,"get_telescope_offsets: can't open file %s\n",
		TELESCOPE_OFFSETS_FILE);
            fflush(stderr);
	    return(-1);
        }

        if(fgets(string,STR_BUF_LEN,input)==NULL||
                sscanf(string,"%lf %lf",&ra_offset,&dec_offset)!=2){
	    fclose(input);
            fprintf(stderr,"get_telescope_offsets: can't read new offsets\n");
            fflush(stderr);
	    return(-1);
        }
        fclose(input);
          
        if(verbose){
            fprintf(stderr,"get_telescope_offsets: new offsets are %8.6f %8.6f\n",
		ra_offset,dec_offset);
        }

        if(ra_offset<RA_OFFSET_MIN||ra_offset>RA_OFFSET_MAX||
             dec_offset<DEC_OFFSET_MIN||dec_offset>DEC_OFFSET_MAX){
            fprintf(stderr,
               "get_telescope_offset: new offsets out of range. Substituting default values %8.5f %8.5f\n",
			status->ra_offset,status->dec_offset);
	    return(-1);
        }
        else{
           status->ra_offset=ra_offset;
           status->dec_offset=dec_offset;
        }

#endif

        fprintf(stderr,
             "get_telescope_offset: setting telescope offsets to  %8.5f %8.5f\n",
			status->ra_offset,status->dec_offset);
        fflush(stderr);

        return(0);

}

/*****************************************************/

/* use system call to FOCUS_PROGRAM to determine best focus   
   Return -1 if the telescope won't focus.
   Return +1 if the images sequence is bad.
   Return 0 is the focus is successful.
*/

int focus_telescope(Field *f, Telescope_Status *status, double focus_default)
{
        char command_string[STR_BUF_LEN];
        int i;
        double median,focus;

        if(verbose){
            fprintf(stderr,"focus_telescope: Field %d, n_done %d\n",
		f->field_number,f->n_done);
            fflush(stderr);
        }

        /* form system command for focus script*/

        sprintf(command_string,"%s ",FOCUS_SCRIPT);
        for(i=0;i<f->n_done;i++){
           sprintf(command_string+strlen(command_string),"%s ",
		f->filename+(i*FILENAME_LENGTH));
        }
        sprintf(command_string+strlen(command_string),"\n");

        if(verbose){
           fprintf(stderr,"focus_telescope: %s\n",command_string);
           fflush(stderr);
        }

#if FAKE_RUN

        status->focus=focus_default;
#else

        /* run focus script. Output will be in FOCUS_OUTPUT_FILE */

        if(system(command_string)==-1){
             fprintf(stderr,"focus_telescope: system command unsucessfull\n");
	     fflush(stderr);
        }
         
        median=get_median_focus(FOCUS_OUTPUT_FILE);
        if(median<=0){
           fprintf(stderr,"focus_telescope: could not get focus\n");
           focus=focus_default;
        }
        else if (median<MIN_FOCUS||median>MAX_FOCUS){
           fprintf(stderr,"focus_telescope: media out of range: %8.5f\n",median);
           focus=focus_default;
        }
        else if (fabs(median-focus_default)>MAX_FOCUS_CHANGE){
           fprintf(stderr,"focus_telescope: unexpected change of focus: %8.5f\n",median);
           focus=focus_default;
        }
        else{
           focus=median;
           fprintf(stderr,"focus_telescope: best focus is %8.5f mm\n",focus);
        }


        fprintf(stderr,"focus_telescope: setting focus to %8.5f mm\n",focus);
        fflush(stderr);
        
        if(set_telescope_focus(focus)!=0){
           fprintf(stderr,"focus_telescope: could not set telescope focus\n");
           return(-1);
        }

        if(verbose){
           fprintf(stderr,"focus_telescope: updating telescope status\n");
           fflush(stderr);
        }

        if(update_telescope_status(status)!=0){
           fprintf(stderr,"focus_telescope: could not update telescope status\n");
           return(-1);
        }

#endif

        fprintf(stderr,"focus_telescope: telescope focus now set at %8.5f mm\n",
		status->focus);
	

        return(0);

}

/*****************************************************/

double get_median_focus(char *file)
{
    FILE *input;
    double focus[20],median,temp;
    int i,n,done;
    char string[STR_BUF_LEN],s[256];

    input=fopen(file,"r");
    if(input==NULL){
        fprintf(stderr,"get_median_focus: could not open file %s\n",
	    file);
        fflush(stderr);
        return(-1.0);
    }

    n=0;
    while(fgets(string,STR_BUF_LEN,input)!=NULL){
         if(strstr(string,"best focus:")!=NULL){
            sscanf(string,"%s %s %lf",s,s,focus+n);
	    if(verbose){
               fprintf(stderr,"get_median_focus: value %d is %8.5f\n",n+1,focus[n]);
	       fflush(stderr);
            }
            n++;
            if(n>=20){
               fprintf(stderr,"get_median_focus: too many focus values\n");
 	       return(-1);
            }
         }
    }

    if(n==0){
      fprintf(stderr,"get_median_focus: no focus values\n");
      fflush(stderr);
      return(-1.0);
    }

    if(n==1){
        median=focus[0];
        return(median);
    }

    if (n==2){
        median=(focus[0]+focus[1])/2.0;
        return(median);
    }

    /* bubble sort */

    done=0;
    while(!done){
      done=1;
      for(i=0;i<n-1;i++){
        if(focus[i]>focus[i+1]){
            temp=focus[i];
            focus[i]=focus[i+1];
            focus[i+1]=temp;
            done=0;
        }
      }
    }

    i = n/2;
    if( n==2*i){
       median=(focus[i-1]+focus[i])/2.0;
    }
    else{
      i++;
      median=focus[i-1];
    }

    return(median);
}
  
/*****************************************************/

int stow_telescope()
{
    char command[MAXBUFSIZE],reply[MAXBUFSIZE];

    if(verbose){
      fprintf(stderr,"stow_telescope: stowing telescope\n");
      fflush(stderr);
    }

    sprintf(command,STOW_COMMAND);

    if(do_telescope_command(command,reply,TELESCOPE_POINT_TIMEOUT_SEC,host_name)!=0){
        fprintf(stderr,"stow_telescope: stow error: reply : %s\n",reply);
        return(-1);
    }
    else {
        if(verbose){
            fprintf(stderr,"stow_telescope: stow command succesful\n");
        }
        stow_flag=1;
        stop_flag=1;
    }

    return(0);
}


/*****************************************************/

int set_telescope_focus(double focus)
{
    char command[MAXBUFSIZE],reply[MAXBUFSIZE];
    int i;
    double focus1;

    if(focus<MIN_FOCUS ||focus>MAX_FOCUS-MAX_FOCUS_DEVIATION){
        fprintf(stderr,"set_telescope_focus: focus out of range: %8.5f\n",focus);
        return(-1);
    }

    /* first get the current focus */

    if(get_telescope_focus(&focus1)!=0){
        fprintf(stderr,"set_telescope_focus: error reading resulting focus\n");
        return(-1);
    }
    else if (verbose){
        fprintf(stderr,"set_telescope_focus: current focus is %8.5f\n",focus1);
        fprintf(stderr,"set_telescope_focus: target focus is %8.5f\n",focus);
    }

    if(focus<focus1){

        focus1=focus1+MAX_FOCUS_DEVIATION;
        if(verbose){
            fprintf(stderr,
               "set_telescope_focus: advancing focus to %8.5f before a decrement\n",
	       focus1);
            fflush(stderr);
        }

        sprintf(command,"%s %9.5f",SETFOCUS_COMMAND,focus1);

        if(do_telescope_command(command,reply,TELESCOPE_FOCUS_TIMEOUT_SEC,host_name)!=0){
            fprintf(stderr,"set_telescope_focus: setfocus reply error: reply : %s\n",reply);
            return(-1);
        }
        else if(verbose){
            fprintf(stderr,"set_telescope_focus: setfocus succesful\n");
 
        }

        if(get_telescope_focus(&focus1)!=0){
            fprintf(stderr,"set_telescope_focus: error reading resulting focus\n");
            return(-1);
        }
        else if (verbose){
            fprintf(stderr,"set_telescope_focus: focus now %8.5f\n",focus1);
        }
    }


    /* now go to desired focus. Repeat NUM_FOCUS_ITERATIONS times */

   if(verbose){
            fprintf(stderr,"set_telescope_focus: now setting to target focus %8.5f\n",focus);
            fflush(stderr);
    }

    for(i=1;i<=NUM_FOCUS_ITERATIONS;i++){

        if(verbose&&i>1){
          fprintf(stderr,"set_telescope_focus: setting to target focus again\n");
          fflush(stderr);
        }

        sprintf(command,"%s %9.5f",SETFOCUS_COMMAND,focus);

        if(do_telescope_command(command,reply,TELESCOPE_FOCUS_TIMEOUT_SEC,host_name)!=0){
            fprintf(stderr,"set_telescope_focus: setfocus reply error: reply : %s\n",reply);
            return(-1);
        }
        else if(verbose){
            fprintf(stderr,"set_telescope_focus:  command succesful\n");
 
        }

        if(get_telescope_focus(&focus1)!=0){
            fprintf(stderr,"set_telescope_focus: error reading resulting focus\n");
            return(-1);
        }
        else if (verbose){
            fprintf(stderr,"set_telescope_focus: focus now %8.5f\n",focus1);
        }
    }

    if(fabs(focus1-focus)>MAX_FOCUS_DEVIATION){
        fprintf(stderr,"set_telescope_focus: unable to set focus to %8.5f. Current focus is %8.5f\n",
              focus,focus1);
        fflush(stderr);
        return(-1);
    }

    return(0);

}

/*****************************************************/

int get_telescope_focus(double *focus)
{
    char reply[MAXBUFSIZE],s[256];



     *focus= NOMINAL_FOCUS_DEFAULT;

     if(do_telescope_command(GETFOCUS_COMMAND,reply,TELESCOPE_COMMAND_TIMEOUT,host_name)!=0){
       fprintf(stderr,"get_telescope_focus: error getting focus\n");
       fflush(stderr);
       return(-1);
     }
     else if (strstr(reply, TEL_DONE_REPLY)!=NULL){
       sscanf(reply,"%s %lf",s,focus);
#if 0
       fprintf(stderr,"get_telescope_focus: focus is %8.5f\n",*focus);
       fflush(stderr);
#endif
       return(0);
     }
     else{
       fprintf(stderr,"get_telescope_focus: bad reply from telescope\n");
       fprintf(stderr,"get_telescope_focus: reply: %s\n",reply);
       return(-1);
    }

}

/*****************************************************/

int stop_telescope()
{
    char command[MAXBUFSIZE],reply[MAXBUFSIZE];

    if(verbose){
      fprintf(stderr,"stop_telescope: stopping telescope\n");
      fflush(stderr);
    }

    sprintf(command,STOP_COMMAND);

    if(do_telescope_command(command,reply,TELESCOPE_POINT_TIMEOUT_SEC,host_name)!=0){
        fprintf(stderr,"stop_telescope: stop error: reply : %s\n",reply);
        return(-1);
    }
    else{
        if(verbose){
            fprintf(stderr,"stop_telescope: stop command succesful\n");
        }
        stop_flag=1;
    }

    return(0);
}

/*****************************************************/


int point_telescope(double ra, double dec, double ra_rate, double dec_rate)
{
    char command[MAXBUFSIZE],reply[MAXBUFSIZE];


    if (ra>24.0)ra=ra-24.0;
    sprintf(command,"%s %9.6f %9.5f",TRACK_COMMAND,ra,dec);

    if(do_telescope_command(command,reply,TELESCOPE_POINT_TIMEOUT_SEC,host_name)!=0){
        fprintf(stderr,"point_telescope: pointing error: reply : %s\n",reply);
        return(-1);
    }
    else {
        if(verbose){
           fprintf(stderr,"point_telescope: pointing command succesful\n");
        }
        stop_flag=0;
    }

    if(ra_rate!=0.0||dec_rate!=0.0){
       sprintf(command,"%s %9.6f %9.6f",SET_TRACKING_COMMAND,ra_rate,dec_rate);
       if(do_telescope_command(command,reply,TELESCOPE_POINT_TIMEOUT_SEC,host_name)!=0){
           fprintf(stderr,"point_telescope: set_tracking error: reply : %s\n",reply);
           return(-1);
        }
        else {
           if(verbose){
               fprintf(stderr,"point_telescope: set_tracking command succesful\n");
           }
           stop_flag=0;
        }
    }

    return(0);
}

/*****************************************************/

int update_telescope_status(Telescope_Status *status)
{
     char reply[MAXBUFSIZE];
     char s[256],*s_ptr;


     status->ut=get_ut();

     if(do_telescope_command(DOMESTATUS_COMMAND,reply,TELESCOPE_COMMAND_TIMEOUT,host_name)!=0){
       fprintf(stderr,"update_telescope_status: error getting domestatus\n");
       fflush(stderr);
       return(-1);
     }
     else if (strstr(reply, TEL_DONE_REPLY)!=NULL){
       if(strstr(reply,"open")!=NULL){
         status->dome_status=1;
       }
       else{
         status->dome_status=0;
       }
     }


     if(do_telescope_command(LST_COMMAND,reply,TELESCOPE_COMMAND_TIMEOUT,host_name)!=0){
       fprintf(stderr,"update_telescope_status: error getting lst\n");
       fflush(stderr);
       return(-1);
     }
     else if (strstr(reply, TEL_DONE_REPLY)!=NULL){
       sscanf(reply,"%s %lf",s,&(status->lst));
     }
/* debug */
/*status->lst=status->lst + 3.0;*/


     if(get_telescope_focus(&(status->focus))!=0){
       fprintf(stderr,"update_telescope_status: error getting focus\n");
       fflush(stderr);
       return(-1);
     }

#if 0
     if(do_daytime_telescope_command(FILTER_COMMAND,reply,TELESCOPE_COMMAND_TIMEOUT,host)!=0){
       fprintf(stderr,"update_telescope_status: error getting filter\n");
       fflush(stderr);
       return(-1);
     }
     else if (strstr(reply, TEL_DONE_REPLY)!=NULL){
       sscanf(reply,"%s %s %s",s,s,status->filter_string);
     }
#endif
     sprintf(status->filter_string,"UNKNOWN");



     if(do_telescope_command(POSRD_COMMAND,reply,TELESCOPE_COMMAND_TIMEOUT,host_name)!=0){
       fprintf(stderr,"update_telescope_status: error getting position\n");
       fflush(stderr);
       return(-1);
     }
     else if (strstr(reply, TEL_DONE_REPLY)!=NULL){
       sscanf(reply,"%s %lf %lf",s,&(status->ra),&(status->dec));
     }


     if(do_telescope_command(WEATHER_COMMAND,reply,TELESCOPE_COMMAND_TIMEOUT,host_name)!=0){
       fprintf(stderr,"update_telescope_status: error getting weather\n");
       fflush(stderr);
       return(-1);
     }
     else if (strstr(reply, TEL_DONE_REPLY)!=NULL){

       s_ptr=reply;
       s_ptr=strstr(s_ptr,":");
       if(s_ptr!=NULL)
          sscanf(s_ptr+1,"%lf",&((status->weather).temperature));
       s_ptr++;

       s_ptr=strstr(s_ptr,":");
       if(s_ptr!=NULL)
          sscanf(s_ptr+1,"%lf",&((status->weather).humidity));
       s_ptr++;

       s_ptr=strstr(s_ptr,":");
       if(s_ptr!=NULL)
          sscanf(s_ptr+1,"%lf",&((status->weather).wind_speed));
       s_ptr++;

       s_ptr=strstr(s_ptr,":");
       if(s_ptr!=NULL)
          sscanf(s_ptr+1,"%lf",&((status->weather).wind_direction));
       s_ptr++;

       s_ptr=strstr(s_ptr,":");
       if(s_ptr!=NULL)
          sscanf(s_ptr+1,"%lf",&((status->weather).dew_point));
       s_ptr++;
#if 0
       s_ptr=strstr(s_ptr,":");
       if(s_ptr!=NULL)
          sscanf(s_ptr+1,"%lf",&((status->weather).dome_states));
#endif

     }

     return(0);

}
/*****************************************************/

int print_telescope_status(Telescope_Status *status,FILE *output)
{
   Weather_Info *w;
   w=&(status->weather);

   fprintf(output,"UT    : %10.6f  ",status->ut);  
   fprintf(output,"LST   : %10.6f  ",status->lst);  
   fprintf(output,"RA    : %10.6f  ",status->ra);  
   fprintf(output,"Dec   : %10.6f  ",status->dec); 
   if(status->dome_status==1){
       fprintf(output,"dome  : open  "); 
   }
   else{
       fprintf(output,"dome  : closed  "); 
   }
   fprintf(output,"Focus : %7.3f  ",status->focus); 
   fprintf(output,"Filter: %s  ",status->filter_string); 
   fprintf(output,"Temp  : %5.1f  ",w->temperature); 
   fprintf(output,"Humid : %5.1f  ",w->humidity); 
/*   fprintf(output,"Dew Pt: %5.1f\n",w->dew_point); */
   fprintf(output,"Wnd Sp: %5.1f  ",w->wind_speed); 
   fprintf(output,"Wnd Dr: %5.1f\n",w->wind_direction); 

}

/*****************************************************/

int do_telescope_command(char *command, char *reply, int timeout, char *host)
{

     if(verbose1){
        fprintf(stderr,"do_telescope_command: sending command: %s\n",command);
        fflush(stderr);
     }

     if(send_command(command,reply,host,TEL_COMMAND_PORT,timeout)!=0){
       fprintf(stderr,
          "do_telescope_command: error sendind command %s\n", command);
       return(-1);
        fflush(stderr);
     }
     if(COMMAND_WAIT_TIME>0)usleep(COMMAND_WAIT_TIME);

     if(strstr(reply,TEL_ERROR_REPLY)!=NULL||strlen(reply)==0){
       fprintf(stderr,
          "do_telescope_command: error reading domestatus : %s\n", 
           reply);
       return(-1);
     }
     else if (strstr(reply, TEL_DONE_REPLY)!=NULL){

        if(verbose1){
            fprintf(stderr,"do_telescope_command: reply was %s\n",reply);
            fflush(stderr);
        }

       return(0);
     }
     else{
       fprintf(stderr,
         "do_telescope_command: bad response from telescope : %s\n",
         reply);
       return(-1);
     }
}

/*****************************************************/

/* use this function in daytime when telescope controller is off or when
   dome has not yet opened */

int do_daytime_telescope_command(char *command, char *reply, int timeout, char *host)
{

     if(verbose1){
        fprintf(stderr,"do_telescope_command: sending command: %s\n",command);
        fflush(stderr);
     }

     if(send_command(command,reply,host,DAYTIME_TEL_COMMAND_PORT,timeout)!=0){
       fprintf(stderr,
          "do_telescope_command: error sendind command %s\n", command);
       return(-1);
        fflush(stderr);
     }
     if(COMMAND_WAIT_TIME>0)usleep(COMMAND_WAIT_TIME);

     if(strstr(reply,TEL_ERROR_REPLY)!=NULL||strlen(reply)==0){
       fprintf(stderr,
          "do_telescope_command: error reading domestatus : %s\n", 
           reply);
       return(-1);
     }
     else if (strstr(reply, TEL_DONE_REPLY)!=NULL){

        if(verbose1){
            fprintf(stderr,"do_telescope_command: reply was %s\n",reply);
            fflush(stderr);
        }

       return(0);
     }
     else{
       fprintf(stderr,
         "do_telescope_command: bad response from telescope : %s\n",
         reply);
       return(-1);
     }
}

/*****************************************************/

double get_ut() {

  time_t t;
  struct tm *tm;
  double ut;

  time(&t);
  tm = gmtime(&t);

  /* get ut time in fractional hour for current day */
  ut = tm->tm_hour + tm->tm_min/60. + tm->tm_sec/3600.;

/*debug*/

  if(UT_OFFSET!=0.0){
     ut=ut+UT_OFFSET;
     if(ut>24.0)ut=ut-24.0;
  }

  return(ut);
}

/*****************************************************/

/* return tm structure with year, month, day, hour,
   minute, second filled out. Also return UT in hours */

double get_tm(struct tm *tm_out) {

  time_t t;
  struct tm *tm;
  double ut;

  time(&t);
  tm = gmtime(&t);


  /* use convention Jan is month 1, not 0 */
  tm->tm_mon=tm->tm_mon+1; 

  /* change from year 0 = 1900 to year 1900 = 1900 */
  tm->tm_year=tm->tm_year+1900;

  /* get ut time in fractional hour for current day */
  ut = tm->tm_hour + tm->tm_min/60. + tm->tm_sec/3600.;

/*debug*/

  if(UT_OFFSET!=0.0){

     ut=ut+UT_OFFSET;
     if(ut>24.0){
         ut=ut-24.0;
         advance_tm_day(tm);
     }   
     tm->tm_hour=ut;
     tm->tm_min=(ut-tm->tm_hour)*60.0;
     tm->tm_sec=(ut - tm->tm_hour - (tm->tm_min/60.0) )*3600.0;
  }


  if(tm_out!=NULL)*tm_out=*tm;

  return(ut);
}

/*****************************************************/

int advance_tm_day(struct tm *tm)
{
    tm->tm_mday=tm->tm_mday+1;

    if(tm->tm_mday==29 && tm->tm_mon==2 && !leap_year_check(tm->tm_year)){ 
       tm->tm_mon=3;
       tm->tm_mday=1;
    }
    else if(tm->tm_mday==30 && tm->tm_mon==2 ){ 
       tm->tm_mon=3;
       tm->tm_mday=1;
    }
    else if (tm->tm_mday==31 && (tm->tm_mon == 4 || 
        tm->tm_mon ==6 || tm->tm_mon == 9 || tm->tm_mon==11 ) ) {
        tm->tm_mon=tm->tm_mon+1;
        tm->tm_mday=1;
    }
    else if (tm->tm_mday==32) {
        tm->tm_mon=tm->tm_mon+1;
        tm->tm_mday=1;
        if(tm->tm_mon==13){
          tm->tm_mon=1;
          tm->tm_year=tm->tm_year+1;
        }
    }
    return(0);
}

/*****************************************************/

int leap_year_check(int year)
{
   int n;
   

   n=year/4;
   n=4*n;
   if ( n != year ) return(0);

   n = year/100;
   n = n * 100;
   if ( n != year ) return(1);

   n =  year/400;
   n = n*400;

   if( n == year) return(1);

   return(0);
}

/*****************************************************/

double get_jd()
{
    struct tm tm;
    struct date_time date;
    double jd;

    get_tm(&tm);

    date.y=tm.tm_year;
    date.mo=tm.tm_mon;
    date.d=tm.tm_mday;
    date.h=tm.tm_hour;
    date.mn=tm.tm_min;
    date.s=tm.tm_sec;

    jd=date_to_jd(date);

    return(jd);
}
/*****************************************************/

