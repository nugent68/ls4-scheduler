/* survey_sim.c

   DLR 2007 Feb 7

   Read in a squence observations (NEAT format). Simulate the
   timing of the obervations, given the position of the sun,
   moon, and random weather.

   Print out time, hour angle, airmass of requested observstions

   Use "sky_utils", adapted from "skycalc" to determine sun and moon
   position, airmass, etc.

   syntax: survey_sim sequence_file yyyy mm dd verbose_flag


   NEAT sequence format:
#
# ra        dec    Shutter Expt Interval Nobs 
#
 0.000000  0.000000 N   60.00 9600.0 15   # darks
 3.424352  14.300000 Y  60.00  1800.0 3
 8.35036  13.62647 Y  60.00 1800.0 3    # QJ126C4 02/05 0.51 fu field
...


   Selection strategy (see get_next_scan):

   At each time step, keep track of which fields are up in the sky and
   will remain up long enough to complete the required number of repeat
   observations.

   Of these, consider only those fields that are ready to be observed
   (i.e. have not yet been observed, or are ready to be observed again).
   
   Find the field or fields that have the fewest number of repeats remaining
   before completion. 

   Choose the field with the least repeats remaining and the least time
   remaining to complete those repeats.

   If there is a dark waiting to be repeated, choose this before any other
   field.
    

*/

#include <stdio.h>
#include <math.h>
#include <string.h>
#include "sky_utils.h"

#define DEBUG 0
#define HISTORY_FILE "survey.hist"

#define DEGTORAD 57.29577951 /* 180/pi */
#define LST_SEARCH_INCREMENT 0.0166 /* 1 minute in hours */
#define LST_WAIT_INCREMENT 0.0833 /* 5 minutes in hours */

#define MAX_AIRMASS 2.0
#define MAX_OBS_PER_FIELD 24 /* maximum number of observations per field */
#define MAX_FIELDS 1000 /* maximum number of fields per script */
#define OBSERVATORY_SITE "Palomar"
#define MAX_EXPT 1000.0
#define MAX_INTERVAL 43200.0
#define MIN_INTERVAL 900.0

#define USE_12DEG_START 1 /* 1 to use 12-deg twilight, 0 to use 18-deg twilight */
#define STARTUP_TIME 0.5 /* hours to startup after end of twilight */
#define MIN_EXECUTION_TIME 0.083 /* minimum time (hours) to make an observation */
#if 0
#define EXPOSURE_OVERHEAD 0.0111 /* time to readout exposure (hours) = 40 sec */
#else
#define EXPOSURE_OVERHEAD 0.0125 /* time to readout exposure (hours) = 45 sec */
#endif


typedef struct {
    int ready; /* 1 if ready to be observed, 0 if not */
    int doable; /* 1 if all required observations possible, 0 if not */
    int field_number;
    int line_number;
    char script_line[1024];    
    double ra; /* hours */
    double dec; /*deg */
    int shutter; /* open (Y) or closed (N) */
    double expt; /* exposure time (hours) */
    double interval; /* interval between observations ( hours) */
    int n_required; /* requested number of observations (normally 3) */
    double lst_rise; /* lst (hours) when field rises above airmass threshold */
    double lst_set;  /* lst (hours) when field sets below airmass threshold */
    int n_done; /* number of observations completed */
    double lst_next; /* lst (hours) for next observation */
    double time_up; /* total time (hours) object will be up (if not yet risen) 
                       or else the remaining time up  (already risen)*/
    double time_required; /* total time (hours) required to complete 
                             remaining observations */
    double time_left; /* time remaining (hours) before time_required 
                         exceeds time_up -- i.e. time_up-time_required */
    double lst[MAX_OBS_PER_FIELD]; /* lst (hours) of completed obs */
    double ha[MAX_OBS_PER_FIELD]; /* hour angle (hours) of completed obs  */
    double am[MAX_OBS_PER_FIELD]; /* airmass of completed obs  */
} Field;

/* globals */

/*  site-specific parameters  */

