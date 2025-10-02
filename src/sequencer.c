/* scheduler.c

   DLR 2007 Mar 3

   Read in sequence of observations. Repeatedly simulated night's
   worth of observations until balance between observations
   belonging to survey codes 0, 1, and 2 meets target proportions.

   See scheduler.c for comments about code.

*/

#include "scheduler.h"

#define LOOP_WAIT_SEC 120 /* seconds to wait between loops if no
                           field selected */

#define TOLERANCE 0.05

int verbose=1;
int verbose1 = 0; /* set to 1 for very verbose */
int pause_flag=0;
int focus_done=0;
int offset_done=0;
int stop_flag=1; /* 1 for stopped, 0 for tracking */
int stow_flag=1; /* 1 for stowed, 0 for not stowed */
double focus_start=NOMINAL_FOCUS_START;
double focus_increment=NOMINAL_FOCUS_INCREMENT;
double focus_default=NOMINAL_FOCUS_DEFAULT;
FILE *sequence_out,*log_obs_out,*obs_record;
double ut_prev=0;


int cut_fields(double m0, double m1, double m2, 
      double f0, double f1, double f2, 
      Field *sequence, int num_fields, int num_completed_fields,
      double *priority);


int new_load_sequence(char *script_name, Field *sequence, double *priority);

/************************************************************/

main(int argc, char **argv)
{
	int done = 0;
	struct tm tm;
        double jd,ut;
	char string[1024];
        struct date_time date,date_5day,date_10day,date_15day;
	Night_Times nt; /* times of sunrise/set, moonrise/set, etc */
        Night_Times nt_5day; /* nt for 5 days later */
        Night_Times nt_10day; /* nt for 10 days later */
        Night_Times nt_15day; /* nt for 15 days later */
        char script_name[1024];
	Field sequence[MAX_FIELDS];
        double priority[MAX_FIELDS];
        int i,num_fields,num_observable_fields,num_completed_fields;
        int i_prev,result;
        Site_Params site;
        Telescope_Status tel_status;
        Camera_Status cam_status;
        double dt;
        int bad_weather,delay_sec;
        Fits_Header fits_header;
        FILE *weather_input;
        double f0,f1,f2,m0,m1,m2,weight0,weight1,weight2,time0,time1,time2;
        double total_time,dead_time;
        int n0,n1,n2;
    

        if(argc!=10&&argc!=9){
          fprintf(stderr,
                "syntax: scheduler sequence_file yyyy mm dd f0 f1 f2 verbose_flag [weather_file] \n");
          do_exit(-1);
        }
        if(argc==10){
           weather_input=fopen(argv[9],"r");
           if(weather_input==NULL){
               fprintf(stderr,"can't open weather file %s\n",argv[6]);
               do_exit(-1);
           }
        }
        else{
           weather_input=NULL;
        }

        strcpy(script_name,argv[1]);
        sscanf(argv[2],"%d",&(date.y));
        sscanf(argv[3],"%d",&(date.mo));
        sscanf(argv[4],"%d",&(date.d));
        sscanf(argv[5],"%lf",&f0);
        sscanf(argv[6],"%lf",&f1);
        sscanf(argv[7],"%lf",&f2);
        sscanf(argv[8],"%d",&verbose);

        /* intialize local time of start date to 0 h */
	date.h=0;
	date.mn=0;
	date.s=0;


        /* initialize fits header keywords and values */
        if(init_fits_header(&fits_header)!=0){
           fprintf(stderr,"could not init FITS words\n");
           do_exit(-1);
        }

        /* open sequence file */

        sequence_out=fopen(SELECTED_FIELDS_FILE,"w");
        if(sequence_out==NULL){
            fprintf(stderr,"can't open file %s for output\n",SELECTED_FIELDS_FILE);
            do_exit(-1);
        }     

  
        if(verbose){
          fprintf(stderr,"loading sequence file %s\n",script_name);
        }

        num_fields=new_load_sequence(script_name,sequence,priority);
        if (num_fields<1){
            fprintf(stderr,"Error loading script %s\n",script_name);
            do_exit(-1);
        }
 
        if(verbose){
           fprintf(stderr,
             "%d fields successfully loaded\n",num_fields);
        }

        /* initialize site parameters for DEFAULT observatory (ESO La SIlla) */

        strcpy(site.site_name,"DEFAULT"); /* DEFAULT assigned to ESO La SIlla */
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


        /* get the date and the night_info for the current date *./

        /* initialize tonight */
        init_night(date,&nt,&site);

        if(verbose){
           fprintf(stderr,"# ut sunset : %7.3f\n",nt.ut_sunset); 
           fprintf(stderr,"# ut start  : %7.3f\n",nt.ut_start); 
           fprintf(stderr,"# ut end    : %7.3f\n",nt.ut_end); 
           fprintf(stderr,"# ut sunrise: %7.3f\n",nt.ut_sunrise); 
           fprintf(stderr,"# jd start  : %7.3f\n",nt.jd_start-2450000); 
           fprintf(stderr,"# jd end    : %7.3f\n",nt.jd_end-2450000); 
           fprintf(stderr,"# moon ra   : %7.3f\n",nt.ra_moon); 
           fprintf(stderr,"# moon dec  : %7.3f\n",nt.dec_moon); 
           fprintf(stderr,"# moon frac : %7.3f\n",nt.percent_moon); 
        }

    /* initialize fraction of fields for each of three survey codes (0,1,2)
       to zero */

    m0=0.0;
    m1=0.0;
    m2=0.0;

    /* while the resulting fraction of completed fields for each code differs
       from the target fraction (f0, f1, f2) by more than TOLERANCE, keep
       repeating the nights observations and cutting out fields until the
       target ratios are met */

    while(fabs(m0-f0)>TOLERANCE||
            fabs(m1-f1)>TOLERANCE||fabs(m1-f1)>TOLERANCE){

        /* open log file */

        log_obs_out=fopen(LOG_OBS_FILE,"w");
        if(log_obs_out==NULL){
            fprintf(stderr,"can't open file %s for output\n",LOG_OBS_FILE);
            do_exit(-1);
        }

	stow_flag=0;

        /* Now its time to start observing. Initialize the status of
           the fields given the current jd */

        jd=nt.jd_sunset;
        ut=nt.ut_sunset;

        if(verbose){
          fprintf(stderr,"initialing sequence fields \n");
        }

        num_observable_fields=init_fields(sequence,num_fields,
                       &nt,&nt_5day,&nt_10day,&nt_15day,&site,jd);


        /* keep choosing next field, observing it,  and incrementing 
           jd accordingly until end of night or no more observations
           left  */

     
        time0=0.0;
        time1=0.0;
        time2=0.0;
        dead_time=0.0;
        i_prev=-1; /* initialize index of previously observed field to -1 */
	done = 0; /* intialize done flag to 0. On termination of evening,
                     it will be set to 1 */

        /* Wait for sun to set or observations to end. */
 
        while(jd<nt.jd_sunrise && !done){

             bad_weather=0;

             if(weather_input!=NULL&&
			check_weather(weather_input,jd,&date,&nt)!=0){
	          bad_weather=1;
             }

             /* choose next field to observe */

             i=get_next_field(sequence,num_fields,i_prev,jd,bad_weather);

             /* if i < 0, no fields to observe */

             if(i<0){
                    if(verbose){
                      fprintf(stderr,
                      "# UT : %9.6f No fields ready to observe\n",ut);
                    }

                    /* If there are not more fields ready to be observed,
                       and the sun is rising, stop the observations by
                       setting done flag to 1 */

                    if(jd>nt.jd_sunrise){
			fprintf(stderr,"Ending Scheduled Observations\n");
			fflush(stderr);
			done=1;
		    }

                    /* Otherwise just wait a little and check again */

		    else{
			fprintf(stderr,"Wait before checking again\n");
			fflush(stderr);
                        jd=jd+(LOOP_WAIT_SEC/86400.0);
                        if(jd>nt.jd_start&&jd<nt.jd_end)dead_time=dead_time+(LOOP_WAIT_SEC/3600.0);
		    }
             }
           
             /* otherwise, if the weather is good,
                go ahead and observe field i */

             else if (!bad_weather){

                    if(observe_next_field(sequence,i,i_prev,jd,&dt,&nt,
			log_obs_out,&tel_status,&cam_status,&fits_header)!=0){
                       fprintf(stderr,"ERROR observing field %d\n",i);
                       fflush(stderr);
                       if(stop_flag==0){
                           if(verbose){
                               fprintf(stderr,"stopping telescope\n");
                               fflush(stderr);
                           }
                           stop_flag=1;
                       }
                    }
 
                    /* A memory leak of some kind requires this fflush statement here */
		    fflush(stderr);

                    jd=jd+(dt/SIDEREAL_DAY_IN_HOURS);
                    if(sequence[i].survey_code==0){
                             time0=time0+dt;
                    }
                    else if(sequence[i].survey_code==1){
                             time1=time1+dt;
                    }
                    else if(sequence[i].survey_code==2){
                             time2=time2+dt;
                    }
                  
		    i_prev=i;

                 }

          /* otherwise wait and go around loop again */

          else{
                jd=jd+(LOOP_WAIT_SEC/86400.0);
                dead_time=dead_time+(LOOP_WAIT_SEC/3600.0);
          } 
             
        } /* end of while (jd < nt.sunrise) loop */

 
        num_completed_fields=0;
	n0=0;
	n1=0;
	n2=0;
        weight0=0.0;
        weight1=0.0;
        weight2=0.0;
        for(i=0;i<num_fields;i++){
           if(sequence[i].n_done==sequence[i].n_required){
              if(sequence[i].survey_code==0){
                  n0++;
                  weight0=weight0+priority[i];
              }
              else if(sequence[i].survey_code==1){
                  n1++;
                  weight1=weight1+priority[i];
              }
              else if(sequence[i].survey_code==2){
                  n2++;
                  weight2=weight2+priority[i];
              }
              /*fprintf(sequence_out,"%s",sequence[i].script_line);*/
              num_completed_fields++;
           }
           /*print_field_status(sequence+i,stderr);*/
        }

       total_time=time0+time1+time2;
       fprintf(stderr, 
            "# %d fields loaded  %d fields observable  %d field completed\n",
            num_fields, num_observable_fields, num_completed_fields);

        fprintf(stderr,
            "# totals: %d %d %d   weights: %f %f %f  times: %f %f %f %f    %f %f %f  dead_time: %f\n",
             n0,n1,n2,weight0,weight1,weight2,
             total_time,time0,time1,time2,
	     time0/total_time,time1/total_time,time2/total_time,dead_time);

        m0=(double)n0/(double)num_completed_fields;
	m1=(double)n1/(double)num_completed_fields;
	m2=(double)n2/(double)num_completed_fields;

        fclose(log_obs_out);

        if(f0<0){
           break;
        }
        else{
            cut_fields(m0,m1,m2,f0,f1,f2,
               sequence, num_fields, num_completed_fields,priority);
        }
     }

     fprintf(stderr, 
            "# %d fields loaded  %d fields observable  %d field completed\n",
            num_fields, num_observable_fields, num_completed_fields);

     total_time=time0+time1+time2;

     fprintf(stderr,
        "# totals: %d %d %d   weights: %f %f %f  times: %f %f %f %f   %f %f %f   dead_time: %f\n",
             n0,n1,n2,weight0,weight1,weight2,
             total_time, time0,time1,time2,
             time0/total_time,time1/total_time,time2/total_time,dead_time);

     for(i=0;i<num_fields;i++){
         if(sequence[i].n_done==sequence[i].n_required){
                fprintf(sequence_out,"%s",sequence[i].script_line);
         }
         else if (sequence[i].field_number<0){
                fprintf(sequence_out,"## excluded %s",sequence[i].script_line);
         }
         else if (sequence[i].n_done>0){
                fprintf(sequence_out,"## incomplete %s",sequence[i].script_line);
         }
         else if (sequence[i].n_done==0){
                fprintf(sequence_out,"## skipped %s",sequence[i].script_line);
         }
     }
                                                                                                                                          

      do_exit(0);
}

