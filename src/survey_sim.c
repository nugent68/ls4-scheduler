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

#define DEG_TO_RAD (3.14159/180.0)

#define DEBUG 0
#define HISTORY_FILE "survey.hist"
#define SELECTED_FIELDS_FILE "fields.selected"
#define LOG_OBS_FILE "log.obs"

#define DEGTORAD 57.29577951 /* 180/pi */
#define LST_SEARCH_INCREMENT 0.0166 /* 1 minute in hours */
#define LST_WAIT_INCREMENT 0.0833 /* 5 minutes in hours */

#define MAX_AIRMASS 2.0
#define MAX_OBS_PER_FIELD 24 /* maximum number of observations per field */
#define MAX_FIELDS 2000 /* maximum number of fields per script */
#define OBSERVATORY_SITE "ESO La Silla"
#define MAX_EXPT 1000.0
#define MAX_INTERVAL (43200.0/3600.0)
#define MIN_INTERVAL (900.0/3600.0)
#define MAX_DEC 60.0 /* no decs higher than this */

#define USE_12DEG_START 1 /* 1 to use 12-deg twilight, 0 to use 18-deg twilight */
#define STARTUP_TIME /*0.5*/0.0 /* hours to startup after end of twilight */
#define MIN_EXECUTION_TIME 0.029 /* minimum time (hours) to make an observation */
#if 0
#define EXPOSURE_OVERHEAD 0.0111 /* time to readout exposure (hours) = 40 sec */
#else
#define EXPOSURE_OVERHEAD 0.0125 /* time to readout exposure (hours) = 45 sec */
#endif

#define RA_STEP0 0.03333 /* hours difference in RA between paired fields
                            at 0 Dec */