typedef struct {

    double jdb, jde;       /* jd of begin and end of dst */
    char site_name[45];    /* initialized later with strcpy for portability */
    char zabr;          /* single-character abbreviation of time zone */
    char zone_name[25];    /* name of time zone, e. g. Eastern */
    short use_dst;         /* 1 for USA DST, 2 for Spanish, negative for south,
                              0 to use standard time year round */
    double longit;         /* W longitude in decimal hours */
    double lat;            /* N latitude in decimal degrees */
    double elevsea;        /* for MDM, strictly */
    double elev;           /* well, sorta -- height above horizon */
    double horiz;
    double stdz;           /* standard time zone offset, hours */

} Site_Params;

int verbose = 1;

int load_sequence(char *script_name, Field *sequence);

int observe_next_field(Field *f, double lst, double *time_taken);

int init_fields(Field *sequence, int num_fields, 
                Night_Times *nt,Site_Params *site, double lst);

double get_lst_rise_time(double ra,double dec, double max_am, 
       Night_Times *nt, Site_Params *site, double *am);

double get_lst_set_time(double ra,double dec, double max_am, 
       Night_Times *nt, Site_Params *site, double *am);

double get_airmass(double ha, double dec, Site_Params *site);

double get_ha(double ra, double lst);

double clock_difference(double h1,double h2);

int get_next_field(Field *sequence,int num_fields, double lst);

int update_field_status(Field *f, double lst);

int shorten_interval(Field *f);

int print_field_status (Field *f, FILE *output);

int print_history(double lst,Field *sequence, int num_fields,FILE *output);


extern double sin();

/************************************************************/