/******************************************************************/

int cut_fields(double m0, double m1, double m2, 
            double f0, double f1, double f2, 
            Field *sequence, int num_fields, int num_completed_fields,
	    double *priority)
{
    int i;
    int n0, n1, n2;
    int dn0,dn1,dn2;
    double min_priority0,min_priority1,min_priority2;


    /* Estimage number of fields to cut for each survey code */

    dn0=-(f0-m0)*num_completed_fields;
    dn1=-(f1-m1)*num_completed_fields;
    dn2=-(f2-m2)*num_completed_fields;


    while(dn0>0||dn1>0||dn2>0){

fprintf(stderr,"%d %d %d\n",dn0,dn1,dn2);fflush(stderr);

       /* find minimum priority for completed fields of each survey code */

       min_priority0=1e6;
       min_priority1=1e6;
       min_priority2=1e6;

   
       for(i=0;i<num_fields;i++){
#if 1
           if(sequence[i].n_done==sequence[i].n_required&&
                                     sequence[i].field_number>=0){
#else
           if(sequence[i].field_number>=0){
#endif


               if(sequence[i].survey_code==0&&min_priority0>priority[i]){
                   min_priority0=priority[i];
               }
               else if(sequence[i].survey_code==1&&min_priority1>priority[i]){
                   min_priority1=priority[i];
               }
               else if(sequence[i].survey_code==2&&min_priority2>priority[i]){
                   min_priority2=priority[i];
               }
           }
        }



        /* Go through completed fields. If the number to cut is positive,
            set the field_number to -1 (makes field unobservable) */

        for(i=0;i<num_fields;i++){
#if 1
            if(sequence[i].n_done==sequence[i].n_required&&
                                     sequence[i].field_number>=0){
#else
            if(sequence[i].field_number>=0){
#endif
                if(dn0>0&&sequence[i].survey_code==0&&
                               min_priority0==priority[i]){
                   sequence[i].field_number=-1;
		   dn0--;
                   if(i<num_fields-1&&paired_fields(sequence+i,sequence+i+1)&&
                          sequence[i+1].survey_code==0&&
                          sequence[i+1].field_number>0){
                       sequence[i+1].field_number=-1;
                       dn0--;
                   }
               }
                else if(dn1>0&&sequence[i].survey_code==1&&
                               min_priority1==priority[i]){
                   sequence[i].field_number=-1;
		   dn1--;
                   if(i<num_fields-1&&paired_fields(sequence+i,sequence+i+1)&&
                          sequence[i+1].survey_code==1&&
                          sequence[i+1].field_number>0){
                       sequence[i+1].field_number=-1;
                       dn1--;
                   }
               }
                else if(dn2>0&&sequence[i].survey_code==2&&
                               min_priority2==priority[i]){
                   sequence[i].field_number=-1;
		   dn2--;
                   if(i<num_fields-1&&paired_fields(sequence+i,sequence+i+1)&&
                          sequence[i+1].survey_code==2&&
                          sequence[i+1].field_number>0){
                       sequence[i+1].field_number=-1;
                       dn2--;
                   }
                }
                  
            }
        }


    }


    return(0);
}

/******************************************************************/

do_stop(double ut,Telescope_Status *status)
{

       stop_flag=1;

	if(stop_flag==0){
             return(-1);
        }
        else{
             return(0);
        }

}

/************************************************************/

do_stow(double ut,Telescope_Status *status)
{

       stow_flag=1;

	if(stow_flag==0){
             return(-1);
        }
        else{
             return(0);
        }

}

/************************************************************/

int close_files()
{
     if(verbose){
        fprintf(stderr,"close_files: closing open file pointers\n");
        fflush(stderr);
     }

     if(sequence_out!=NULL)fclose(sequence_out);
       
     return(0);
}

/************************************************************/

int do_exit(int code)
{
     close_files();

     if(verbose){
        fprintf(stderr,"do_exit: do_exiting\n");
        fflush(stderr);
     }

     exit(code);
}
/************************************************************/

int save_obs_record(Field *sequence, FILE *obs_record, int num_fields,
         struct tm *tm)
{
     char string[1024];

     /* rewind obs_record so that next write will be at start of file
        (overwriting previous records) */


     if(fseek(obs_record,0,SEEK_SET)!=0){
         fprintf(stderr,"save_obs_record: can't rewind obs_record\n");
         fflush(stderr);
         return(-1);
     }

     sprintf(string,"%d %d %d %d %d %d %d\n",
          num_fields,tm->tm_year,tm->tm_mon,tm->tm_mday,
          tm->tm_hour,tm->tm_min,tm->tm_sec);

     if(fwrite((void *)string,strlen(string),1,obs_record)!=1){
          fprintf(stderr,"save_obs_record: ERROR writing first line\n");
          fflush(stderr);
          return(-1);
     }

     if(fwrite((void *)sequence,sizeof(Field),
                    num_fields,obs_record)!=num_fields){
          fprintf(stderr,"save_obs_record: ERROR writing %d fields\n",
                   num_fields);
          fflush(stderr);
          return(-1);
     }
     
     return(0);

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

/* observe the next field. If it is a sky field, first point the 
   telescope. Then wait for the previous readout of the camera to
   complete. If the readout is bad, flag the previous exposure as
   bad so that it will be taken again later. Then take the next 
   exposure and return when the readout begins. Return -1 if
   telescope doesn't point or if camera doesn't respond correctly.
   Print diagnostic messages to stderr. Print the status of each
   field to output to serve as an exposure log. */

int observe_next_field(Field *sequence, int index, int index_prev,
        double jd, double *dt, Night_Times *nt,
	FILE *output,Telescope_Status *tel_status, Camera_Status *cam_status,
        Fits_Header *fits_header)
{
    double lst,ut,actual_expt,ha,ra_correction,dec_correction,ra,dec,dt1;
    struct timeval t0,t1,t2;
    struct tm tm;
    char string[1024],shutter_string[3],filename[1024],field_description[1024];
    Field *f,*f_prev;
    double focus;
    double ra_dither,dec_dither;
    int n_clears;

    ra_dither=0.0;
    dec_dither=0.0;
    ha=0.0;

    f=sequence+index;
    if(index_prev>=0){
       f_prev=sequence+index_prev;
    }
    else{
       f_prev=NULL;
    }

    gettimeofday(&t0,NULL);

    get_shutter_string(shutter_string,f->shutter,field_description); 
    if(verbose){
         fprintf(stderr,"observe_next_field: observing %s, field %d iteration %d\n",
		field_description, f->field_number,f->n_done+1);
         fflush(stderr);
    }

    ut=nt->ut_start+(jd-nt->jd_start)*SIDEREAL_DAY_IN_HOURS;
    lst=nt->lst_start+(jd-nt->jd_start)*24.0;


    /* on first observation of focus sequence, set ra to lst - 1 hour
       and dec to 0 deg. On all other iterations, set to same ra and
       dec as first iteration 
    */

    if(f->shutter==FOCUS_CODE){
       if(f->n_done==0){
	  ha=1.0;
          f->ra=lst-1.0;
          f->dec=0.0;
          fprintf(stderr,
	     "observe_next_field: Pointing %s at %12.6f %12.5f\n",
	     field_description,f->ra,f->dec);
       }
    }

   /* set offset position to lst-1 hour, dec = 0  on first iteration*/

    else if(f->shutter==OFFSET_CODE){
        if(f->n_done==0){
	    ha=1.0;
            f->ra=lst-1.0;
            if(f->ra<0)f->ra=f->ra+24.0;
            f->dec=0.0;
            fprintf(stderr,
	       "observe_next_field: Pointing %s at %12.6f %12.5f\n",
	       field_description, f->ra,f->dec);
        }
    }

   /* set evening flat position to lst+3 hour, dec = 0  on first iteration*/

    else if (f->shutter==EVENING_FLAT_CODE){
        if(f->n_done==0){
	    ha=-3.0;
            f->ra=lst+3.0;
            if(f->ra>24.0)f->ra=f->ra-24.0;
            f->dec=0.0;
            fprintf(stderr,
	       "observe_next_field: Pointing %s at %12.6f %12.5f\n",
	       field_description, f->ra,f->dec);
        }
    }

   /* set morning flat position to lst-3 hour, dec = 0  on first iteration*/

    else if (f->shutter==MORNING_FLAT_CODE){
        if(f->n_done==0){
	    ha=3.0;
            f->ra=lst-4.0;
            if(f->ra<0.0)f->ra=f->ra+24.0;
            f->dec=0.0;
            fprintf(stderr,
	       "observe_next_field: Pointing %s at %12.6f %12.5f\n",
	       field_description, f->ra,f->dec);
        }
    }

    /* if this is a repeat observation of a sky field, compute the ra and
       dec corrections to the pointing given hour angle of initial
       observation and hour angle of the pending observation */

    else if (f->shutter==SKY_CODE){ 

        ha=lst-f->ra; /* current hour angle of field */
        if(ha<-12)ha=ha+24.0;
        if(ha>12)ha=ha-24.0;

        if(f->n_done>0&& POINTING_CORRECTIONS_ON){
            ra_correction=get_ra_correction(f->ha[0],ha);
            dec_correction=get_dec_correction(f->ha[0],ha);
        }
        else{
            ra_correction=0.0;
            dec_correction=0.0;
        }
    }

    *dt=f->expt+EXPOSURE_OVERHEAD;
    if(f->shutter==FOCUS_CODE)*dt=*dt+FOCUS_OVERHEAD;
    tm.tm_year=0;
    tm.tm_mon=0;
    tm.tm_mday=0;
    tm.tm_hour=ut;
    tm.tm_min=(ut-tm.tm_hour)*60.0;
    tm.tm_sec=(ut-tm.tm_hour-(tm.tm_min/60.0))*3600.0;

    get_filename(filename,&tm,f->shutter);

    /* update n_done, lst_next, and compute dt */

    f->ut[f->n_done]=ut;
    f->jd[f->n_done]=jd;
    f->ha[f->n_done]=ha;
    f->lst[f->n_done]=lst;
    f->actual_expt[f->n_done]=actual_expt/3600.0;
    strncpy(f->filename+(f->n_done)*FILENAME_LENGTH,filename,FILENAME_LENGTH);
    f->n_done=f->n_done+1;
    f->jd_next=jd+(f->interval/SIDEREAL_DAY_IN_HOURS);
 
#if 0
    fprintf(stderr,
        "UT : %10.6f Exposed field : %d  RA: %9.6f  Dec: %9.5f n_done: %d n_wanted: %d time_left : %7.3f jd_next : %7.3f  description: %s\n",

         ut, f->field_number, f->ra, f->dec, f->n_done, 
         f->n_required, f->time_left, f->jd_next, field_description);
#endif
    get_shutter_string(shutter_string,f->shutter,field_description); 

    if(output!=NULL){
        fprintf(output,"%10.6f %10.6f %s %d %6.1f %7.3f %11.6f %7.3f %s # %s %d",
                f->ra,f->dec,shutter_string,f->n_done,3600.0*f->expt,
                ut,jd,actual_expt,filename,field_description,f->field_number);
        if(strstr(f->script_line,"#")!=NULL){
           fprintf(output,"%s",strstr(f->script_line,"#")+1);
        }
        else{
           fprintf(output,"\n");
        }
        fflush(output);
    }

    return(0);
}

/************************************************************/

/* return offsetin ra and dec (deg) appropriate for dithering flat fields.
   The offset is on a grid with spacing DITHER_SPACING, centered on
   the nominal pointing of the telescope.

   Divide the grid into squares, concentric on the nominal pointing.

   On iterations 1 to 8, step along position of the smallest square (side length 3).
   On iterations 9 to 24, step along the next biggest square (side length 5).

*/

int get_flat_dither(int iteration, double *ra_dither, double *dec_dither)
{
    int i,square_size,i0,side,step_a,step_b;


    if (iteration == 0){
         *ra_dither=0.0;
         *dec_dither=0.0;
         return(0);
    }

    else if (iteration <= 8){ 
        square_size=3;
        i0=1;
    }
    else if (iteration <= 24 ){ 
       square_size=5;
       i0=9;
    }
    else if (iteration <= 48 ){ 
       square_size=7;
       i0=25;
    }
    else if (iteration <= 80 ){
       square_size=9;
       i0=49;
    }
    else if (iteration <= 120 ){
       square_size=11;
       i0=81;
    }
    else{
       fprintf(stderr,"get_flat_dither: too many iterations: %d\n",iteration);
       *ra_dither=0;
       *dec_dither=0;
       return(-1);
    }

    i=iteration-i0;
    side=i/(square_size-1);
    step_a=square_size/2;
    step_b=i-side*(square_size-1);


    if(side==0){
       *ra_dither=step_a*FLAT_DITHER_STEP;
       *dec_dither=(step_b-step_a)*FLAT_DITHER_STEP;
    }
    else if (side==1){
       *ra_dither=(step_b-step_a+1)*FLAT_DITHER_STEP;
       *dec_dither=step_a*FLAT_DITHER_STEP;
    }
    else if (side==2){
       *ra_dither=-step_a*FLAT_DITHER_STEP;
       *dec_dither=(step_b-step_a+1)*FLAT_DITHER_STEP;
    }
    else{
       *ra_dither=(step_b-step_a)*FLAT_DITHER_STEP;
       *dec_dither=-step_a*FLAT_DITHER_STEP;
    }
     
    return(0);
}

/************************************************************/

int get_shutter_code(char *shutter_flag)
{
          int code;

          if(strcmp(shutter_flag,SKY_STRING)==0){
            code=SKY_CODE;
          }
          else if(strcmp(shutter_flag,SKY_STRING_LC)==0){
            code=SKY_CODE;
          }
          else if (strcmp(shutter_flag,DARK_STRING)==0){
            code=DARK_CODE;
          }
          else if (strcmp(shutter_flag,DARK_STRING_LC)==0){
            code=DARK_CODE;
          }
          else if (strcmp(shutter_flag,FOCUS_STRING)==0){
            code=FOCUS_CODE;
          }
          else if (strcmp(shutter_flag,FOCUS_STRING_LC)==0){
            code=FOCUS_CODE;
          }
          else if (strcmp(shutter_flag,OFFSET_STRING)==0){
            code=OFFSET_CODE;
          }
          else if (strcmp(shutter_flag,OFFSET_STRING_LC)==0){
            code=OFFSET_CODE;
          }
          else if (strcmp(shutter_flag,EVENING_FLAT_STRING)==0){
            code=EVENING_FLAT_CODE;
          }
          else if (strcmp(shutter_flag,EVENING_FLAT_STRING_LC)==0){
            code=EVENING_FLAT_CODE;
          }
          else if (strcmp(shutter_flag,MORNING_FLAT_STRING)==0){
            code=MORNING_FLAT_CODE;
          }
          else if (strcmp(shutter_flag,MORNING_FLAT_STRING_LC)==0){
            code=MORNING_FLAT_CODE;
          }
          else if (strcmp(shutter_flag,DOME_FLAT_STRING)==0){
            code=DOME_FLAT_CODE;
          }
          else if (strcmp(shutter_flag,DOME_FLAT_STRING_LC)==0){
            code=DOME_FLAT_CODE;
          }
          else{
            code=BAD_CODE;
          }

          return(code);
}

/************************************************************/

int get_shutter_string(char *string, int shutter, char *description)
{
    switch (shutter){

    case BAD_CODE:
          strcpy(string,BAD_STRING_LC);
          sprintf(description,BAD_FIELD_TYPE);
          break;

    case DARK_CODE:
          strcpy(string,DARK_STRING_LC);
          sprintf(description,DARK_FIELD_TYPE);
          break;

    case SKY_CODE:
          strcpy(string,SKY_STRING_LC);
          sprintf(description,SKY_FIELD_TYPE);
          break;

    case FOCUS_CODE:
          strcpy(string,FOCUS_STRING_LC);
          sprintf(description,FOCUS_FIELD_TYPE);
          break;

    case OFFSET_CODE:
          strcpy(string,OFFSET_STRING_LC);
          sprintf(description,OFFSET_FIELD_TYPE);
          break;

    case EVENING_FLAT_CODE:
          strcpy(string,EVENING_FLAT_STRING_LC);
          sprintf(description,EVENING_FLAT_TYPE);
          break;

    case MORNING_FLAT_CODE:
          strcpy(string,MORNING_FLAT_STRING_LC);
          sprintf(description,MORNING_FLAT_TYPE);
          break;

    case DOME_FLAT_CODE:
          strcpy(string,DOME_FLAT_STRING_LC);
          sprintf(description,DOME_FLAT_TYPE);
          break;

    default:
          fprintf(stderr,"get_shutter_string: unrecognized shutter code: %d\n",shutter);
          sprintf(description,BAD_FIELD_TYPE);
          strcpy(string,BAD_STRING_LC);
          return(-1);
    }

    return(0);
}

/************************************************************/

/* Choose the next field to observe.

   This is a two pass selection loop. In the first pass, consider 
   every field. Call update_field_status to determine if the
   field is observable, ready to be observed, and how much time 
   is left to observe it. If there is a field with DO_NOW_STATUS 
   (e.g. a dark) , choose that field and return with its field index. 
   Otherwise, keep track of how many fields have READY_STATUS
   and TOO_LATE_STATUS. For those with READY_STATUS,
   update the minimum value of n_left (number of fields remaining 
   for completion).

   Before starting the second pass, check if the previously
   observed field was the first in a pair and if the
   second in the pair is ready to be observed. If so, choose
   the second in the pair as the next field.

   In the second pass, find all the fields that are ready to observe
   (READY_STATUS) and have n_left matching the minimum value
   determined in the first pass. Of these, select the field
   that has the least time remaining (time_left) to complete the
   required observations.

   If there are no fields ready to observe, choose among the "late"
   fields (TOO_LATE_STATUS) which are observable, but for which
   there is not enough time to
   observe all the remaining observations. Choose the late field with
   the most amount of time left (least negative value) and shorten
   the time interval between fields so that there is time left. Choose
   this field.

   If there are not late fields that can be shortened (while still
   keeping interval > MIN_INTERVAL), return -1 (no field selected).

*/

int get_next_field(Field *sequence,int num_fields, int i_prev,
				double jd, int bad_weather)
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

         
         update_field_status(f,jd,bad_weather);

         /* status of DO_NOW_STATUS  means must do now (i.e. darks or 2nd offset
            field).  Return field index */

         if(f->status==DO_NOW_STATUS){
            return(i);
         }

         /* for and field with READY_STATUS, increment the count and update minimum
            value of n_left */

         else if (f->status==READY_STATUS){
	    n_ready++;
            n_left=f->n_required-f->n_done;
            
            if(n_left<n_left_min){
                 n_left_min=n_left;
            }

         }

         /* Also count fields with TOO_LATE_STATUS */

         else if (f->status==TOO_LATE_STATUS){
            n_late++;
         }

     }
  
     /* if the pair to the previous fields is ready to observed,
        this is first pick */

#if 0
     if(f_next!=NULL&&f_next->status==READY_STATUS&&paired_fields(f_next,f_prev)){
        return(i_prev+1);
     }
#else

     if(f_prev!=NULL&&paired_fields(f_next,f_prev)&&f_next->doable){
        if(verbose){
            fprintf(stderr,"get_next_field: field %d is paired with field %d \n",
                    i_prev+1,i_prev);
            fflush(stderr);
        }
        if(f_next->status==READY_STATUS){
            if(verbose){
                fprintf(stderr,"get_next_field: paired field %d is ready \n", i_prev+1);
                fflush(stderr);
            }
	    return(i_prev+1);
        }
        else if(f_next->status==TOO_LATE_STATUS){
          shorten_interval(f_next);
          update_field_status(f_next,jd,bad_weather);
          if(f_next->status==READY_STATUS){
             if(verbose){
                fprintf(stderr,"get_next_field: paired field %d is too late\n", i_prev+1);
                fflush(stderr);
             }
             return(i_prev+1);
          }
          else{
             if(verbose){
                fprintf(stderr,"get_next_field: field %d is paired with field %d but not ready after interval shortened\n",
                    i_prev+1,i_prev);
             }
             return(i_prev+1);
          }
        }
        else{
             if(verbose){
                fprintf(stderr,
                  "get_next_field: field %d is paired with previous field %d but is neither ready nore too late\n",
                  i_prev+1,i_prev);
             }
             return(i_prev+1);
        }
     }
#endif

     /* Otherwise, if there are fields with READY_STATUS, choose the first one with
        MUSTDO survey code, or else choose the one 
        that has least time left to complete the required observations */

     else if(n_ready>0){

      
       time_left_min=10000.0;
       i_min=-1;

       for(i=0;i<num_fields;i++){
         f=sequence+i;
         if(f->status==READY_STATUS){
            if(f->survey_code==MUSTDO_SURVEY_CODE){
              return(i);
            }
            n_left=f->n_required-f->n_done;
            if(n_left==n_left_min&&f->time_left<time_left_min){
                 i_min=i;
                 time_left_min=f->time_left;
            }
         }
       }

       return(i_min);

     }

     /* If there are no observable fields ready to observe,  but there are
        fields with TOO_LATE_STATUS, choose the first one with MUSTDO_SURVEY_CODE, or
        else the field that has the most time left. Shorten the interval so 
        that time_left=0.  If still doable, choose this field. Otherwise return -1 */

     else if (n_late>0){

        if(verbose) {
          fprintf(stderr,"get_next_field: No fields ready. %d late fields\n",n_late);
          fprintf(stderr,"get_next_field: Choosing field to shorten interval\n");
        }

        i_max=-1;
        time_left_max=-1000;
        for(i=0;i<=num_fields;i++){
          f=sequence+i;
          if(f->status==TOO_LATE_STATUS&&f->time_left>time_left_max){
              if(f->survey_code==MUSTDO_SURVEY_CODE){
                  time_left_max=f->time_left;
                  i_max=i;
                  i=num_fields+1;
              }
              else{
                  time_left_max=f->time_left;
                  i_max=i;
              }
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
        update_field_status(f,jd,bad_weather);
        if(f->status==READY_STATUS){

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

    if(f1->shutter!=SKY_CODE||f2->shutter!=SKY_CODE)return(0);

    /*dra=1.1*RA_STEP0/cos(f1->dec*DEG_TO_RAD);*/
    dra=RA_STEP0/cos(f1->dec*DEG_TO_RAD);

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

/* If not doable, already completed, or already set,  set doable flag to 0,
   set status to NO_DOABLE_STATUS and return NOT_DOABLE_STATUS. 

   If not yet risen or not yet ready to observe, don't change doable flag,
   but still set status to NO_DOABLE_STATUS and return NOT_DOABLE_STATUS. 

   For any dark, status to DO_NOW_STATUS and return status.

   For focus, or flat ready to observe, set status to DO_NOW_STATUS if
   the bad_weather flag is 0 and return status.

   For any other field,  update time_required, time_up , and time left.
   If time_left<0, keep doable = 1 but return TOO_LATE_STATUS.
   Otherwise, this is field is observable. Return READY_STATUS 

*/


int update_field_status(Field *f, double jd, int bad_weather)
{

      /* Isn't doable */
      if(f->doable==0){
	  f->status=NOT_DOABLE_STATUS;
          return(NOT_DOABLE_STATUS);
      }

      /* Has been completed. */

      else if(f->n_done==f->n_required){
         f->doable=0; 
         f->status=NOT_DOABLE_STATUS;
         return(NOT_DOABLE_STATUS);
      }
        
      /* hasn't risen yet */

      else if(jd<f->jd_rise){
         f->status=NOT_DOABLE_STATUS;
         return(NOT_DOABLE_STATUS);
      }

      /* has already set 0 */

      else if(jd>f->jd_set){
         f->doable=0;
         f->status=NOT_DOABLE_STATUS;
         return(NOT_DOABLE_STATUS);
      }

      /* not yet time to reobserve */

      else if (f->jd_next-jd>(MIN_EXECUTION_TIME/SIDEREAL_DAY_IN_HOURS)){
         f->status=NOT_DOABLE_STATUS;
         return(NOT_DOABLE_STATUS);
      }


      /* darks  and domes that  are ready to
         observe get highest priority (DO_NOW_STATUS) */

      else if (f->shutter==DARK_CODE||f->shutter==DOME_FLAT_CODE){
        f->status=DO_NOW_STATUS;
        return(DO_NOW_STATUS);
      }


      /* sky flats, focus and pointing_offsets fields  that are ready to
         observe get highest priority (DO_NOW_STATUS)  as long as bad_weather flag is 0*/

      else if (f->shutter==EVENING_FLAT_CODE||
		f->shutter==MORNING_FLAT_CODE||f->shutter==FOCUS_CODE||
                f->shutter==OFFSET_CODE){
        if( !bad_weather){
           f->status=DO_NOW_STATUS;
           return(DO_NOW_STATUS);
        }
        else {
           f->status=NOT_DOABLE_STATUS;
           return(NOT_DOABLE_STATUS);
        }
      }

      /* Any other field is either ready to observe (READY_STATUS) or
         too late to observe (TOO_LATE_STATUS ) */

      else{

      /* Update time_required, time_up , time left. */

        f->time_required=(f->n_required-f->n_done)*f->interval;

        /* time up is now to when the field sets */

        f->time_up=(f->jd_set-jd)*SIDEREAL_DAY_IN_HOURS; 

        /* time left is time up - time required to finish the
           observations.  */

        f->time_left=f->time_up-f->time_required; 
        
        if(f->time_left<0){
              f->status=TOO_LATE_STATUS;
              return(TOO_LATE_STATUS);
        }
        else{
            f->status=READY_STATUS;
            return(READY_STATUS);
        }
      }
}

/************************************************************/

/* initialize rise and set times for each field. Set 0 values for time,
hour angle, and airmass of each oservation for each field. Set number
done to 0. Determine which observations are doable, and initialize
time_up, time_required, time_left, and jd_next */

int init_fields(Field *sequence, int num_fields, 
                Night_Times *nt, Night_Times *nt_5day,
                Night_Times *nt_10day, Night_Times *nt_15day,
                Site_Params *site, double jd)

{
    int i,j,n_observable,n_up_too_short,n_moon_too_close,n_never_rise;
    int n_moon_too_close_later;
    int n_same_ra;
    Field *f;
    double am,ha,time_up,time_required;
    double dark_night_duration, whole_night_duration;
    double ra_prev,current_epoch;
    double max_airmass,max_hourangle;

    n_up_too_short=0;
    n_moon_too_close=0;
    n_moon_too_close_later=0;
    n_never_rise=0;
    n_same_ra=1;
    ra_prev=0.0;
    
    /* if initializing fields after jd_start, set jd_start to
       current jd */

    if (jd > nt->jd_start){
        if(verbose){
            fprintf(stderr,
                    "adjusting jd_start from %7.3f to %7.3f\n",
                    nt->jd_start-2450000,jd-2450000);
        }
        nt->jd_start=jd;
    }

    /* calculate length of dark night remaining, and length of
       whole night remaining (time until sunrise) */

    dark_night_duration=(nt->jd_end-nt->jd_start)*SIDEREAL_DAY_IN_HOURS;
    whole_night_duration=(nt->jd_sunrise-jd)*SIDEREAL_DAY_IN_HOURS;

    if(verbose){
         fprintf(stderr,"jd_start: %7.3f\n",nt->jd_start-2450000);
         fprintf(stderr,"jd_end: %7.3f\n",nt->jd_end-2450000);
         fprintf(stderr,"dark night duration : %7.3f\n",dark_night_duration);
         fprintf(stderr,"whole night duration : %7.3f\n",whole_night_duration);
    }

    n_observable=0;
    for (i=0;i<num_fields;i++){

        f=sequence+i;
        f->n_done=0;
        f->status=0;
        for(j=0;j<f->n_required;j++){
           f->ut[j]=0.0;
           f->jd[j]=0.0;
           f->lst[j]=0.0;
           f->ha[j]=0.0;
           f->am[j]=0.0;
        }

        /* get rise and set times of given position (the jd when the
           airmass crosses below and above the maximum airmass, MAX_AIRMASS). 
           If the object is already up at the start of the observing window
           (nt.jd_start), set jd_rise to nt.jd_start. If it is up at the 
           end of the observing window (nt.jd_end), set the jd_set to 
           nt.jd_end. If the object is not up at all during the observing 
           window, set jd_rise and jd_set to -1.  These are irrelevant
           for darks, flats, focus fields, and offset pointing */

        if(f->dec<=-27.000){
             max_airmass=2.2;
        }
        else{
             max_airmass=MAX_AIRMASS;
        }
        max_hourangle = MAX_HOURANGLE;
        f->jd_rise=get_jd_rise_time(f->ra,f->dec,max_airmass,
		max_hourangle,nt,site,&am,&ha);
        f->jd_set=get_jd_set_time(f->ra,f->dec,max_airmass,
		max_hourangle,nt,site,&am,&ha);
        
        galact(f->ra,f->dec,2000.0,&(f->gal_long),&(f->gal_lat));
        eclipt(f->ra,f->dec,2000.0,nt->jd_start,&(f->epoch),&(f->ecl_long),&(f->ecl_lat));

        /* calculate total time object will be observable (jd_set-jd_rise)
           and the total time required to make all the observations. Again,
           these are irrelevant for darks, flats, focus fields, and offset fields */

        f->time_up=(f->jd_set-f->jd_rise)*SIDEREAL_DAY_IN_HOURS;
        f->time_required=(f->n_required-1)*f->interval;
        f->time_left=f->time_up-f->time_required; 

        /* Determine if the observation is doable (i.e. it rises during the
           night and there is enough time to do all the observations while
           it is up). If so, set doable=1 and initialize the jd_next to the
           earliest possible time to observe the position (either jd_start or
           jd_rise).  If not, set doable=0 and  jd_next = -1. */

        /* if field number is negative, this field has been eliminated from
           the list. Set doable to 0 */

        if(f->field_number<0){
            f->doable=0;
        }

        /* darks and dome flats are always doable.  Set time left to whole 
           night's duration, jd_next and jd_rise to sunset, and jd_set to
           sunrise .*/
        else if(f->shutter==DARK_CODE||f->shutter==DOME_FLAT_CODE){
            n_observable++;
            f->doable=1;

            f->time_left=whole_night_duration;
	    f->time_up=whole_night_duration;
            f->jd_next=jd;
            f->jd_rise=jd;
            f->jd_set=nt->jd_sunrise;

            if(verbose){
               if(f->shutter==DARK_CODE){
                   fprintf(stderr,"field: %d %7.3f %7.3f darks\n",
			f->field_number,f->ra,f->dec);
               }
               else if(f->shutter==DOME_FLAT_CODE){
                   fprintf(stderr,"field: %d %7.3f %7.3f dome flat\n",
			f->field_number,f->ra,f->dec);
               }
            }
        }

         /* offset pointing and focus can be done anytime it is dark */
        else if(f->shutter==FOCUS_CODE||f->shutter==OFFSET_CODE){
             if(jd<nt->jd_end){
                 n_observable++;
                 f->doable=1;

                 f->time_left=dark_night_duration;
	         f->time_up=dark_night_duration;
                 f->jd_next=nt->jd_start;
                 f->jd_rise=nt->jd_start;
                 f->jd_set=nt->jd_end;

                 if(verbose){
                    if(f->shutter==FOCUS_CODE){
                        fprintf(stderr,"field: %d %7.3f %7.3f focus\n",
			f->field_number,f->ra,f->dec);
                    }
                    else if(f->shutter==OFFSET_CODE){
                        fprintf(stderr,"field: %d %7.3f %7.3f offset\n",
			f->field_number,f->ra,f->dec);
                    }
                 }
             }
             else{
                 if(verbose){
                    fprintf(stderr,"skipping field %d, morning twilight has started\n",
                    f->field_number);
                 }
             }
        }

        /* evening flats can only be done after sunset (sunset + SKYFLAT_WAIT_TIME) and i
           before jd_start */
 
        else if (f->shutter==EVENING_FLAT_CODE){
             if(jd<nt->jd_start){
                 n_observable++;
                 f->doable=1;
                 f->jd_rise=nt->jd_sunset+SKYFLAT_WAIT_TIME;
                 if(jd>f->jd_rise){
                    f->jd_next=jd;
                    f->jd_rise=jd;
                 }
                 else{
                    f->jd_next=f->jd_rise;
                 }
                 f->jd_set=nt->jd_start;
                 f->time_up=(f->jd_set-f->jd_next)*SIDEREAL_DAY_IN_HOURS;
	         f->time_left=(f->jd_set-jd)*SIDEREAL_DAY_IN_HOURS;

                 if(verbose){
                    fprintf(stderr,"field: %d %7.3f %7.3f evening flat\n",
		    f->field_number,f->ra,f->dec);
                 }
             }
             else{
                 if(verbose){
                    fprintf(stderr,"skipping field %d, evening twilight has ended\n",
                    f->field_number);
                 }
             }
        }

        /* morning flats can only be done after jd_end and before sunrise - SKYFLAT_WAIT_TIME  */
 
        else if (f->shutter==MORNING_FLAT_CODE){
             if(jd<nt->jd_sunrise-SKYFLAT_WAIT_TIME){
                 n_observable++;
                 f->doable=1;
                 f->jd_set = nt->jd_sunrise-SKYFLAT_WAIT_TIME;
                 if(jd>nt->jd_end){
                     f->jd_next=jd;
                     f->jd_rise=jd;
	             f->time_up=(f->jd_set-jd)*SIDEREAL_DAY_IN_HOURS;
                     f->time_left=f->time_up;
                 }
                 else{
	             f->time_up=(f->jd_set-nt->jd_end)*SIDEREAL_DAY_IN_HOURS;
                     f->jd_next=nt->jd_end;
                     f->jd_rise=nt->jd_end;
                     f->time_left=(f->jd_set-jd)*SIDEREAL_DAY_IN_HOURS;
                 }

                 if(verbose){
                    fprintf(stderr,"field: %d %7.3f %7.3f morning flat\n",
		    f->field_number,f->ra,f->dec);
                 }
              }
              else{
                 if(verbose){
                    fprintf(stderr,"skipping field %d, morning twilight has ended\n",
                    f->field_number);
                 }
              }
        }

        /* never rises. Not doable */

        else if(f->jd_rise<0){ 
           n_never_rise++;
           f->doable=0;
           f->jd_next=-1;
           f->time_left=-1;

           if(verbose)fprintf(stderr,"field: %d %7.3f %7.3f never rises\n",
			f->field_number,f->ra,f->dec);

        }

       /* Field too close to tonight's moon. Not doable */
        else if (moon_interference(f,nt,MIN_MOON_SEPARATION)){
            n_moon_too_close++;
            f->doable=0;
            f->jd_next=-1;

            if(verbose)fprintf(stderr,
		"field: %d %7.3f %7.3f moon too close\n",
		f->field_number,f->ra,f->dec);
 
        }

        else if (f->dec>MAX_DEC){
           f->doable=0;
           if(verbose)fprintf(stderr,"field: %d %7.3f %7.3f dec too high\n",
		f->field_number,f->ra,f->dec);
        }

        else if (f->dec<MIN_DEC){
           f->doable=0;
           if(verbose)fprintf(stderr,"field: %d %7.3f %7.3f dec too high\n",
		f->field_number,f->ra,f->dec);
        }

        /* not enough time for required obs. Not doable (unless it is a must do field) */
        else if(f->survey_code!=MUSTDO_SURVEY_CODE&&f->time_left<0){ 
           n_up_too_short++;
           f->doable=0;
           f->jd_next=-1;

           if(verbose)fprintf(stderr,"field: %d %7.3f %7.3f up too short\n",
		f->field_number,f->ra,f->dec);

        }

        /* Doable */
        else{ 
           n_observable++;
           f->doable=1;

           /* if position has already risen, set jd_next to jd_start.
              Otherwise set jd_next to jd_rise */

           if(jd>f->jd_rise){
               f->jd_next=nt->jd_start;
           }
           else{
               f->jd_next=f->jd_rise;
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
                      "%d field: %d %7.3f %7.3f %7.3f %d rise: %9.6f  set: %9.6f next: %9.6f  time_up : %7.3f time_required: %7.3f time_left: %7.3f survey_code: %d\n",
		      f->doable,f->field_number,f->ra,f->dec,
                      f->expt,f->shutter,f->jd_rise-2450000,f->jd_set-2450000,
		      f->jd_next-2450000,f->time_up, f->time_required, f->time_left,
                      f->survey_code);
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

int moon_interference(Field *f, Night_Times *nt, double min_separation)
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

	if(dmoon<min_separation)return(1);
     }
 
     return(0);
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

int new_load_sequence(char *script_name, Field *sequence, double *priority)
{

    FILE *input;
    int n,n_fields,line,n1;
    char string[1024],*s_ptr,shutter_flag[3],s[256];
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

          n=sscanf(s_ptr,"%lf %lf %s %lf %lf %d %d %s %lf",
	    &(f->ra),&(f->dec),shutter_flag,&(f->expt),&(f->interval),
	    &(f->n_required),&(f->survey_code),s,priority+n_fields);

          f->interval=f->interval/3600.0;
          f->expt=f->expt/3600.0;

          f->shutter=get_shutter_code(shutter_flag);

          /* if this is a focus field, read the focus start, increment, and
             default setting from the script line */

          if(f->shutter==FOCUS_CODE){
             sscanf(s_ptr,"%s %s %s %s %s %s %s %lf %lf",
		s,s,s,s,s,s,s,&focus_increment,&focus_default);
             n1 = f->n_required/2;
             focus_start=focus_default-n1*focus_increment;
             if(verbose){
                fprintf(stderr,
			"load_sequence: focus start, incr, default: %8.5f %8.5f %8.5f\n",
			focus_start,focus_increment,focus_default);
               fflush(stderr);
             }
	        
          }

          /* if the focus parameters are out of range for a focus field
             skip the field */

	  if(f->shutter==FOCUS_CODE&&
             (focus_start<MIN_FOCUS||focus_increment<MIN_FOCUS_INCREMENT||
                focus_start>MAX_FOCUS||focus_increment>MAX_FOCUS_INCREMENT||
                focus_start+(f->n_required*focus_increment)>MAX_FOCUS)){
             fprintf(stderr,"focus parameters out of range: %s",s_ptr);
          }
          
           /* Also make sure 6 parameters are read from the line, and that
             the parameters are within range. If not, skip this field. */

          else if(n!=9||f->ra<0.0||f->ra>24.0||f->dec<-90.0||f->dec>90.0||
                f->expt>MAX_EXPT||f->expt<0||
                f->interval>MAX_INTERVAL||f->interval<MIN_INTERVAL||f->n_required<1||
                f->n_required>MAX_OBS_PER_FIELD||f->shutter==BAD_CODE||
                f->survey_code<MIN_SURVEY_CODE||f->survey_code>MAX_SURVEY_CODE){
             fprintf(stderr,"load_sequence: bad field line %d: %s\n",
                line,string);
          }

          /* Accept the field */

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
   
int print_history(double jd, Field *sequence, int num_fields,FILE *output)
{
    int i;
    Field *f;
    char string[1024];
   
    sprintf(string,"%12.6f ",jd-2450000);
    for(i=0;i<num_fields;i++){
       f=sequence+i;
       if (f->n_done==f->n_required){
         sprintf(string+strlen(string),"%s",".");
       }
       else{
         sprintf(string+strlen(string),"%d",f->n_done);
       }
    }

    sprintf(string+strlen(string),"\n");

    fprintf(output,"%s",string);
    fflush(output);

    return(0);
}
/************************************************************/
 
int check_weather (FILE *input, double jd, struct date_time *date, Night_Times *nt)
{
     char string[1024],s[256];
     double t_obs,t_on,duration,ut;
     int year, mon, day, doy;
 
     /* ut date is 1 + doy since get_day_of_year takes local time */
     doy=1+get_day_of_year (date);
 
     /* ut values don't go past 24 hours at ESO La SIlla at night */
     ut=nt->ut_start+(jd-nt->jd_start)*SIDEREAL_DAY_IN_HOURS;
 
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

double get_jd_rise_time(double ra,double dec, double max_am, double max_ha,
       Night_Times *nt, Site_Params *site, double *am, double *ha)
{
    double lst,jd;

    /* get lst, hour angle, jd, and airmass at start time of observing window.
       If am is less than max_am and absolute value of ha is less than max_am, 
       object is already up. Return with jd.
       Otherwise, stepwise search from jd_start to jd_end to find the 
       time when am < max_am and fabs(ha) < max_ha (the rise time)
       If it never rises, return -1. Otherwise return the jd when it does.
    */

    lst=nt->lst_start;
    jd=nt->jd_start;
    *ha=get_ha(ra,lst);
    *am=get_airmass(*ha,dec,site);

    if (*am < max_am && fabs (*ha) < max_ha ) return (jd);

     
    while(jd<nt->jd_end&&(*am>max_am||fabs(*ha)>max_ha)){
          jd=jd+(LST_SEARCH_INCREMENT/SIDEREAL_DAY_IN_HOURS);
          lst=lst+LST_SEARCH_INCREMENT;
          if(lst>24.0)lst=lst-24.0;
          *ha=get_ha(ra,lst);
          *am=get_airmass(*ha,dec,site);
    }

    if(*am>max_am){
       return(-1.0);
    }
    else if(fabs(*ha)>max_ha){
       return(-1.0);
    }
    else{
       return(jd);
    }
     
    
}

/************************************************************/


double get_jd_set_time(double ra,double dec, double max_am, double max_ha,
       Night_Times *nt, Site_Params *site, double *am, double *ha)
{
    double lst,jd;

    /* get lst, hour angle, jd, and airmass at end time of observing window.
       If am is less than max_am and absolute value of ha is less than
       max_ha, object is still up. Return with jd.
       Otherwise, stepwise search from jd_end to jd_start to find the 
       set_time. If it sets before lst_start, return -1. 
       Otherwise return the jd when the am gets above max_am or 
       the ha gets above max_ha*/


    lst=nt->lst_end;
    jd=nt->jd_end;
    *ha=get_ha(ra,lst);
    *am=get_airmass(*ha,dec,site);

    if (*am < max_am && fabs (*ha) < max_ha ) return (jd);

   
    while(jd>nt->jd_start&&(*am>max_am||fabs(*ha)>max_ha)){
          lst=lst-LST_SEARCH_INCREMENT;
          jd=jd-(LST_SEARCH_INCREMENT/SIDEREAL_DAY_IN_HOURS);
          if(lst<0.0)lst=lst+24.0;
          *ha=get_ha(ra,lst);
          *am=get_airmass(*ha,dec,site);
    }

    if(*am>max_am){
       return(-1.0);
    }
    else if(fabs(*ha)>max_ha){
       return(-1.0);
    }
    else{
       return(jd);
    }
     
    
}
/************************************************************/
/*************************************************************************/