typedef struct {
    int status; /* 0 if not doable, 2 if must observe pronto, 1 if ready to observe,
                   -1 if not enough time to observe remaining fields */
    int doable; /* 1 if all required observations possible, 0 if not */
    int field_number;
    int line_number;
    char script_line[1024];    
    double ra; /* hours */
    double dec; /*deg */
    double gal_long; /*deg*/
    double gal_lat; /* deg */
    double ecl_long; /* deg */
    double ecl_lat; /* deg */
    double epoch; /* current epoch of obs (+/- 0.5 days) */
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

int adjust_date(struct date_time *date, int n_days);

int init_night(struct date_time date, Night_Times *nt, 
                                     Site_Params *site);

int load_sequence(char *script_name, Field *sequence);

int check_weather(FILE *input, double lst, 
			struct date_time *date, Night_Times *nt);

int get_day_of_year(struct date_time *date);

int observe_next_field(Field *f, double lst, double *time_taken, 
		Night_Times *nt, FILE *output);

int init_fields(Field *sequence, int num_fields, 
                Night_Times *nt, Night_Times *nt_5day,
		Night_Times *nt_10day, Night_Times *nt_15day,
		Site_Params *site, double lst);

int moon_interference(Field *f, Night_Times *nt);

double get_lst_rise_time(double ra,double dec, double max_am, 
       Night_Times *nt, Site_Params *site, double *am);

double get_lst_set_time(double ra,double dec, double max_am, 
       Night_Times *nt, Site_Params *site, double *am);

double get_airmass(double ha, double dec, Site_Params *site);

double get_ha(double ra, double lst);

double clock_difference(double h1,double h2);

int get_next_field(Field *sequence,int num_fields, double lst, int i_prev);

int paired_fields(Field *f1, Field *f2);

int update_field_status(Field *sequence, double lst);

int shorten_interval(Field *f);

int print_field_status (Field *f, FILE *output);

int print_history(double lst,Field *sequence, int num_fields,FILE *output);


extern double sin(),fabs();

/************************************************************/

main(int argc, char **argv)
{
	char string[1024];
        struct date_time date,date_5day,date_10day,date_15day;
	Night_Times nt; /* times of sunrise/set, moonrise/set, etc */
        Night_Times nt_5day; /* nt for 5 days later */
        Night_Times nt_10day; /* nt for 10 days later */
        Night_Times nt_15day; /* nt for 15 days later */
        char script_name[1024];
	Field sequence[MAX_FIELDS];
        int i,num_fields,num_observable_fields,num_completed_fields;
        int i_prev;
        Site_Params site;
        double lst,dt;
        FILE *hist_out,*sequence_out,*log_obs_out,*weather_input;
        
        if(argc!=7&&argc!=6){
          fprintf(stderr,"syntax: survey_sim sequence_file yyyy mm dd verbose_flag [weather_file] \n");
          exit(-1);
        }

        strcpy(script_name,argv[1]);
        sscanf(argv[2],"%d",&(date.y));
        sscanf(argv[3],"%d",&(date.mo));
        sscanf(argv[4],"%d",&(date.d));
        sscanf(argv[5],"%d",&verbose);
	date.h=0;
	date.mn=0;
	date.s=0;

        if(argc==7){
           weather_input=fopen(argv[6],"r");
           if(weather_input==NULL){
               fprintf(stderr,"can't open weather file %s\n",argv[6]);
               exit(-1);
           }
        }
        else{
           weather_input=NULL;
        }

        hist_out=fopen(HISTORY_FILE,"w");
        if(hist_out==NULL){
            fprintf(stderr,"can't open file %s for output\n",HISTORY_FILE);
            exit(-1);
        }     

        sequence_out=fopen(SELECTED_FIELDS_FILE,"w");
        if(sequence_out==NULL){
            fprintf(stderr,"can't open file %s for output\n",SELECTED_FIELDS_FILE);
            exit(-1);
        }     

        log_obs_out=fopen(LOG_OBS_FILE,"w");
        if(log_obs_out==NULL){
            fprintf(stderr,"can't open file %s for output\n",LOG_OBS_FILE);
            exit(-1);
        }     

        /* initialize site parameters for DEFAULT observatory */

        strcpy(site.site_name,"DEFAULT"); /* DEFAULT assigned to ESO La Silla */
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


        /* get date for current date plus 5 days */
        date_5day=date;
        adjust_date(&date_5day,5);
        init_night(date_5day,&nt_5day,&site);

 
        /* get date for current date plus 10 days */
        date_10day=date;
        adjust_date(&date_10day,10);
        init_night(date_10day,&nt_10day,&site);


        /* get date for current date plus 15 days */
        date_15day=date;
        adjust_date(&date_15day,15);
        init_night(date_15day,&nt_15day,&site);

        /* initialize tonight */
        init_night(date,&nt,&site);

        if(verbose){
           fprintf(stderr,"# lst start : %7.3f\n",nt.lst_start); 
           fprintf(stderr,"# lst end   : %7.3f\n",nt.lst_end); 
           fprintf(stderr,"# moon ra   : %7.3f\n",nt.ra_moon); 
           fprintf(stderr,"# moon dec  : %7.3f\n",nt.dec_moon); 
           fprintf(stderr,"# moon frac : %7.3f\n",nt.percent_moon); 
           fprintf(stderr,"# moon ra5  : %7.3f\n",nt_5day.ra_moon); 
           fprintf(stderr,"# moon dec5 : %7.3f\n",nt_5day.dec_moon); 
           fprintf(stderr,"# moon frac5: %7.3f\n",nt_5day.percent_moon); 
           fprintf(stderr,"# moon ra10 : %7.3f\n",nt_10day.ra_moon); 
           fprintf(stderr,"# moon dec10: %7.3f\n",nt_10day.dec_moon); 
           fprintf(stderr,"# moon frac10: %7.3f\n",nt_10day.percent_moon); 
           fprintf(stderr,"# moon ra15 : %7.3f\n",nt_15day.ra_moon); 
           fprintf(stderr,"# moon dec15: %7.3f\n",nt_15day.dec_moon); 
           fprintf(stderr,"# moon frac15: %7.3f\n",nt_15day.percent_moon); 
        }


        if(verbose){
          fprintf(stderr,"# loading sequence file %s\n",script_name);
        }

        num_fields=load_sequence(script_name,sequence);
        if (num_fields<1){
            fprintf(stderr,"Error loading script %s\n",script_name);
             exit(-1);
        }
 
        if(verbose){
          fprintf(stderr,"# %d fields successfully loaded\n",num_fields);
        }

        if(verbose){
          fprintf(stderr,"# initialing sequence fields \n");
        }

        lst=nt.lst_start;

        num_observable_fields=init_fields(sequence,num_fields,
                              &nt,&nt_5day,&nt_10day,&nt_15day,&site,lst);

        if(verbose){
          fprintf(stderr,"# %d observable field \n",num_observable_fields);
        }


        fprintf(stderr,"lst: %7.3f Starting observations\n",lst);

        /* keep choosing next field and incrementing lst accordingly
           until end of night */

        i_prev=-1;
        while(clock_difference(lst,nt.lst_end)>0){

             /* if weather file is open, and weather is bad, increment lst */

/*
             if(verbose&&weather_input!=NULL){
                fprintf(stderr,"lst: %7.3f checking weather\n");
             }
*/
             if(weather_input!=NULL&&check_weather(weather_input,lst,&date,&nt)!=0){

                if(verbose){
                   fprintf(stderr,"lst : %7.3f  bad weather\n",lst);
                }
                lst=lst+LST_WAIT_INCREMENT;
             }

             /* otherwise try to observe next field */

             else{
               

                 /* choose next field to observe */

                 i=get_next_field(sequence,num_fields,lst,i_prev);

                 /* if i < 0, no fields to observe */

                 if(i<0){
                    if(verbose){
                      fprintf(stderr,"lst : %7.3f  No fields to observe\n",lst);
                    }
                    lst=lst+LST_WAIT_INCREMENT;
                 }
            
                 /* otherwise, observe field i */

                 else{
                    observe_next_field(sequence+i,lst,&dt,&nt,log_obs_out);
                    print_history(lst,sequence,num_fields,hist_out);
                    lst=lst+dt;
		    i_prev=i;
                 }
             }

             if(lst>24.0)lst=lst-24.0;
             
        }

       fprintf(stderr,"lst: %7.3f Ending observations\n",lst);

        num_completed_fields=0;
        for(i=0;i<num_fields;i++){
           if(sequence[i].n_done==sequence[i].n_required){
                fprintf(sequence_out,"%s",sequence[i].script_line);
                num_completed_fields++;
           }
           print_field_status(sequence+i,stderr);
        }

        fprintf(stderr, "# %d fields loaded  %d fields observable  %d field completed\n",
		num_fields, num_observable_fields, num_completed_fields);

       fclose(log_obs_out);
       fclose(sequence_out);
       fclose(hist_out);

        

        exit(0);
}

/************************************************************/

int check_weather (FILE *input, double lst, struct date_time *date, Night_Times *nt)
{
     char string[1024],s[256];
     double t_obs,t_on,duration,ut;
     int year, mon, day, doy;

     /* ut date is 1 + doy since get_day_of_year takes local time */
     doy=1+get_day_of_year (date); 

     /* ut values don't go past 24 hours at ESO La Silla at night */
     ut=nt->ut_start+clock_difference(nt->lst_start,lst);

     t_obs=doy+(ut/24.0);
     t_on=0;
     duration=0.0;

/*
     if(verbose){
        fprintf(stderr,"check_weather : doy,ut,t_obs = %03d %10.6f %10.6f\n",doy,ut,t_obs);
     }
*/
     /* read lines from weather input until line is found with 
        t_on exceeding t_obs. If t_obs>t_on
        but  t_obs< t_on+duration, weather was good. 
        Otherwise, return 1 */

     rewind(input);

     while(t_obs>t_on+duration&&fgets(string,1024,input)!=NULL){
       /*if(verbose)fprintf(stderr,"%s",string);*/
       sscanf(string,"%s %s %s %lf %s %lf",s,s,s,&t_on,s,&duration);
       duration=duration/24.0;
     }
/*
     if(verbose){
        fprintf(stderr,"check_weather : t_on, duration = %10.6f %10.6f\n",t_on,duration);
     }
*/

     if( t_obs >= t_on && t_obs <= t_on+duration){
       return(0);
     }
     else{
       return(1);
     }


}
       
/************************************************************/

int get_day_of_year(struct date_time *date)
{
    int n,doy;

    doy=date->d;

    if(date->mo>1)doy=doy+31;
    if(date->mo>2)doy=doy+28;
    if(date->mo>3)doy=doy+31;
    if(date->mo>4)doy=doy+30;
    if(date->mo>5)doy=doy+31;
    if(date->mo>6)doy=doy+30;
    if(date->mo>7)doy=doy+31;
    if(date->mo>8)doy=doy+31;
    if(date->mo>9)doy=doy+30;
    if(date->mo>10)doy=doy+31;
    if(date->mo>11)doy=doy+30;

    n=(date->y)/4;
    n=n*4;
    if(date->y==n)doy++;

    return(doy);
}

/************************************************************/

int init_night(struct date_time date, Night_Times *nt, 
                                     Site_Params *site)
{
        double dt;

        /* initialize night_time values */
        print_tonight(date,site->lat,site->longit,site->elevsea,site->elev,site->horiz,
                      site->site_name,site->stdz,site->zone_name,site->zabr,site->use_dst,
                      &(site->jdb),&(site->jde),2,nt,0);



        if (USE_12DEG_START){
          nt->ut_start=nt->ut_evening12+STARTUP_TIME;
          nt->ut_end=nt->ut_morning12-MIN_EXECUTION_TIME;
          nt->lst_start=nt->lst_evening12+STARTUP_TIME;
          nt->lst_end=nt->lst_morning12-MIN_EXECUTION_TIME;
          nt->jd_start=nt->jd_evening12+(STARTUP_TIME/24.0);
          nt->jd_end=nt->jd_morning12-(MIN_EXECUTION_TIME/24.0);
       }
        else {
          nt->ut_start=nt->ut_evening18+STARTUP_TIME;
          nt->ut_end=nt->ut_morning18-MIN_EXECUTION_TIME;
          nt->lst_start=nt->lst_evening18+STARTUP_TIME;
          nt->lst_end=nt->lst_morning18-MIN_EXECUTION_TIME;
          nt->jd_start=nt->jd_evening18+(STARTUP_TIME/24.0);
          nt->jd_end=nt->jd_morning18-(MIN_EXECUTION_TIME/24.0);
        }

        /* make sure night isn't longer than 12 hours. Otherwise
           clock_difference function fails */

        if(nt->lst_start<nt->lst_end&&nt->lst_end-nt->lst_start>12){
           dt=nt->lst_end-nt->lst_start-12;
	   dt=dt*1.1;
           dt=dt/2.0;
           nt->lst_start=nt->lst_start+dt;
           nt->lst_end=nt->lst_end-dt;
           nt->ut_start=nt->ut_start+dt;
           nt->ut_end=nt->ut_end-dt;
	   nt->jd_start=nt->jd_start+(dt/24.0);
	   nt->jd_end=nt->jd_end-(dt/24.0);
        }
        else if(nt->lst_start>nt->lst_end&&nt->lst_end-nt->lst_start+24>12){
           dt=nt->lst_end+24-nt->lst_start-12;
           dt=dt*1.1;
           dt=dt/2.0;
           nt->lst_start=nt->lst_start+dt;
           nt->lst_end=nt->lst_end-dt;
           nt->ut_start=nt->ut_start+dt;
           nt->ut_end=nt->ut_end-dt;
	   nt->jd_start=nt->jd_start+(dt/24.0);
	   nt->jd_end=nt->jd_end-(dt/24.0);
        }
           

        if(nt->ut_start>24.0)nt->ut_start=nt->ut_start-24.0;
        if(nt->lst_start>24.0)nt->lst_start=nt->lst_start-24.0;

        if(nt->ut_end<0.0)nt->ut_end=nt->ut_end+24.0;
        if(nt->lst_end<0.0)nt->lst_end=nt->lst_end+24.0;



        return(0);
}

/************************************************************/

int adjust_date(struct date_time *date, int n_days)
{
    double jd;
    short dow;

    jd=date_to_jd(*date);
    jd=jd+n_days;
    caldat(jd,date,&dow);

    return(0);
}
/************************************************************/

/* choose next field to observe at time lst. If there are no fields,
   return with dt = 0 and i < 0. If there is a field, update the 
   observation count, set the next time to observe the field, 
   set dt = exposure time plus overhead, and return
   with the index of the chosen field. */

int observe_next_field(Field *f,double lst, double *dt, Night_Times *nt,
				FILE *output)
{
    double jd,ut;

    /* update n_done, lst_next, and compute dt */

    f->lst[f->n_done]=lst;
    f->n_done=f->n_done+1;
    f->lst_next=lst+f->interval;
    if (f->lst_next>24.0)f->lst_next=f->lst_next-24.0;
 
    ut=nt->ut_start+clock_difference(nt->lst_start,lst);
    jd=nt->jd_start+(clock_difference(nt->lst_start,lst)/24.0);

    *dt=f->expt+EXPOSURE_OVERHEAD;
 
    fprintf(stderr,
    "lst : %7.3f Exposing field : %d obs %d out of %d time_left : %7.3f lst_next : %7.3f\n",
         lst, f->field_number, f->n_done, f->n_required, f->time_left, f->lst_next);
    if(output!=NULL){
        if(f->shutter){
           fprintf(output,"%10.6f %10.6f %s %d %6.1f %7.3f %10.5f\n",
		f->ra,f->dec,"y",1,3600.0*f->expt,ut,jd);
        }
        else{
           fprintf(output,"%10.6f %10.6f %s %d %6.1f %7.3f %10.5f\n",
		f->ra,f->dec,"n",1,3600.0*f->expt,ut,jd);
        }
    }

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

   If there are nto field ready to observe, choose among the "late"
   fields which are observable,but there is not enough time to
   observe all the remaining observations. Choose the late field with
   the most amount of time left (least negative value) and shorten
   the time interval between fields so that there time left. Choose
   this field.

   If there are not late fields that can be shortened (while still
   keeping interval > MIN_INTERVAL), return -1 (no field selected).

*/

int get_next_field(Field *sequence,int num_fields, double lst, int i_prev)
{
     Field *f,*f_prev,*f_next;
     double time_left_min,time_left,time_left_max;
     int i,n_left,n_left_min;
     int i_min,i_max,status,n_ready,n_late;
    

     if(i_prev>=0&&i_prev<num_fields-1){
       f_prev=sequence+i_prev;
       f_next=sequence+i_prev+1;
     }
     else{
       f_prev=NULL;
       f_next=NULL;
     }

     n_left_min=1000;
     n_ready=0;
     n_late=0;

     for(i=0;i<num_fields;i++){
         f=sequence+i;

         
         update_field_status(f,lst);

         /* status of 2 means must do now (i.e. darks or 2nd offset
            field).  Return field index */

         if(f->status==2){
            return(i);
         }

         /* status of 0 means ready to observe. Update minimum
            value of n_left */

         else if (f->status>0){
	    n_ready++;
            n_left=f->n_required-f->n_done;
            
            if(n_left<n_left_min){
                 n_left_min=n_left;
            }
         }

         /* status of -1 means no time left to observe remaining
            field. Update count of these "late" fields */

         else if (f->status<0){
            n_late++;
         }
     }
  
     /* if the pair to the previous fields is ready to observed,
        this is first pick */

     if(f_next!=NULL&&f_next->status>0&&paired_fields(f_next,f_prev)){
	return(i_prev+1);
     }

     /* otherwise choose field that has least time left to complete
	the required observations */

     else if(n_ready>0){

      
       time_left_min=10000.0;
       i_min=-1;

       for(i=0;i<num_fields;i++){
         f=sequence+i;
         if(f->status>0){
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

     else if (n_late>0){

        if(verbose) {
          fprintf(stderr,"get_next_field: No fields ready. %d late fields\n",n_late);
          fprintf(stderr,"get_next_field: Choosing field to shorten interval\n");
        }

        i_max=-1;
        time_left_max=-1000;
        for(i=0;i<=num_fields;i++){
          f=sequence+i;
          if(f->status==-1&&f->time_left>time_left_max){
                time_left_max=f->time_left;
                i_max=i;
          }
        }


        if(i_max<0){
           if(verbose)fprintf(stderr,"get_next_field: No fields to shorten\n");
           return(-1);
        }
           
        if(verbose){
           fprintf(stderr,
                "get_next_field: choosing field %d to shorten intervals\n",
                 i_max);
        }
           
        f=sequence+i_max;
        shorten_interval(f);
        update_field_status(f,lst);
        if(f->status>0){

           if(verbose){
              fprintf(stderr,
                 "get_next_field: interval shortened to %7.3f\n",
                 f->interval*3600.0);
           }
           return(i_max);
        }
        else{
           if(verbose){
              fprintf(stderr,
                 "get_next_field: could not shorten interval of field %d\n",
                 i_max);
           }

           return(-1);
        } 
     }
     else{

        if(verbose) {
          fprintf(stderr,"get_next_field: No fields ready or late\n");
        }

        return(-1);

     }

}
/*******************************************************/

int paired_fields(Field *f1, Field *f2)
{
    double dra;

    dra=1.1*RA_STEP0/cos(f1->dec*DEG_TO_RAD);

    if(f2->dec==f1->dec&&
	   fabs(clock_difference(f1->ra,f2->ra))<dra){
	return(1);
    }
    else{
	return(0);
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
        /*new_interval = f->interval/2.0;*/

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

/* If field is up and ready to be observed, update time_up, time_remaining
   and time_left and return 1. If not doable, return 0. If the field has
   been completed or has set, set doable to 0 and return 0.*/

int update_field_status(Field *f, double lst)
{

      /* Isn't doable, Just return 0*/
      if(f->doable==0){
	  f->status=0;
          return(0);
      }

      /* Has been completed. Set doable to 0 and return 0*/

      else if(f->n_done==f->n_required){
         f->doable=0; 
         f->status=0;
         return(0);
      }
        
      /* hasn't risen yet. Just return 0 */

      else if(clock_difference(lst,f->lst_rise)>0){
         f->status=0;
         return(0);
      }

      /* has already set, Set doable to 0 and return 0 */

      else if(clock_difference(lst,f->lst_set)<0){
         f->doable=0;
         f->status=0;
         return(0);
      }

      /* not yet time to reobserve. Return 0 */

      else if (clock_difference(lst,f->lst_next)>MIN_EXECUTION_TIME){
         f->status=0;
         return(0);
      }

      /* dark, and it is time to observe it. Return 2
         to indicate must do immediately */

      else if (f->shutter==0 ){
        f->status=2;
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
	      f->status=0;
              return(0);
#else
              f->status=-1;
              return(-1);
#endif
        }
        else{
            f->status=1;
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
                Night_Times *nt, Night_Times *nt_5day,
		Night_Times *nt_10day, Night_Times *nt_15day,
		Site_Params *site, double lst)
{
    int i,j,n_observable,n_up_too_short,n_moon_too_close,n_never_rise;
    int n_moon_too_close_later;
    int n_same_ra;
    Field *f;
    double am,time_up,time_required,night_duration;
    double ra_prev,current_epoch;

    n_up_too_short=0;
    n_moon_too_close=0;
    n_moon_too_close_later=0;
    n_never_rise=0;
    n_same_ra=1;
    ra_prev=0.0;
    
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
        f->status=0;
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
        galact(f->ra,f->dec,2000.0,&(f->gal_long),&(f->gal_lat));
        eclipt(f->ra,f->dec,2000.0,nt->jd_start,&(f->epoch),&(f->ecl_long),&(f->ecl_lat));

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

        else if (f->dec>MAX_DEC){
           f->doable=0;
           if(verbose)fprintf(stderr,"field: %d %7.3f %7.3f dec too high\n",
		f->field_number,f->ra,f->dec);
        }

        /* below 30 deg galactic latitude, too much extinction for supernove */
        else if (fabs(f->gal_lat)<30.0){
           f->doable=0;
           if(verbose)fprintf(stderr,"field: %d %7.3f %7.3f galactic lat too low: %7.3f\n",
		f->field_number,f->ra,f->dec,f->gal_lat);
        }

        /* never rises. Not doable */
        else if(f->lst_rise<0){ 
           n_never_rise++;
           f->doable=0;
           f->lst_next=-1;
           f->time_left=-1;

           if(verbose)fprintf(stderr,"field: %d %7.3f %7.3f never rises\n",
			f->field_number,f->ra,f->dec);

        }

        /* not enough time for required obs. Not doable */
        else if(f->time_left<0){ 
           n_up_too_short++;
           f->doable=0;
           f->lst_next=-1;

           if(verbose)fprintf(stderr,"field: %d %7.3f %7.3f up too short\n",
		f->field_number,f->ra,f->dec);

        }

       /* Field too close to tonight's moon. Not doable */
        else if (moon_interference(f,nt)){
            n_moon_too_close++;
            f->doable=0;
            f->lst_next=-1;

            if(verbose)fprintf(stderr,
		"field: %d %7.3f %7.3f moon too close\n",
		f->field_number,f->ra,f->dec);
 
        }
       /* Field too close to moon on more than one of the remaining epochs. Not doable */
        else if (moon_interference(f,nt_5day)+
		moon_interference(f,nt_10day)+moon_interference(f,nt_15day)>1){
            n_moon_too_close_later++;
            f->doable=0;
            f->lst_next=-1;

            if(verbose)fprintf(stderr,
		"field: %d %7.3f %7.3f %d %d %d moon too close later\n",
		f->field_number,f->ra,f->dec,
		moon_interference(f,nt_5day),
		moon_interference(f,nt_10day),
		moon_interference(f,nt_15day));
 
        }


        /* Allow no more than 18 fields in a row at the same ra*/
  
        else if (fabs(clock_difference(f->ra,ra_prev))<
		   1.1*RA_STEP0/cos(f->dec*DEG_TO_RAD)&&n_same_ra>=18){
           f->doable=0;
           if(verbose)fprintf(stderr,"field: %d %7.3f %7.3f too many at same ra\n",
		f->field_number,f->ra,f->dec);

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

           if(fabs(clock_difference(f->ra,ra_prev))<
			1.1*RA_STEP0/cos(f->dec*DEG_TO_RAD)){
                 n_same_ra++;
           }
           else{
                 ra_prev=f->ra;
                 n_same_ra=1;
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

    if(verbose){
      fprintf(stderr,
        "init_fields: %d never rise  %d up to short  %d moon to close %d too close later %d observable\n",
	n_never_rise,n_up_too_short, n_moon_too_close, n_moon_too_close_later, n_observable);
    }

    return(n_observable);
}
/************************************************************/

int moon_interference(Field *f, Night_Times *nt)
{
    double dra,ddec,dmoon;


    if(nt->percent_moon>0.5){
    	dra=clock_difference(nt->ra_moon,f->ra)*15.0;
    	ddec=nt->dec_moon-f->dec;
    	dmoon=sqrt(dra*dra+ddec*ddec);
#if 0
fprintf(stderr,"field: %7.3f %7.3f moon: %7.3f %7.3f  dra, ddec, dmoon: %7.3f %7.3f %7.3f\n",
f->ra,f->dec,nt->ra_moon,nt->dec_moon, dra,ddec,dmoon);
#endif

	if(dmoon<40.0)return(1);
     }
 
     return(0);
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

    /*fprintf(stderr,"%10.6f %10.6f %10.6f %10.6f\n",lst,nt->lst_end,ha,*am);*/
     
    while(clock_difference(nt->lst_end,lst)<0&&*am>max_am){
          lst=lst+LST_SEARCH_INCREMENT;
          if(lst>24.0)lst=lst-24.0;
          ha=get_ha(ra,lst);
          *am=get_airmass(ha,dec,site);
          /*fprintf(stderr,"%10.6f %10.6f %10.6f %10.6f\n",lst,nt->lst_end,ha,*am);*/
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

 /*fprintf(stderr,"%10.6f %10.6f %10.6f %10.6f\n",lst,nt->lst_start,ha,*am);*/
   
    while(clock_difference(nt->lst_start,lst)>0&&*am>max_am){
          lst=lst-LST_SEARCH_INCREMENT;
          if(lst<0.0)lst=lst+24.0;
          ha=get_ha(ra,lst);
          *am=get_airmass(ha,dec,site);
/*fprintf(stderr,"%10.6f %10.6f %10.6f %10.6f\n",lst,nt->lst_start,ha,*am);*/
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

    fprintf(output," gal. lat: %7.3f ",f->gal_lat);
    fprintf(output," ecl. lat: %7.3f ",f->ecl_lat);

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