main(int argc, char **argv)
{
        struct date_time date;
	Night_Times nt; /* times of sunrise/set, moonrise/set, etc */
        char script_name[1024];
	Field sequence[MAX_FIELDS];
        int i,num_fields,num_observable_fields,num_completed_fields;
        Site_Params site;
        double lst,dt;
        FILE *hist_out;
        
        if(argc!=6){
          fprintf(stderr,"syntax: survey_sim sequence_file yyyy mm dd verbose_flag\n");
          exit(-1);
        }

        strcpy(script_name,argv[1]);
        sscanf(argv[2],"%d",&(date.y));
        sscanf(argv[3],"%d",&(date.mo));
        sscanf(argv[4],"%d",&(date.d));
        sscanf(argv[5],"%d",&verbose);

        hist_out=fopen(HISTORY_FILE,"w");
        if(hist_out==NULL){
            fprintf(stderr,"can't open file %s for output\n",HISTORY_FILE);
            exit(-1);
        }     

        /* initialize site parameters for DEFAULT observatory */

        strcpy(site.site_name,"DEFAULT"); /* DEFAULT assigned to Palomar */
        load_site(&site.longit,&site.lat,&site.stdz,&site.use_dst,site.zone_name,&site.zabr,
                        &site.elevsea,&site.elev,&site.horiz,site.site_name);

        if(verbose){
          fprintf(stderr,"# site: %s\n",site.site_name);
          fprintf(stderr,"# longitude: %7.3f\n",site.longit);
          fprintf(stderr,"# latitude: %7.3f\n",site.lat);
          fprintf(stderr,"# elevsea: %7.3f\n",site.elevsea);
          fprintf(stderr,"# elev: %7.3f\n",site.elev);
          fprintf(stderr,"# horiz: %7.3f\n",site.horiz);
        }

        /* initialize night_times */
        print_tonight(date,site.lat,site.longit,site.elevsea,site.elev,site.horiz,
                      site.site_name,site.stdz,site.zone_name,site.zabr,site.use_dst,
                      &(site.jdb),&(site.jde),2,&nt);
        fprintf(stdout,"#\n");


        if (USE_12DEG_START){
          nt.ut_start=nt.ut_evening12+STARTUP_TIME;
          nt.ut_end=nt.ut_morning12-MIN_EXECUTION_TIME;
          nt.lst_start=nt.lst_evening12+STARTUP_TIME;
          nt.lst_end=nt.lst_morning12-MIN_EXECUTION_TIME;
       }
        else {
          nt.ut_start=nt.ut_evening18+STARTUP_TIME;
          nt.ut_end=nt.ut_morning18-MIN_EXECUTION_TIME;
          nt.lst_start=nt.lst_evening18+STARTUP_TIME;
          nt.lst_end=nt.lst_morning18-MIN_EXECUTION_TIME;
        }

        if(nt.ut_start>24.0)nt.ut_start=nt.ut_start-24.0;
        if(nt.lst_start>24.0)nt.lst_start=nt.lst_start-24.0;

        if(nt.ut_end<0.0)nt.ut_start=nt.ut_start+24.0;
        if(nt.lst_end<0.0)nt.lst_start=nt.lst_start+24.0;

        if(verbose){
          fprintf(stderr,"# loading sequence file %s\n",script_name);
	  fflush(stdout);
        }

        num_fields=load_sequence(script_name,sequence);
        if (num_fields<1){
            fprintf(stderr,"Error loading script %s\n",script_name);
             exit(-1);
        }
 
        if(verbose){
          fprintf(stderr,"# %d fields successfully loaded\n",num_fields);
	  fflush(stdout);
        }

        if(verbose){
          fprintf(stderr,"# initialing sequence fields \n");
	  fflush(stdout);
        }

        lst=nt.lst_start;

        num_observable_fields=init_fields(sequence,num_fields,
                              &nt,&site,lst);

        if(verbose){
          fprintf(stderr,"# %d observable field \n",num_observable_fields);
	  fflush(stdout);
        }


        fprintf(stderr,"lst: %7.3f Starting observations\n",lst);

        /* keep choosing next field and incrementing lst accordingly
           until end of night */

        while(clock_difference(lst,nt.lst_end)>0){

             /* choose next field to observe */

             i=get_next_field(sequence,num_fields,lst);

             /* if i < 0, no fields to observe */

             if(i<0){
                if(verbose){
                   fprintf(stderr,"lst : %7.3f  No fields to observe\n",lst);
                }
                lst=lst+LST_WAIT_INCREMENT;
             }

             /* otherwise, observe field i */

             else{
                observe_next_field(sequence+i,lst,&dt);
                print_history(lst,sequence,num_fields,hist_out);
                lst=lst+dt;
             }

             if(lst>24.0)lst=lst-24.0;
             
        }

       fprintf(stderr,"lst: %7.3f Ending observations\n",lst);

        num_completed_fields=0;
        for(i=0;i<num_fields;i++){
           if(sequence[i].n_done==sequence[i].n_required)num_completed_fields++;
           print_field_status(sequence+i,stderr);
        }

        fprintf(stderr, "%d fields loaded  %d fields observable  %d field completed\n",
		num_fields, num_observable_fields, num_completed_fields);

       fclose(hist_out);

        exit(0);
}

/************************************************************/

/* choose next field to observe at time lst. If there are no fields,
   return with dt = 0 and i < 0. If there is a field, update the 
   observation count, set the next time to observe the field, 
   set dt = exposure time plus overhead, and return
   with the index of the chosen field. */

int observe_next_field(Field *f,double lst, double *dt)
{
 
    /* update n_done, lst_next, and compute dt */

    f->lst[f->n_done]=lst;
    f->n_done=f->n_done+1;
    f->lst_next=lst+f->interval;
    if (f->lst_next>24.0)f->lst_next=f->lst_next-24.0;

    *dt=f->expt+EXPOSURE_OVERHEAD;
 
    fprintf(stderr,
    "lst : %7.3f Exposing field : %d obs %d out of %d time_left : %7.3f lst_next : %7.3f\n",
         lst, f->field_number, f->n_done, f->n_required, f->time_left, f->lst_next);

    return(0);
}


/************************************************************/

/* This is a two pass selection loop. In the first pass, consider 
   each field. Call update_field_status to determine if 
   field is observable, ready to be observed,  and how much time 
   is left to observe it. If it is a dark, return with that fields
   index. Otherwise, if it is observable and ready to observe,
   set its ready flag to 1 and update the minimum value of
   n_left (number of fields remaining for completion).

   In the second pass, find all the fields that are ready to observe
   (ready flag set) and have n_left matching the minimum value
   determined in the first pass. Of these, select the field
   that has the least time remaining (time_left) to complete the
   required observations.

*/

int get_next_field(Field *sequence,int num_fields, double lst)
{
     Field *f;
     double time_left_min,time_left,time_left_max;
     int i,n_left,n_left_min;
     int i_min,i_max,status,n_ready;
    

     n_left_min=1000;
     n_ready=0;

     for(i=0;i<num_fields;i++){
         f=sequence+i;
         f->ready=0;
         
         status=update_field_status(f,lst);

         /* status of 2 means must do now (i.e. darks).
            Return field index */

         if(status==2){
            f->ready=1;
            return(i);
         }

         /* Otherwise, set ready flag and update minimum
            value of n_left */

         else if (status>0){
            f->ready=1;
	    n_ready++;
            n_left=f->n_required-f->n_done;
            
            if(n_left<n_left_min){
                 n_left_min=n_left;
            }
         }
     }
 
     if(n_ready>0){
       time_left_min=10000.0;
       i_min=-1;

       for(i=0;i<num_fields;i++){
         f=sequence+i;
         if(f->ready){
            n_left=f->n_required-f->n_done;
            if(n_left==n_left_min&&f->time_left<time_left_min){
                 i_min=i;
                 time_left_min=f->time_left;
            }
         }
       }

       return(i_min);

     }

     /* no observable fields with time left. Choose the observable field
        with most time left. Shorten the interval so that time_left=0.
        If still doable, choose this field. Otherwise return -1 */

     else{

        i_max=-1;
        time_left_max=-1000;
        for(i=0;i<=num_fields;i++){
          f=sequence+i;
          if(f->doable&&f->time_left>time_left_max){
                time_left_max=f->time_left;
                i_max=i;
          }
        }

        shorten_interval(f+i_max);
        if(f->doable){
           return(i_max);
        }
        else{
           return(-1);
        } 
     }

}

/*******************************************************/

/* shorten interval between exposures so that time_left=0. If new 
   interval is less than MIN_INTERVAL, set doable to 0 */

int shorten_interval(Field *f)
{
        double new_interval,new_time_required;

        new_time_required=f->time_up;
        new_interval = new_time_required/(f->n_required-f->n_done);

        if(new_interval>MIN_INTERVAL){
           f->time_required=new_time_required;
           f->interval=new_interval;
           f->time_left=0;
        }
        else{
           f->doable=0.0;
        }

        return(0);
}

/************************************************************/

/* If field is up and ready to be observed, ppdate time_up, time_remaining
   and time_left and return 1. If not doable, return 0. If the field has
   been completed or has set, set doable to 0 and return 0.*/

int update_field_status(Field *f, double lst)
{

      /* Isn't doable, Just return 0*/
      if(f->doable==0){
          return(0);
      }

      /* Has been completed. Set doable to 0 and return 0*/

      else if(f->n_done==f->n_required){
         f->doable=0; 
         return(0);
      }
        
      /* hasn't risen yet. Just return 0 */

      else if(clock_difference(lst,f->lst_rise)>0){
         return(0);
      }

      /* has already set, Set doable to 0 and return 0 */

      else if(clock_difference(lst,f->lst_set)<0){
         f->doable=0;
         return(0);
      }

      /* not yet time to reobserve. Return 0 */

      else if (clock_difference(lst,f->lst_next)>0){
         return(0);
      }

      /* dark, and it is time to observe it. Return 2
         to indicate must do immediately */

      else if (f->shutter==0 ){
        return(2);
      }

      /* Update time_required, time_up , time left.
         If time_left<0, keep doable = 1 but return -1.
         Otherwise, this is field is observable. Return 1 */

      else{

        f->time_required=(f->n_required-f->n_done)*f->interval;

        /* time up is now to when the field sets */

        f->time_up=f->lst_set-lst; 
        if(f->time_up<0)f->time_up=f->time_up+24.0;

        /* time left is time up - time required to finish the
           observations.  */

        f->time_left=f->time_up-f->time_required; 
        
        if(f->time_left<0){
#if 0
              f->doable=0;
              return(0);
#else
              return(-1);
#endif
        }
        else{
            return(1);
        }
      }
}

/************************************************************/

/* initialize rise and set times for each field. Set 0 values for time,
hour angle, and airmass of each oservation for each field. Set number
done to 0. Determine which observations are doable, and initialize
time_up, time_required, time_left, and lst_next */

int init_fields(Field *sequence, int num_fields, 
                Night_Times *nt, Site_Params *site, double lst)
{
    int i,j,n_observable;
    Field *f;
    double am,time_up,time_required,night_duration;

    
    /* if initializing fields after lst_start, set lst_start to
       current lst */

    if (clock_difference(nt->lst_start,lst)>0){
        if(verbose){
            fprintf(stderr,
                    "adjusting lst_start from %7.3f to %7.3f\n",
                    nt->lst_start,lst);
        }
        nt->lst_start=lst;
    }


    /* calculate length of night remaining */

    night_duration=nt->lst_end-nt->lst_start;
    if(night_duration<0)night_duration=night_duration+24.0;


    if(verbose){
         fprintf(stderr,"lst_start: %7.3f\n",nt->lst_start);
         fprintf(stderr,"lst_end: %7.3f\n",nt->lst_end);
    }

    n_observable=0;
    for (i=0;i<num_fields;i++){

        f=sequence+i;
        f->n_done=0;
        f->ready=0;
        for(j=0;j<f->n_required;j++){
           f->lst[j]=0.0;
           f->ha[j]=0.0;
           f->am[j]=0.0;
        }

        /* get rise and set times of given position (the lst when the
           airmass crosses below and above the maximum airmass, MAX_AIRMASS). 
           If the object is already up at the start of the observing window
           (nt.lst_start), set lst_rise to nt.lst_start. If it is up at the 
           end of the observing window (nt.lst_end), set the lst_set to 
           nt.lst_end. If the object is not up at all during the observing 
           window, set lst_rise and lst_set to -1. */

        f->lst_rise=get_lst_rise_time(f->ra,f->dec,MAX_AIRMASS,nt,site,&am);
        f->lst_set=get_lst_set_time(f->ra,f->dec,MAX_AIRMASS,nt,site,&am);

        /* calculate total time object will be observable (lst_set-lst_rise)
           and the total time required to make all the observations */

        f->time_up=f->lst_set-f->lst_rise; 
        if(f->time_up<0)f->time_up=f->time_up+24.0;
        f->time_required=(f->n_required-1)*f->interval;
        f->time_left=f->time_up-f->time_required; 

        /* Determine if the observation is doable (i.e. it rises during the
           night and there is enough time to do all the observations while
           it is up). If so, set doable=1 and initialize the lst_next to the
           earliest possible time to observe the position (either lst_start or
           lst_rise).  If not, set doable=0 and  lst_next = -1. */

        /* darks are always doable. Set time left to whole night's duration */
        if(f->shutter==0){ 
          n_observable++;
          f->doable=1;
          f->lst_next=nt->lst_start;
          f->time_left=night_duration;
	  f->lst_rise=nt->lst_start;
	  f->lst_set=nt->lst_end;
	  f->time_up=night_duration;
/*

          if(verbose)fprintf(stderr,"field: %d %7.3f %7.3f darks\n",
			f->field_number,f->ra,f->dec);
*/
        }

        /* never rises. Not doable */
        else if(f->lst_rise<0){ 
           f->doable=0;
           f->lst_next=-1;
           f->time_left=-1;
/*
           if(verbose)fprintf(stderr,"field: %d %7.3f %7.3f never rises\n",
			f->field_number,f->ra,f->dec);
*/
        }

        /* not enough time for required obs. Not doable */
        else if(f->time_left<0){ 
           f->doable=0;
           f->lst_next=-1;
/*
           if(verbose)fprintf(stderr,"field: %d %7.3f %7.3f not enough time\n",
		f->field_number,f->ra,f->dec);
*/
        }

        /* Doable */
        else{ 
           n_observable++;
           f->doable=1;

           /* if position has already risen, set lst_next to lst_start.
              Otherwise set lst_next to lst_rise */

           if(clock_difference(f->lst_rise,nt->lst_start)>0){
               f->lst_next=nt->lst_start;
           }
           else{
               f->lst_next=f->lst_rise;
           }


        }
        if(verbose){
              fprintf(stderr,
                      "%d field: %d %7.3f %7.3f %7.3f %d rise: %7.3f  set: %7.3f lst_next: %7.3f  time_up : %7.3f time_required: %7.3f time_left: %7.3f\n",
		      f->doable,f->field_number,f->ra,f->dec,
                      f->expt,f->shutter,f->lst_rise,f->lst_set,
		      f->lst_next,f->time_up, f->time_required, f->time_left);
        }
    }

    return(n_observable);
}

/************************************************************/

/* If object is already up at current lst, return current lst value.
   If it rises before the end of the night, return with the rise time.
   If it never rises, return -1
*/

double get_lst_rise_time(double ra,double dec, double max_am, 
       Night_Times *nt, Site_Params *site, double *am)
{
    double ha,lst;

    /* get lst, hour angle, and airmass at start time of observing window.
       If am is less than max_am, object is already up. Return with lst.
       Otherwise, stepwise search from lst_start to lst_end to find the 
       rise time. If it never rises, return -1. Otherwise return the lst 
       when the am gets below max_am */

    lst=nt->lst_start;
    ha=get_ha(ra,lst);
    *am=get_airmass(ha,dec,site);

    if (*am < max_am ) return (lst);

    
    while(clock_difference(nt->lst_end,lst)<0&&*am>max_am){
          lst=lst+LST_SEARCH_INCREMENT;
          if(lst>24.0)lst=lst-24.0;
          ha=get_ha(ra,lst);
          *am=get_airmass(ha,dec,site);
    }

    if(*am>max_am){
       return(-1.0);
    }
    else{
       return(lst);
    }
     
    
}

/************************************************************/


double get_lst_set_time(double ra,double dec, double max_am, 
       Night_Times *nt, Site_Params *site, double *am)
{
    double ha,lst;

    /* get lst, hour angle, and airmass at end time of observing window.
       If am is less than max_am, object is still up. Return with lst.
       Otherwise, stepwise search from lst_end to lst_start to find the 
       set_time. If it sets before lst_start, return -1. Otherwise return the lst 
       when the am gets above max_am */


    lst=nt->lst_end;
    ha=get_ha(ra,lst);
    *am=get_airmass(ha,dec,site);

    if (*am < max_am ) return (lst);

    
    while(clock_difference(nt->lst_start,lst)>0&&*am>max_am){
          lst=lst-LST_SEARCH_INCREMENT;
          if(lst<0.0)lst=lst+24.0;
          ha=get_ha(ra,lst);
          *am=get_airmass(ha,dec,site);
    }

    if(*am>max_am){
       return(-1.0);
    }
    else{
       return(lst);
    }
     
    
}
/************************************************************/

/* Given two clock values (interval 0 to 24), return
   their difference, h2 - h1, assuming there difference can not
   be greater than 12 or less than -12 */

double clock_difference(double h1,double h2)
{
   double dt;

   dt = h2 - h1;
   if(dt>12.0)dt=dt-24.0;
   if(dt<-12.0)dt=dt+24.0;
   
   return (dt);
}
/************************************************************/

double get_airmass(double ha, double dec, Site_Params *site) {
 
  double alt, az, am;

  alt = altit(dec,ha,site->lat,&az);
  if (alt <= 0) {
     am=1000.0; /* below horizon */
  }
  else{
     am = 1.0/sin(alt/DEGTORAD);
  }
  return(am);
}
                                                                                               

/************************************************************/

double get_ha(double ra, double lst) {
  double ha;
 
  ha = lst - ra;
  if (ha <= -12.0) {
    ha += 24.0;
  } else if (ha >= 12.0) {
    ha -= 24.0;
  }
  return(ha);
}


/************************************************************/

int load_sequence(char *script_name, Field *sequence)
{

    FILE *input;
    int n,n_fields,line;
    char string[1024],*s_ptr,shutter_flag[3];
    Field *f;

    input=fopen(script_name,"r");
    if (input==NULL){
       fprintf(stderr,"load_sequence: can't open file %s\n",script_name);
       return(-1);
    }

    n_fields=0;
    line=0;
    while(fgets(string,1024,input)!=NULL){
      line++;
      /* get rid of leading blanks */
      s_ptr=string;
      while(strncmp(s_ptr," ",1)==0&&*s_ptr!=0)s_ptr++;

      /* if there are more characters left in the string, and if the
         current character is not "#", then read in the next line */

       if(*s_ptr!=0 && strncmp(s_ptr,"#",1)!=0){

          f=sequence+n_fields;
	  f->field_number=n_fields;
          f->line_number=line;
          strcpy(f->script_line,string);

          n=sscanf(s_ptr,"%lf %lf %s %lf %lf %d",
	    &(f->ra),&(f->dec),shutter_flag,&(f->expt),&(f->interval),
	    &(f->n_required));

          f->interval=f->interval/3600.0;
          f->expt=f->expt/3600.0;

          if(strcmp(shutter_flag,"Y")==0||strcmp(shutter_flag,"y")==0){
            f->shutter=1;
          }
          else if (strcmp(shutter_flag,"N")==0||strcmp(shutter_flag,"n")==0){
            f->shutter=0;
          }
          else{
            f->shutter=-1;
          }

           /* Make sure 6 parameters are read from the line, and that
             the parameters are within range. If so, increment n_fields
             and go on to the next line. If not, skip this field. */

          if(n!=6||f->ra<0.0||f->ra>24.0||f->dec<-90.0||f->dec>90.0||
                 f->expt>MAX_EXPT||f->expt<0||
                f->interval>MAX_INTERVAL||f->interval<MIN_INTERVAL||f->n_required<1||
                f->n_required>MAX_OBS_PER_FIELD){
             fprintf(stderr,"load_sequence: bad field line %d: %s\n",
                line,string);
          }
          else{
             n_fields++;
          }
       }
    }
         
    fclose(input);

    return(n_fields);

}
/************************************************************/

int print_field_status (Field *f, FILE *output)
{
    int i;
    double dt;

    if(f->n_done==f->n_required){
       fprintf(output,"Field : %d %s",
              f->field_number," Completed ");
    }
    else{
       fprintf(output,"Field : %d %s",
              f->field_number,"Unfinished ");
    }

    fprintf(output,"Required : %d  Done: %d Interval : %7.3f LSTs : ",
            f->n_required,f->n_done,f->interval);
    for(i=0;i<f->n_done;i++){
        fprintf(output,"%7.3f ",f->lst[i]);
    }
    fprintf(output," dLSTs: ");
    for(i=1;i<f->n_done;i++){
        dt=f->lst[i]-f->lst[i-1];
        if(dt<0.0)dt=dt+24.0;
        fprintf(output,"%7.3f ",dt);
    }
    fprintf(output,"\n");

    return(0);
}

/************************************************************/

/* print one line with character for each field in the sequence as follows:

   "." : not observable
   "n" : where is n number of fields completed
   "#" : completed
*/
   
int print_history(double lst, Field *sequence, int num_fields,FILE *output)
{
    int i;
    Field *f;
   
    fprintf(output,"%8.4f ",lst);

    for(i=0;i<num_fields;i++){
       f=sequence+i;
       if (f->n_done==f->n_required){
         fprintf(output,"%s",".");
       }
/*
       else if(f->doable==0){
         fprintf(output,"%s",".");
       }
*/
       else{
         fprintf(output,"%d",f->n_done);
       }
    }

    fprintf(output,"\n");
    fflush(output);

    return(0);
}

/*************************************************************************/

