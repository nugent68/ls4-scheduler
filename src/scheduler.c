/* scheduler.c

   DLR 2007 Feb 7

   Read in a sequence of observations (NEAT format). Send commands
   to the telescope and camera as necessary to expose the
   fields at the specified interval. Impose airmass constraints
   for all fields, skipping fields that are not below the
   maximum airmass for the required time to complete the exposures.
   Impose contraints on moon proximity for supernova fields and
   skip those failing that constraint.

   Add any given time, choose the next field to observe by priority,
   where darks get first priority. Fields with the fewest number of
   observations left to completion and the least time remaining
   to complete these observations get second priority. Third
   priority goes to fields that can not be completed in the time
   remaining unless the interval between observation is shortened. 

   Repeat observations of fields for which there is a bad camera
   readout.

   As each field is completed, print out a log of the observation 
   and an ASCII chart indicating the completion status
   of all fields (history file) in the sequence.

   Set FAKE_RUN=1 (scheduler.h) to simulate observations, with 
   optional name of weather file on command line (weather file 
   lists when dome is open and closed during the night).
  

   syntax: scheduler sequence_file yyyy mm dd verbose_flag

   where yyyy mm dd is the local time date.


   The format for a NEAT sequence file is:

# ra    dec    Shutter Expt Interval Nobs 
#
 0.000000  0.000000 N   60.00 9600.0 15   # darks
 3.424352  14.300000 Y  60.00  1800.0 3
 8.35036  13.62647 Y  60.00 1800.0 3    # QJ126C4 02/05 0.51 fu field
...

 Optional Shutter Flags:

 N  Dark exposure
 Y  Sky Exposure
 F  Focus sequence (with start, step, and default focus settings on command line
 O  Telescope Offset Exposure
 T  Twilight Flat


*/

#include "scheduler.h"
#include <unistd.h>

// do not wait for readout of exposure before moving to next field.

#define WAIT_FLAG False
//#define DEBUG 1

#define LOOP_WAIT_SEC 10 /* seconds to wait between loops if no
               field selected */
int verbose=0;
int verbose1 = 0; /* set to 1 for very verbose */
int pause_flag=0;
int focus_done=0;
int offset_done=0;
int stop_flag=1; /* 1 for stopped, 0 for tracking */
int stow_flag=1; /* 1 for stowed, 0 for not stowed */
double focus_start=NOMINAL_FOCUS_START;
double focus_increment=NOMINAL_FOCUS_INCREMENT;
double focus_default=NOMINAL_FOCUS_DEFAULT;
FILE *hist_out,*sequence_out,*log_obs_out,*obs_record;
double ut_prev=0;
char filter_name[STR_BUF_LEN];
char *filter_name_ptr=0;

// global
char *host_name=NULL;

// NOTE: each element of selection string must correspond to an element of Selection_Code 
// defined in scheduler.h

char *selection_string[] = {"not selected", "first do_now flat", "first do_now dark",
       "first_do_now sky field", "first ready paired field", "first late paired field",
       "first not-ready late paired field", "first not-ready and not-late paired field",
       "late must-do field with least time left", "ready must-do field with least time left",
       "ready field with least time left", "late ready field with most time left"};

/************************************************************/
      
int main(int argc, char **argv)
{
    int done = 0;
    struct tm tm;
    double jd,ut;
    char string[STR_BUF_LEN];
    struct date_time date,date_5day,date_10day,date_15day;
    Night_Times nt; /* times of sunrise/set, moonrise/set, etc */
    Night_Times nt_5day; /* nt for 5 days later */
    Night_Times nt_10day; /* nt for 10 days later */
    Night_Times nt_15day; /* nt for 15 days later */
    char script_name[STR_BUF_LEN], new_script_name[STR_BUF_LEN];
    Field sequence[MAX_FIELDS],new_sequence[MAX_FIELDS];
    int i,num_fields,num_observable_fields,num_completed_fields;
    int num_new_fields, num_new_observable_fields, num_new_fields_prev;
    int i_prev,result;
    Site_Params site;
    Telescope_Status tel_status;
    Camera_Status cam_status;
    double dt;
    int telescope_ready,bad_weather,delay_sec;
    Fits_Header fits_header;
    char exp_mode[256];
    bool first_exposure = True;
    char code_string[1024];
    int selection_code;
    char site_name[1024];
#if FAKE_RUN
    FILE *weather_input;
#endif

    // initialize the site name from SITE_NAME environment variable. If
    // that is not set, then use "DEFAULT" for the site name.
    if ( getenv("SITE_NAME") != NULL){
      strcpy(site.site_name,getenv("SITE_NAME"));
    }
    else{
      fprintf(stderr,"WARNING: environment variable SITE_NAME is not set. Using DEFAULT\n");
      strcpy(site.site_name,"DEFAULT");
    } 

    init_status_names();

    init_semaphores();

#if FAKE_RUN    
    if(argc!=7&&argc!=6){
      fprintf(stderr,
        "syntax: scheduler sequence_file yyyy mm dd verbose_flag [weather_file] \n");
      do_exit(-1);
    }
    if(argc==7){
       weather_input=fopen(argv[6],"r");
       if(weather_input==NULL){
           fprintf(stderr,"can't open weather file %s\n",argv[6]);
           do_exit(-1);
       }
    }
    else{
       weather_input=NULL;
    }
#else
    if(argc!=6){
      fprintf(stderr,
        "syntax: scheduler sequence_file yyyy mm dd verbose_flag \n");
      do_exit(-1);
    }
#endif

    strcpy(script_name,argv[1]);
    sscanf(argv[2],"%hd",&(date.y));
    sscanf(argv[3],"%hd",&(date.mo));
    sscanf(argv[4],"%hd",&(date.d));
    sscanf(argv[5],"%d",&verbose);
    if (verbose > 1)verbose1=1;
    sprintf(new_script_name,"%s.add",script_name);
    fprintf(stderr,"new script name is %s\n",new_script_name);
    fflush(stderr);

    host_name = (char *)malloc(1024*sizeof(char));
    if(gethostname(host_name,1024) != 0){
      fprintf(stderr,"unable to get host name\n");;
      do_exit(-1); 
    }
    fprintf(stderr,"host_name is %s\n",host_name);

    num_new_fields_prev=0;
    filter_name_ptr=0;

    /* install signal handlers */

    if(install_signal_handlers()!=0){
       fprintf(stderr,"could not install signal handlers. Exitting\n");
       fflush(stderr);
       do_exit(-1);
    }

    /* intialize local time of start date to 0 h */
    date.h=0;
    date.mn=0;
    date.s=0;


    /* initialize fits header keywords and values */
    if(init_fits_header(&fits_header)!=0){
       fprintf(stderr,"could not init FITS words\n");
       do_exit(-1);
    }

    /* open history file */
 
    hist_out=fopen(HISTORY_FILE,"a");
    if(hist_out==NULL){
        fprintf(stderr,"can't open file %s for output\n",HISTORY_FILE);
        do_exit(-1);
    }     

    /* open sequence file */

    sequence_out=fopen(SELECTED_FIELDS_FILE,"a");
    if(sequence_out==NULL){
        fprintf(stderr,"can't open file %s for output\n",SELECTED_FIELDS_FILE);
        do_exit(-1);
    }     

    /* open log file */

    log_obs_out=fopen(LOG_OBS_FILE,"a");
    if(log_obs_out==NULL){
        fprintf(stderr,"can't open file %s for output\n",LOG_OBS_FILE);
        do_exit(-1);
    }

    /* if OBS_RECORD_FILE exists, then the scheduler1 is being restarted.
       Reain in sequence from the binary record. Otherwise, read in the
       read in a new sequence from the specified script */     

    if(verbose){
      fprintf(stderr,"loading obs_record from file %s\n",OBS_RECORD_FILE);
    }

    num_fields=load_obs_record(OBS_RECORD_FILE, sequence, &obs_record);

    if(num_fields < 0 ) {
      fprintf(stderr,"unable to load obs record. Exitting\n");
      do_exit(-1);
    }
    else if(num_fields >  0) {   
       fprintf(stderr,"Continuing observation of %d fields\n",num_fields);
       fflush(stderr);
    }
    else{
       fprintf(stderr,"No previous record of observations\n");
       fflush(stderr);
    }

    if(num_fields==0){
       if(verbose){
         fprintf(stderr,"loading sequence file %s\n",script_name);
       }

       num_fields=load_sequence(script_name,sequence);
       if (num_fields<1){
           fprintf(stderr,"Error loading script %s\n",script_name);
           do_exit(-1);
       }
 
       if(verbose){
          fprintf(stderr,
        "%d fields successfully loaded\n",num_fields);
       }
    }

    /* initialize site parameters for DEFAULT observatory (ESO La Silla) */

    //strcpy(site.site_name,"DEFAULT"); /* DEFAULT assigned to ESO La Silla */
    strcpy(site.site_name,"Fake"); /* Fake Site */
    if(verbose){
      fprintf(stderr,"loading info for site  %s\n",site.site_name);
    }
    load_site(&site.longit,&site.lat,&site.stdz,&site.use_dst,site.zone_name,&site.zabr,
            &site.elevsea,&site.elev,&site.horiz,site.site_name);

    if(verbose){
      fprintf(stderr,"# site: %s\n",site.site_name);
      fprintf(stderr,"# longitude: %10.6f\n",site.longit);
      fprintf(stderr,"# latitude: %10.6f\n",site.lat);
      fprintf(stderr,"# elevsea: %10.6f\n",site.elevsea);
      fprintf(stderr,"# elev: %10.6f\n",site.elev);
      fprintf(stderr,"# horiz: %10.6f\n",site.horiz);
    }


    /* get the date and the night_info for the current date and
       the current date plus 5, 10, and 15 days (to allow 
       check of moon proximity for sne followup) */

    /* get date for current date plus 5 days */
    date_5day=date;
    adjust_date(&date_5day,5);
    init_night(date_5day,&nt_5day,&site,0);

 
    /* get date for current date plus 10 days */
    date_10day=date;
    adjust_date(&date_10day,10);
    init_night(date_10day,&nt_10day,&site,0);


    /* get date for current date plus 15 days */
    date_15day=date;
    adjust_date(&date_15day,15);
    init_night(date_15day,&nt_15day,&site,0);

    /* initialize tonight */
    init_night(date,&nt,&site,1);

    if(verbose){
       fprintf(stderr,"# ut sunset : %10.6f\n",nt.ut_sunset); 
       fprintf(stderr,"# ut start  : %10.6f\n",nt.ut_start); 
       fprintf(stderr,"# ut end    : %10.6f\n",nt.ut_end); 
       fprintf(stderr,"# ut sunrise: %10.6f\n",nt.ut_sunrise); 
       fprintf(stderr,"# jd start  : %10.6f\n",nt.jd_start-2450000); 
       fprintf(stderr,"# jd end    : %10.6f\n",nt.jd_end-2450000); 
       fprintf(stderr,"# lst start : %10.6f\n",nt.lst_start); 
       fprintf(stderr,"# lst end   : %10.6f\n",nt.lst_end); 
       fprintf(stderr,"# moon ra   : %10.6f\n",nt.ra_moon); 
       fprintf(stderr,"# moon dec  : %10.6f\n",nt.dec_moon); 
       fprintf(stderr,"# moon frac : %10.6f\n",nt.percent_moon); 
       fprintf(stderr,"# moon ra5  : %10.6f\n",nt_5day.ra_moon); 
       fprintf(stderr,"# moon dec5 : %10.6f\n",nt_5day.dec_moon); 
       fprintf(stderr,"# moon frac5: %10.6f\n",
                         nt_5day.percent_moon); 
       fprintf(stderr,"# moon ra10 : %10.6f\n",nt_10day.ra_moon); 
       fprintf(stderr,"# moon dec10: %10.6f\n",nt_10day.dec_moon); 
       fprintf(stderr,"# moon frac10: %10.6f\n",
                       nt_10day.percent_moon); 
       fprintf(stderr,"# moon ra15 : %10.6f\n",nt_15day.ra_moon); 
       fprintf(stderr,"# moon dec15: %10.6f\n",nt_15day.dec_moon); 
       fprintf(stderr,"# moon frac15: %10.6f\n",
                       nt_15day.percent_moon); 
    }

#if FAKE_RUN
    stow_flag=0;
    telescope_ready=1;
#else
    /* Wait until after sunset */ 

    ut=get_ut();
    jd=get_jd();
    if(verbose){
      fprintf(stderr,"current ut,jd,ut_offset: %12.6f %12.6f %12.6f\n", ut,jd,UT_OFFSET );
    }

    while(jd<nt.jd_sunset){
       fprintf(stderr,
        "# UT: %9.5f UT_Start: %9.5f jd: %12.6f jd_sunset : %12.6fwaiting for sunset ...\n",
        ut,nt.ut_start,jd-2450000,nt.jd_sunset-2450000);
       fflush(stderr);
       sleep(60);
       ut=get_ut();
       jd=get_jd();
    }

    /* make sure sun has not already risen */

    if(jd>nt.jd_sunrise){
       fprintf(stderr,
           "# UT: %9.5f UT_End: %9.5f Sun is up. Exiting.\n",
        ut,nt.ut_end);
       fflush(stderr);
       do_exit(0);
    }

    fprintf(stderr,
        "# UT: %9.5f UT_Start: %9.5f Sun is down. Starting observing program\n",
        ut,nt.ut_start);
    fflush(stderr);


    /* Make sure the camera is responding. Exit if not */

    if(verbose){
      fprintf(stderr,"checking camera status\n");
      fflush(stderr);
    }

    if(update_camera_status(&cam_status)!=0){
        fprintf(stderr,
           "Error :  can't update camera status. Exiting\n");
        do_exit(-1);
    }

    /* Initialize the camera, and then print its status */

    if(verbose){
      fprintf(stderr,"initializing camera\n");
      fflush(stderr);
    }

#if 0
    // there is no init command for the LS4 camera
    if(init_camera()!=0){
       fprintf(stderr,"unable to initialize camera\n");
       do_exit(-1);
    }
#endif
    ut_prev=-1000.0;

    fprintf(stderr, "LS4 Camera Status:\n");
    print_camera_status(&cam_status,stderr);

    /* initialize telescope pointing offsets */
 
    if(verbose){
      fprintf(stderr,"initialzing default telescope offsets\n");
      fflush(stderr);
    }

    if(init_telescope_offsets(&tel_status)!=0){
         fprintf(stderr,"problem initializing default telescope offsets\n");
    }

    if(verbose){
      fprintf(stderr,"Default telescope offsets are %8.6f %8.6f\n",
        tel_status.ra_offset,tel_status.dec_offset);
      fflush(stderr);
    }

 

    /* On start up check the telescope status. If it cannot be 
       updated, this is probably because control has not
       yet been given to the computer, or because the dome has
       not yet been opened for the first time. Keep checking
       until the telescope reponds, or until the night ends */


    ut=get_ut();
    jd=get_jd();

    if(verbose){
      fprintf(stderr,"checking telescope status\n");
      fflush(stderr);
    }

    if(update_telescope_status(&tel_status)!=0){
        fprintf(stderr,
           "Telescope Status not yet available\n");
        telescope_ready=0;
    }
    else{
        print_telescope_status(&tel_status,stderr);
        telescope_ready=1;
    }
    print_telescope_status(&tel_status,stderr);

#endif 


    /* Now its time to start observing. Initialize the status of
       the fields given the current jd */

#if FAKE_RUN
    jd=nt.jd_sunset;
    ut=nt.ut_sunset;
#else
    ut=get_ut();
    jd=get_jd();
    if(verbose){
      fprintf(stderr,"current ut,jd: %12.6f %12.6f\n", ut,jd );
    }
#endif
    if(verbose){
      fprintf(stderr,"initialing sequence fields \n");
    }

    num_observable_fields=init_fields(sequence,num_fields,
               &nt,&nt_5day,&nt_10day,&nt_15day,&site,jd,&tel_status);


    fprintf(stderr,
          "# UT: %9.6f Starting observations\n",
            ut);
    fprintf(stderr,
          "# UT: %9.6f  %d fields are observable\n",
             ut,num_observable_fields);

    /* keep choosing next field, observing it,  and incrementing 
       jd accordingly until end of night or no more observations
       left  */

     
    i_prev=-1; /* initialize index of previously observed field to -1 */
    done = 0; /* intialize done flag to 0. On termination of evening,
             it will be set to 1 */

    /* Wait for sun to set or observations to end. */
 
    while(jd<nt.jd_sunrise && !done){

         bad_weather=0;
#if FAKE_RUN

         if(weather_input!=NULL&&
            check_weather(weather_input,jd,&date,&nt)!=0){
          bad_weather=1;
         }

#else

         /* update UT time and Julian Date */

         ut=get_tm(&tm);
         jd=get_jd();

#endif

         /* update sequence with latest additions from new_script_name */
         num_new_fields = 0;
#if FAKE_RUN

         if(access(new_script_name,F_OK) {
           num_new_fields = 0;
         }
         else if( jd>nt.jd_start + 0.1){
           if(verbose){
         fprintf(stderr,"# UT %9.5f : checking for new observations to add to sequence\n",ut);
           }

           num_new_fields=load_sequence(new_script_name,new_sequence);
         }
         else{
           num_new_fields=-1;
         }
#else

         if(verbose){
           fprintf(stderr,"# UT %9.5f : checking for new observations to add to sequence\n",ut);
         }

         // if file of new observations does not exist, just set num_new_fields to 0
         if(access(new_script_name,F_OK)!=0) {
           num_new_fields = 0;
         }
         // otherwise load any new fields
         else{
           num_new_fields=load_sequence(new_script_name,new_sequence);
         }
#endif

         if (num_new_fields<0){
           fprintf(stderr,"Error loading new observations from script %s\n",new_script_name);
         }
         else{
           if(verbose){
          if ( num_new_fields == 0 ){
            fprintf(stderr,"no new fields to read\n");
          }
          else if ( num_new_fields <= num_new_fields_prev ){
            fprintf(stderr,"no new fields: %d in file, %d already read\n",
            num_new_fields, num_new_fields_prev);
          }
          else{
            fprintf(stderr,
            "%d new fields to be added to queue\n",num_new_fields - num_new_fields_prev);
          }
          fflush(stderr);
           }
         }

         if(num_new_fields >   num_new_fields_prev){
        if (verbose ) {
           fprintf(stderr,"checking which new fields are observable\n");
           fflush(stderr);
        }
        num_new_observable_fields=init_fields(new_sequence+num_new_fields_prev,num_new_fields,
               &nt,&nt_5day,&nt_10day,&nt_15day,&site,jd,&tel_status);
        if ( num_new_observable_fields > 0 ) {
          fprintf(stderr,"Adding %d new fields to queue, of which %d are observable\n",
            num_new_fields,num_new_observable_fields);
          fflush(stderr);
          if (add_new_fields(sequence,num_fields,
            new_sequence+num_new_fields_prev,num_new_fields)==0){
             fprintf(stderr,"%d new fields succesfully added to queue\n",num_new_fields);
             num_fields = num_fields + num_new_fields;
          }
          else{
             fprintf(stderr,"ERROR : could not add new fields to queue\n");
             fflush(stderr);
          }
        }
        else{
          if (verbose) {
             fprintf(stderr,"no observable new fields\n");
             fflush(stderr);
          }
        }
        num_new_fields_prev = num_new_fields;
         }
#if FAKE_RUN
#else

         /* If pause flag is set (from signal handler) don't do
        anything but idle, waiting for pause_flag to be
        reset, or else the night to end */

         if(pause_flag){
        fprintf(stderr,
        "# UT : %9.6f Skipping Telescope check\n",ut);
         }

         /* Otherwise, update the telescope status. This will
        give dome and weather conditions .

        If telescope status unavailable, it could be a late
        start because of bad weather, or a communication fault
        with the telescope controller. In either case, just
        treat it is bad weather and keep checking until the
        status is available or the end of the night */


         else if(update_telescope_status(&tel_status)!=0){
        fprintf(stderr,
        "# UT : %9.6f Can't update telescope status \n",ut);
        bad_weather=1;
        telescope_ready=0;
         }

         else{
         telescope_ready=1;
         if(verbose)print_telescope_status(&tel_status,stderr);
         if(tel_status.dome_status!=1){
              bad_weather=1;
         }
         else {
              stow_flag=0; /* once dome is open, assume telescope 
                      in not stowed */
         }
         }

         /* if bad weather , stow the telescope if necessary. */

         /*if(telescope_ready&&bad_weather&&stow_flag==0){*/
         if(telescope_ready&&bad_weather&&stop_flag==0){

        if(verbose){
           fprintf(stderr,
            "# UT : %9.6f bad weather\n",ut);
        }
        if(verbose){
           fprintf(stderr,
            "# UT : %9.6f stowing telescope\n",ut);
        }

        /*if(do_stow(ut,&tel_status)!=0){*/
        if(do_stop(ut,&tel_status)!=0){
            fprintf(stderr,
            "# UT : %9.6f ERROR stowing telescope\n",ut);
        }
        /* don't try to stow again */
         }

#endif


         /* if pause flag, wait LOOP_WAIT_SEC seconds  before checking
        pause flag again. Also stop telescope if necessary. */

         if(pause_flag){

        if(verbose){
           fprintf(stderr,
            "# UT : %9.6f observations paused\n",ut);
        }

        /* If pause flag is set but telescope was not stopped,
           stop it now. Set stop_flag to indicate it has been
           stopped. If the weather is bad, stow the telescope. */


        if(telescope_ready){
              /*if(bad_weather&&stow_flag==0){*/
              if(bad_weather&&stop_flag==0){
               fprintf(stderr,
                   "# UT : %9.6f stowing telescope\n",ut);
               /*if(do_stow(ut,&tel_status)!=0){*/
               if(do_stow(ut,&tel_status)!=0){
                  fprintf(stderr,
                   "# UT : %9.6f ERROR stowing telescope\n",ut);
               }
              }
              else if(stop_flag==0){
               fprintf(stderr,
                   "# UT : %9.6f stopping telescope\n",ut);
               if(do_stop(ut,&tel_status)!=0){
                  fprintf(stderr,
                   "# UT : %9.6f ERROR stopping telescope\n",ut);
               }
              }
              fflush(stderr);
        }
#if FAKE_RUN
           
        ut=ut+(LOOP_WAIT_SEC/3600.0);
        if(ut>24.0)ut=ut-24.0;
        jd=jd+(LOOP_WAIT_SEC/86400.0);
#else
        sleep(LOOP_WAIT_SEC);
#endif
         }/* end of pause_flag check */

        /* if focus sequence is complete, wait for readout of last exposure.
           If readout  is good, get and set best focus. If focus sequence bad,
           set focus to default focus.
           If last readout is bad, set n_done to n_done-1. This will force
           another attempt on the last exposure so that focus sequence can
           proceed.
           If telescope won't focus, exit.*/
 
         else if(telescope_ready&&i_prev>=0&&focus_done==0&&
            sequence[i_prev].shutter==FOCUS_CODE&&
                sequence[i_prev].n_done==sequence[i_prev].n_required){

      
          if(verbose){
            fprintf(stderr,"waiting for camera readout\n");
            fflush(stderr);
          }
#if FAKE_RUN
          if(0){
#else
          if(wait_camera_readout(&cam_status)!=0){
#endif
              fprintf(stderr,"bad readout of last exposure in focus sequence. Trying again\n");
              fflush(stderr);
              sequence[i_prev].n_done=sequence[i_prev].n_done-1;
          }
          else{
              fprintf(stderr,"Focus sequence complete. Getting and Setting best focus\n");
              result=focus_telescope(sequence+i_prev,&tel_status,focus_default);
              if(result<0){
              fprintf(stderr,"Unable to focus telescope. Exitting\n");fflush(stderr);
              sequence[i_prev].n_done=0;

              if(save_obs_record(sequence,obs_record,num_fields,&tm)!=0){
                 fprintf(stderr,"ERROR saving obs record\n");
                 fflush(stderr);
                 do_exit(-1);
              }
              do_exit(-1);
              }
              else if(result>0){
              fprintf(stderr,"Bad focus sequence. Default used: %9.5f\n",
                tel_status.focus);
              fflush(stderr);
              }
              else{
              fprintf(stderr,"Telescope focus set to %8.5f\n",tel_status.focus);
              fflush(stderr);
              focus_done=1;
              }
          }
          } /*end of last focus exposure check */

        /* If previous exposure was an offset exposure (shutter==OFFSET_CODE),
           wait for readout of the previous exposure. If the readout is good
           get and set offsets. If the readout is bad, set n_done to n_done-1.*/
 
          else if(i_prev>=0&&sequence[i_prev].shutter==OFFSET_CODE&&offset_done==0&&
                sequence[i_prev].n_done==sequence[i_prev].n_required){

      
          if(verbose){
            fprintf(stderr,"waiting for camera readout\n");
            fflush(stderr);
          }
#if FAKE_RUN
          if(0){
#else
          if(wait_camera_readout(&cam_status)!=0){
#endif
              fprintf(stderr,"bad readout of last exposure in focus sequence. Trying again\n");
              fflush(stderr);
              sequence[i_prev].n_done=sequence[i_prev].n_done-1;
          }
          else{
              fprintf(stderr,"Offset exposure complete. Getting and Setting telescope offsets\n");
              result=get_telescope_offsets(sequence+i_prev,&tel_status);
              if(result!=0){
              fprintf(stderr,"Unable to get offsets. Using previous values\n");
              fflush(stderr);

              }
              else{
              fprintf(stderr,"Telescope offsets set to %8.6f %8.6f deg\n",
                tel_status.ra_offset,tel_status.dec_offset);
              fflush(stderr);
              }

              /* set flag so that offsets are not attempted again until
             a new offset field appears in the field list */

              offset_done=1;
          }
         } /* end of offset pointing check */

         /* otherwise try to observe next sky field */

         else{
           

         /* choose next field to observe */

         i=get_next_field(sequence,num_fields,i_prev,jd,bad_weather);
         if (i>=0 ){
            selection_code = sequence[i].selection_code;
            sprintf(code_string,"%s",selection_string[selection_code]);
            fprintf(stderr,
              "# UT : %9.6f Selected field %d: %s\n",ut,i,code_string);
         }

         /* if i < 0, no fields to observe */

         if(i<0){
            if(verbose){
              fprintf(stderr,
              "# UT : %9.6f No fields ready to observe\n",ut);
            }

#if FAKE_RUN
#else

            /* If there are no fields pending, and weather
               if good, stop telescope. If weather is bad,
               stow the telescope */

            if(telescope_ready){
            /*if(bad_weather&&stow_flag==0){*/
            if(bad_weather&&stop_flag==0){
               fprintf(stderr,
                   "# UT : %9.6f stowing telescope\n",ut);
               /*if(do_stow(ut,&tel_status)!=0){*/
               if(do_stop(ut,&tel_status)!=0){
                  fprintf(stderr,
                   "# UT : %9.6f ERROR stowing telescope\n",ut);
               }
            }
            else if(stop_flag==0){
               fprintf(stderr,
                   "# UT : %9.6f stopping telescope\n",ut);
               if(do_stop(ut,&tel_status)!=0){
                  fprintf(stderr,
                   "# UT : %9.6f ERROR stopping telescope\n",ut);
               }
            }
            fflush(stderr);
            }


#endif
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
#if FAKE_RUN
            ut=ut+(LOOP_WAIT_SEC/3600.0);
            if(ut>24.0)ut=ut-24.0;
            jd=jd+(LOOP_WAIT_SEC/86400.0);
#else
            sleep(LOOP_WAIT_SEC);
#endif
            }
         } //if(i<0){
       
#if 0
         /* otherwise, if the weather is good, or else the weather is bad but the
            next field is a dark or a dome flat, go ahead and observe field i */

         else if ((bad_weather&&sequence[i].shutter==DARK_CODE) ||
              (bad_weather&&sequence[i].shutter==DOME_FLAT_CODE) ||
              ( (!bad_weather) && telescope_ready) ){
#else
         /* otherwise, if the next field is a dark or a dome flat, or else if the 
            weather is good and the telescope us ready, go ahead and observe field i */

         else if (sequence[i].shutter==DARK_CODE ||
              sequence[i].shutter==DOME_FLAT_CODE ||
              ( (!bad_weather) && telescope_ready) ){
#endif

            /* reset focus_done flag to 0 if this is a new focus sequence */
            if(focus_done&&sequence[i].shutter==FOCUS_CODE)focus_done=0;

            /* reset offset_done flag to 0 if this is a new offset sequence */
            else if(offset_done&&sequence[i].shutter==OFFSET_CODE)offset_done=0;

            if (first_exposure){
            strcpy(exp_mode,EXP_MODE_FIRST);
            first_exposure = False;
            }
            else{
            strcpy(exp_mode,EXP_MODE_NEXT);
            }
            if(observe_next_field(sequence,i,i_prev,jd,&dt,&nt,WAIT_FLAG,
            log_obs_out,&tel_status,&cam_status,&fits_header,exp_mode)!=0){
               fprintf(stderr,"ERROR observing field %d\n",i);
               fflush(stderr);
               if(telescope_ready&&stop_flag==0){
               if(verbose){
                   fprintf(stderr,"stopping telescope\n");
                   fflush(stderr);
               }
               if(stop_telescope()!=0){
                   fprintf(stderr,"ERROR stopping telescope\n");
               }
               else{
                   stop_flag=1;
               }
               }
            }
 
            /* Save a binary record of the status of each field so that
               scheduler can start up where it ended if it crashes */

            if(save_obs_record(sequence,obs_record,num_fields,&tm)!=0){
              fprintf(stderr,"ERROR saving obs record\n");
              fflush(stderr);
              do_exit(-1);
            }

            /* save a line of ASCII symbols to graphically represent completion
               status of the fields */

            print_history(jd,sequence,num_fields,hist_out);
 
            /* A memory leak of some kind requires this fflush statement here */
            fflush(stderr);

#if FAKE_RUN
            jd=jd+(dt/24.0);
            ut=ut+dt;
#endif
            i_prev=i;

         }
         else{
#if FAKE_RUN
            telescope_ready=1;
            bad_weather=0;
            ut=ut+(LOOP_WAIT_SEC/3600.0);
            if(ut>24.0)ut=ut-24.0;
            jd=jd+(LOOP_WAIT_SEC/86400.0);
#else
            if(!telescope_ready){
              fprintf(stderr,"Waiting for telescope to come up...\n");
            }
            else if (bad_weather){
              fprintf(stderr,"Waiting for dome to open\n");
            }
            fflush(stderr);

            sleep(LOOP_WAIT_SEC);
#endif
         } //if(i<0){
         } /* end of choose and observe next field */
         
#if FAKE_RUN
#else
         ut=get_ut();
         jd=get_jd();
#endif
    } /* end of while (jd < nt.sunrise) loop */

    fprintf(stderr, "# UT: %9.6f Ending observations\n",ut);

#if FAKE_RUN
#else
    if(jd>nt.jd_sunrise){
         fprintf(stderr,
           "# UT: %9.6f Stowing Telescope\n",ut);
         if(stow_telescope()!=0){
        fprintf(stderr,"Could not stow telescope\n");
         }
    }
#endif
         

    num_completed_fields=0;
    for(i=0;i<num_fields;i++){
       if(sequence[i].n_done==sequence[i].n_required){
        fprintf(sequence_out,"%s",sequence[i].script_line);
        num_completed_fields++;
       }
       print_field_status(sequence+i,stderr);
    }

    fprintf(stderr, 
        "# %d fields loaded  %d fields observable  %d field completed\n",
        num_fields, num_observable_fields, num_completed_fields);

/*
    fclose(log_obs_out);
    fclose(sequence_out);
    fclose(hist_out);  
*/
    do_exit(0);
}
        
/******************************************************************/

int add_new_fields(Field *sequence, int num_fields,
        Field *new_sequence, int num_new_fields){
  
    int i,field_number;

    if (num_fields + num_new_fields >= MAX_FIELDS ){
     fprintf(stderr, "add_new_fields: too many new fields to add\n");
     return(-1);
    }

    field_number = num_fields;
    for (i=1;i<=num_new_fields;i++){
    *(sequence+num_fields+i-1)=*(new_sequence+i-1);
    (sequence+num_fields+i-1)->field_number = field_number + i;
    }

    return(0);
}
/******************************************************************/

int do_stop(double ut,Telescope_Status *status)
{

#if FAKE_RUN
       stop_flag=1;
#else

    if(verbose){
         fprintf(stderr,"do_stoP: waiting 60 seconds\n");
         fflush(stderr);
    }
    
    sleep(60);

    if(verbose){
         fprintf(stderr,"do_stop: updating telescope status\n");
         fflush(stderr);
    }

    if(update_telescope_status(status)!=0){
          fprintf(stderr,"do_stop: ERROR updating telescope status\n");
          fflush(stderr);
    }
#if 0
    /* don't point to meridian on a stop */
    if(verbose){
        fprintf(stderr, "# UT : %9.6f pointing telescope to %12.6f %12.6f\n",
          ut,status->lst,0.0);
          fflush(stderr);
    }

    if(point_telescope(status->lst,0.0,0.0,0.0)!=0){
          fprintf(stderr,"do_stop: ERROR pointing telescope\n");
          fflush(stderr);
    }

#endif
    if(verbose){
        fprintf(stderr, "# UT : %9.6f stopping telescope\n",ut);
    }

    if(stop_telescope()!=0){
        fprintf(stderr,"ERROR stopping telescope\n");
    }
    else{
        stop_flag=1;
    }
#endif

    if(stop_flag==0){
         return(-1);
    }
    else{
         return(0);
    }

}

/************************************************************/

int do_stow(double ut,Telescope_Status *status)
{

#if FAKE_RUN
       stow_flag=1;
#else

    if(verbose){
         fprintf(stderr,"do_stow: waiting 60 seconds\n");
         fflush(stderr);
    }

    sleep(60);

    if(verbose){
         fprintf(stderr, "# UT : %9.6f do_stow: stowing telescope\n",ut);
         fflush(stderr);
    }

    if(stow_telescope()!=0){
         fprintf(stderr,"do_stow: could not stow telescope\n");
         fflush(stderr);
    }
#if 0
    else{
         stow_flag=1;
         stop_flag=1;
    }
#else
    /* even if telescope controller returns fail, assume the
       stow and the stop worked. Usually the stow is called
       only when the dome is closed, which occurs after a 
       self-stow by the control system */
    stow_flag=1;
    stop_flag=1;
#endif

    if(verbose){
         fprintf(stderr,"do_stow: updating telescope status\n");
         fflush(stderr);
    }

    if(update_telescope_status(status)!=0){
          fprintf(stderr,"do_stow: ERROR updating telescope status\n");
          fflush(stderr);
    }

#endif

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

     if(hist_out!=NULL)fclose(hist_out);
     if(sequence_out!=NULL)fclose(sequence_out);
     if(log_obs_out!=NULL)fclose(log_obs_out);
     if(obs_record!=NULL)fclose(obs_record);
      
     return(0);
}

/************************************************************/

int do_exit(int code)
{
#if FAKE_RUN
#else
     if(verbose){
    fprintf(stderr,"do_exit: stopping telescope\n");
     }
     stop_telescope();
#endif
     if(verbose){
    fprintf(stderr,"do_exit: closing files\n");
     }
     close_files();

     fprintf(stderr,"exiting\n");
     fflush(stderr);

     exit(code);
}
/************************************************************/

int save_obs_record(Field *sequence, FILE *obs_record, int num_fields,
     struct tm *tm)
{
     char string[STR_BUF_LEN];

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

int load_obs_record(char *file_name, Field *sequence, FILE **obs_record)
{
     int i,n;
     int num_fields;
     int n_completed;
     int n_started;
     int n_fresh;
     int year,month,day,hour,minute,second;
     Field *f;
     char string[STR_BUF_LEN];

     *obs_record=fopen(file_name,"r+");
     if(*obs_record==NULL){
    fprintf(stderr,"load_obs_record: can't open file %s for reading\n",
        file_name);
    fprintf(stderr,"load_obs_record: creating new empty record\n");
    fflush(stderr);
    *obs_record=fopen(file_name,"w");
    if(*obs_record==NULL){
      fprintf(stderr,"load_obs_record: can't create new obs record\n");
      return(-1);
    }
    else{
      return(0);
    }
     }

     num_fields=0;
     n_fresh=0;
     n_started=0;
     n_completed=0;

     if(fgets(string,STR_BUF_LEN,*obs_record)==NULL){
     fprintf(stderr,"load_obs_record: can't get first line \n");
     fprintf(stderr,"load_obs_record: assuming no previous observations\n");
     return(0);
     }

     if(sscanf(string,"%d %d %d %d %d %d %d",
           &num_fields,&year,&month,&day,&hour,&minute,&second)!=7){
     fprintf(stderr,"load_obs_record: bad first line: %s\n",string);
     return(-1);
     }
     else{
     fprintf(stderr,"load_obs_record: %s",string);
     } 

     if(num_fields>MAX_FIELDS){
     fprintf(stderr,"load_obs_record: too many fields in obs_record\n");
     return(-1);
     }

     i=fread((void *)sequence, sizeof(Field),num_fields,*obs_record);

     if(i!= num_fields){
     fprintf(stderr,"load_obs_record: only %d of %d fields read\n",
        i,num_fields);
     return(-1);
     }

     /* rewind obs_record so that next write will be at start of file
    (overwriting previous records) */

     if(fseek(*obs_record,0,SEEK_SET)!=0){
     fprintf(stderr,"load_obs_record: can't rewind obs_record\n");
     return(-1);
     }

     for(i=0;i<num_fields;i++){
      f=sequence+i;
      if(f->n_done==0){
          n_fresh++;
      }
      else if (f->n_done < f->n_required){
           n_started++;
      }
      else {
           n_completed++; 
      }
      if(f->shutter==FOCUS_CODE){
         n=f->n_required/2;
         focus_start=focus_default-n*focus_increment;
      }
      if(num_fields>=MAX_FIELDS){
         fprintf(stderr,
          "load_obs_record: maximum number of fields exceeded\n");
         fflush(stderr);
         return(-1);
      }
     }
     
     fprintf(stderr,
       "load_obs_record: %d total, %d fresh, %d started, %d completed\n",
       num_fields, n_fresh,n_started,n_completed);

     return(num_fields);
}

/************************************************************/

int check_weather (FILE *input, double jd, struct date_time *date, Night_Times *nt)
{
     char string[STR_BUF_LEN],s[256];
     double t_obs,t_on,duration,ut;
     int year, mon, day, doy;

     /* ut date is 1 + doy since get_day_of_year takes local time */
     doy=1+get_day_of_year (date); 

     /* ut values don't go past 24 hours at ESO La Silla at night */
     ut=nt->ut_start+(jd-nt->jd_start)*24.0;

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

     while(t_obs>t_on+duration&&fgets(string,STR_BUF_LEN,input)!=NULL){
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
                     Site_Params *site,int print_flag)
{

    /* initialize night_time values */
    print_tonight(date,site->lat,site->longit,site->elevsea,site->elev,site->horiz,
              site->site_name,site->stdz,site->zone_name,site->zabr,site->use_dst,
              &(site->jdb),&(site->jde),2,nt,print_flag);



    if (USE_12DEG_START){
      if (print_flag ){
         fprintf(stderr,"using 12 deg twilight + %12.6f h for start time\n",
            STARTUP_TIME);
     }
      nt->ut_start=nt->ut_evening12+STARTUP_TIME;
      nt->ut_end=nt->ut_morning12-MIN_EXECUTION_TIME;
      nt->lst_start=nt->lst_evening12+STARTUP_TIME;
      nt->lst_end=nt->lst_morning12-MIN_EXECUTION_TIME;
      nt->jd_start=nt->jd_evening12+(STARTUP_TIME/24.0);
      nt->jd_end=nt->jd_morning12-(MIN_EXECUTION_TIME/24.0);
       }
    else {
      if (print_flag ){
         fprintf(stderr,"using 18 deg twilight + %12.6f h for start time\n",
            STARTUP_TIME);
     }
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
   exposure. 
   
   If wait_flag is False,  return after the exposure time ends, but
   before the  readout begins. Otherwise return when the readout 
   also completes.

   Return -1 if there is an error pointing the telescope or taking 
   the exposure. 

   Print diagnostic messages to stderr. Print the status of each
   field to output (this serves as the  exposure log). */

int observe_next_field(Field *sequence, int index, int index_prev,
    double jd, double *dt, Night_Times *nt, bool wait_flag,
    FILE *output,Telescope_Status *tel_status, Camera_Status *cam_status,
    Fits_Header *fits_header, char *exp_mode)
{
    double lst,ut,actual_expt,ha,ra_correction,dec_correction,ra,dec,dt1;
    double ra_rate,dec_rate; /* ra and dec tracking rate corrections in arcsec/hour */
    struct timeval t0,t1,t2;
    struct tm tm;
    char string[STR_BUF_LEN],shutter_string[3],filename[STR_BUF_LEN],field_description[STR_BUF_LEN];
    Field *f,*f_prev;
    double focus;
    double ra_dither,dec_dither;
    int n_clears;
    int n,num_exposures;
    double split_expt,expt;
    int bad_read_count;
    int exp_error_code=0;
    //bool wait_flag = True;

    ra_dither=0.0;
    dec_dither=0.0;
    ra_rate=0.0;
    dec_rate=0.0;
    ha=0.0;
    num_exposures=1;
    split_expt=0.0;
    bad_read_count=0;

    f=sequence+index;
    if(index_prev>=0){
       f_prev=sequence+index_prev;
    }
    else{
       f_prev=NULL;
    }
    expt=f->expt;

    gettimeofday(&t0,NULL);

    get_shutter_string(shutter_string,f->shutter,field_description); 
    if(verbose){
     fprintf(stderr,"observe_next_field: observing %s, field %d iteration %d\n",
        field_description, f->field_number,f->n_done+1);
     fflush(stderr);
    }

#if FAKE_RUN
    ut=nt->ut_start+(jd-nt->jd_start)*24.0;
    lst=nt->lst_start+(jd-nt->jd_start)*SIDEREAL_DAY_IN_HOURS;
#else
    if(f->shutter==DARK_CODE||f->shutter==DOME_FLAT_CODE){
    }
    else if(update_telescope_status(tel_status)!=0){
     fprintf(stderr,"observe_next_field: could not update telescope status\n");
     return(-1);
    }
    lst=tel_status->lst;
    jd=get_jd();
    ut=get_ut();
#endif


    /* on first observation of focus sequence, set ra to lst - 1 hour
       and dec to 0 deg. On all other iterations, set to same ra and
       dec as first iteration 
    */

    if(f->shutter==FOCUS_CODE){
       if(f->n_done==0){
#if 0
      ha=1.0;
      f->ra=lst-1.0;
#else
      ha=-1.0;
      f->ra=lst+1.0;
#endif
      f->dec=0.0;
      fprintf(stderr,
         "observe_next_field: Pointing %s at %12.6f %12.5f\n",
         field_description,f->ra,f->dec);
       }
    }

   /* set offset position to lst-1 hour, dec = 0  on first iteration*/

    else if(f->shutter==OFFSET_CODE){
    if(f->n_done==0){
      ha=-1.0;
      f->ra=lst+1.0;
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

    if(TRACKING_CORRECTIONS_ON){
        ra_rate=get_ra_rate(ha,f->dec);
        dec_rate=get_dec_rate(ha,f->dec);
        if(verbose){
        fprintf(stderr,"observe_next_field:  tracking corrections: HA = %10.6f, Dec = %10.6f, Ra and Dec rates: %10.6f %10.6f\n",
        ha, f->dec, ra_rate,dec_rate);
        }
    }


    /* If the telescope position is west of the meridian (ha>0) and
       the exposure times exceeds LONG_EXPTIME (where tracking errors accrue)
       then break up the exposures into multiple exposures of length
       1+n, where n is the largest integer smaller than expt/LONG_EXPT. */

/*
fprintf(stderr,"HA = %10.6f,  expt = %10.6f,  LONG_EXPTIME = %10.6f\n",
        ha,3600.0*f->expt,3600.0*LONG_EXPTIME);
*/

#ifdef POINTING_TEST
    if(f->expt>LONG_EXPTIME){
#else
    if(ha>0.0&&f->expt>LONG_EXPTIME){
#endif
        fprintf(stderr,"observe_next_field: long exposure in the west\n");
        num_exposures = f->expt/LONG_EXPTIME;
        num_exposures++;
        split_expt=f->expt/num_exposures;
        expt=split_expt;
        f->n_required=f->n_required+num_exposures-1;
        if(f->n_required>MAX_OBS_PER_FIELD){
           fprintf(stderr,"observe_next_field: too many observations of field %d\n",
        f->field_number);
           return(-1);
        }
        fprintf(stderr,
        "observe_next_field: splitting into %d exposure of duration %f sec\n",
            num_exposures,3600.0*split_expt);
    }
    else{
        expt=f->expt;
        num_exposures=1;
    }

    }

#if FAKE_RUN

  for(n=1;n<=num_exposures;n++){
    *dt=expt+EXPOSURE_OVERHEAD;
    actual_expt=expt;
    ha=lst-f->ra;
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
    f->jd_next=jd+(f->interval/24.0);
 
    fprintf(stderr,
        "UT: %10.6f JD: %12.6f Exposed field : %d  RA: %9.6f  Dec: %9.5f n_done: %d n_wanted: %d time_left : %10.6f jd_next : %10.6f  description: %s\n",
         ut, jd-2450000,f->field_number, f->ra, f->dec, f->n_done, 
         f->n_required, f->time_left, f->jd_next, field_description);

    get_shutter_string(shutter_string,f->shutter,field_description); 

    if(f->shutter!=DARK_CODE&&f->shutter!=DOME_FLAT_CODE){ 

    if(f->shutter==FOCUS_CODE||f->shutter==OFFSET_CODE){
       ra=f->ra;
       dec=f->dec;
    }
    else if (f->shutter==EVENING_FLAT_CODE || f->shutter==MORNING_FLAT_CODE){     
       get_dither(f->n_done,&ra_dither,&dec_dither,FLAT_DITHER_STEP);
       ra=f->ra+(ra_dither/15.0);
       dec=f->dec+dec_dither;
       if(verbose){
           fprintf(stderr,
         "observe_next_field: Dithering %s by %7.5f %7.5f deg\n",
          field_description, ra_dither, dec_dither);
       }
    }
    else if (f->shutter==SKY_CODE){
#if 0
       /* this was wrong. offsets should be subtracted not added ! */
       ra=f->ra+(tel_status->ra_offset-ra_correction)/15.0;
       dec=f->dec+tel_status->dec_offset-dec_correction;
#else
       ra=f->ra-(tel_status->ra_offset+ra_correction)/15.0;
       dec=f->dec-tel_status->dec_offset-dec_correction;
#endif
       /* add small dither for deep search coadds. Bad pixels are remove din median average */
       if(f->n_required==6&&DEEP_DITHER_ON){
          get_dither(f->n_done,&ra_dither,&dec_dither,DEEPSEARCH_DITHER_STEP);
          ra=f->ra+(ra_dither/15.0);
          dec=f->dec+dec_dither;
          if(verbose){
           fprintf(stderr,
         "observe_next_field: Dithering %s by %7.5f %7.5f deg\n",
          field_description, ra_dither, dec_dither);
          }
       }
       if(verbose&&POINTING_CORRECTIONS_ON){
           fprintf(stderr,
          "observe_next_field: ra, dec corrections for field %d :%9.6f %9.6f deg ha0: %9.6f ha: %9.6f\n",
          f->field_number,ra_correction,dec_correction,f->ha[0],ha);
       }
    }
    else{
       fprintf(stderr,"observe_next_field: shutter code unrecognized: %d\n",f->shutter);
       fprintf(stderr,"observe_next_field: skipping observations\n");
       return(-1);
    }

    if(verbose){
       fprintf(stderr,"observe_next_field: pointing telescope to %12.6f %12.5f\n",
        ra,dec);
       fflush(stderr);
    }
    }

    if(output!=NULL){
       fprintf(output,"%10.6f %10.6f %s %d %6.1f %10.6f %11.6f %10.6f %s # %s %d",
       f->ra,f->dec,shutter_string,f->n_done,3600.0*expt,
       ha,jd,actual_expt,filename,field_description,f->field_number);
       /*ut,jd,actual_expt,filename,field_description,f->field_number);*/
       if(strstr(f->script_line,"#")!=NULL){
      fprintf(output,"%s",strstr(f->script_line,"#")+1);
       }
       else{
      fprintf(output,"\n");
       }
       fflush(output);
    }
    if(n<num_exposures){
    ut=ut+((*dt));
    jd=jd+(*dt/24.0);
    lst=lst+(*dt);
    }
  }
  *dt=num_exposures*(expt+EXPOSURE_OVERHEAD);
#else

    if(f->shutter!=DARK_CODE&&f->shutter!=DOME_FLAT_CODE){ 

    gettimeofday(&t1,NULL);

    if(f->shutter==FOCUS_CODE||f->shutter==OFFSET_CODE){
       ra=f->ra;
       dec=f->dec;
    }
    else if (f->shutter==EVENING_FLAT_CODE || f->shutter==MORNING_FLAT_CODE){     
       get_dither(f->n_done,&ra_dither,&dec_dither,FLAT_DITHER_STEP);
       ra=f->ra+(ra_dither/15.0);
       dec=f->dec+dec_dither;
       if(verbose){
           fprintf(stderr,
         "observe_next_field: Dithering %s by %7.5f %7.5f deg\n",
          field_description, ra_dither, dec_dither);
       }
    }
    else if (f->shutter==SKY_CODE){
#if 0
       /* this was wrong. offsets should be subtracted not added ! */
       ra=f->ra+(tel_status->ra_offset-ra_correction)/15.0;
       dec=f->dec+tel_status->dec_offset-dec_correction;
#else
       ra=f->ra-(tel_status->ra_offset+ra_correction)/15.0;
       dec=f->dec-tel_status->dec_offset-dec_correction;
#endif
       /* add small dither for deep search coadds. Bad pixels are remove din median average */
       if(f->n_required==6&&DEEP_DITHER_ON){
          get_dither(f->n_done,&ra_dither,&dec_dither,DEEPSEARCH_DITHER_STEP);
          ra=f->ra+(ra_dither/15.0);
          dec=f->dec+dec_dither;
          if(verbose){
           fprintf(stderr,
         "observe_next_field: Dithering %s by %7.5f %7.5f deg\n",
          field_description, ra_dither, dec_dither);
          }
       }
       if(verbose&&POINTING_CORRECTIONS_ON){
           fprintf(stderr,
          "observe_next_field: ra, dec corrections for field %d :%9.6f %9.6f deg ha0: %9.6f ha: %9.6f\n",
          f->field_number,ra_correction,dec_correction,f->ha[0],ha);
       }
    }
    else{
       fprintf(stderr,"observe_next_field: shutter code unrecognized: %d\n",f->shutter);
       fprintf(stderr,"observe_next_field: skipping observations\n");
       return(-1);
    }

    if(verbose){
       fprintf(stderr,"observe_next_field: pointing telescope to %12.6f %12.5f\n",
        ra,dec);
       fflush(stderr);
    }

    if(point_telescope(ra,dec,ra_rate,dec_rate)!=0){
       fprintf(stderr,"observe_next_field: ERROR pointing telescope to %10.6f %10.6f\n",
        ra,dec);
       return(-1);
    }

    gettimeofday(&t2,NULL);

    if(verbose){
       fprintf(stderr,
         "observe_next_field: done pointing telescope in %ld sec\n",
          t2.tv_sec-t1.tv_sec);
       fflush(stderr);
    }
    }
    else{
    if(verbose){
        fprintf(stderr,"observe_next_field: %s , skip telescope pointing\n",
        field_description);
        fflush(stderr);
    }
    }
   
    if(f->shutter==DARK_CODE||f->shutter==DOME_FLAT_CODE){
    }  
    else if(update_telescope_status(tel_status)!=0){
    fprintf(stderr,"observe_next_field: could not update telescope status\n");
    return(-1);
    }

    /* if this exposure is part of a focus sequence, set the telescope
       focus accordingly */
  
    if(f->shutter==FOCUS_CODE){

       focus=focus_start+focus_increment*f->n_done;
       if(focus<MIN_FOCUS||focus>MAX_FOCUS){
     fprintf(stderr,
        "observe_next_field: intended focus setting out of range: %8.5f\n",
        focus);
     return(-1);
       }

       fprintf(stderr,"# setting focus to %8.5f mm\n",
        focus);
       fflush(stderr);

       if(set_telescope_focus(focus)!=0){
       fprintf(stderr,"observe_next_field: unable to set telescope focus\n");
       return(-1);
       }
    
       if(update_telescope_status(tel_status)!=0){
       fprintf(stderr,"observe_next_field: could not update telescope status\n");
       return(-1);
       }
      
       if(verbose){
      fprintf(stderr,"observe_next_field: focus set to %8.5f mm\n",
        tel_status->focus);
       }

    }
    
    ut=get_ut();
    jd=get_jd();

    lst=tel_status->lst;
    if(verbose){
       fprintf(stderr,"observe_next_field: updating FITS header\n");
    }
 
    sprintf(string,"%8.4f",tel_status->ra);
    if(update_fits_header(fits_header,RA_KEYWORD, string)<0)return(-1);

    sprintf(string,"%8.4f",tel_status->dec);
    if(update_fits_header(fits_header,DEC_KEYWORD, string)<0)return(-1);

    sprintf(string,"%8.4f",tel_status->lst);
    if(update_fits_header(fits_header,LST_KEYWORD, string)<0)return(-1);

    sprintf(string,"%8.4f",ha);
    if(update_fits_header(fits_header,HA_KEYWORD, string)<0)return(-1);

    if(filter_name_ptr!=0){
       sprintf(tel_status->filter_string,"%s",filter_name_ptr);
    }

    if(update_fits_header(fits_header,FILTERNAME_KEYWORD,
        tel_status->filter_string)<0)return(-1);

    if(strstr(tel_status->filter_string,"rgzz")!=NULL||
        strstr(tel_status->filter_string,"FAKE")!=NULL){
     if(update_fits_header(fits_header,FILTERID_KEYWORD,"4")<0) return(-1);
    }
    else{
     if(update_fits_header(fits_header,FILTERID_KEYWORD,"0")<0) return(-1);
      fprintf(stderr,"observe_next_field: filter %s not recognized\n",
        tel_status->filter_string);
    }
    sprintf(string,"%8.4f",tel_status->focus);
    if(update_fits_header(fits_header,FOCUS_KEYWORD,string)<0)return(-1);



    /* wait for readout of previous exposure. If the readout is bad, mark
       the previous exposure as undone */

    if(verbose){
       fprintf(stderr,"observe_next_field: waiting for camera readout\n");
       fflush(stderr);
    }
 
    if(wait_camera_readout(cam_status)!=0){
    fprintf(stderr,
         "observe_next_field: bad readout before field %d\n",index);
    if(index_prev>=0&&f_prev->n_done>0){
        fprintf(stderr,
           "observe_next_field: setting last exposure of field %d to undone\n",
           index_prev);
        f_prev->n_done=f_prev->n_done-1;
        f_prev->jd_next=jd;
    }
    }
      
    if(verbose){
     fprintf(stderr,"observe_next_field: Taking next exposure\n");
     fflush(stderr);
    }

    dt1=clock_difference(ut_prev,ut);

    // DEBUG
    fprintf(stderr,"observe_next_field: skipping clears\n");
    if(ut_prev<0.0||dt1>CLEAR_INTERVAL){
    if(ut_prev>0.0){
        fprintf(stderr,
        "observe_next_field: %10.6f minutes since last readout. Clearing camera..\n",
        dt1*60.0);
    }
    else{
        fprintf(stderr,
        "observe_next_field: First exposure since startup. Clearing camera..\n");
    }


    for(n_clears=0;n_clears<NUM_CAMERA_CLEARS;n_clears++){
       if(verbose){
          fprintf(stderr,"observe_next_field: clear %d ...\n",n_clears); 
          fflush(stderr);
        }
       
        if(clear_camera()!=0){
         fprintf(stderr,"could not clear camera\n");
         return(-1);
        }
    }
    }


    for(n=1;n<=num_exposures;n++){
     gettimeofday(&t1,NULL);
     ut=get_ut();
     jd=get_jd();
     actual_expt=expt;

     if(take_exposure(f,fits_header,&actual_expt,filename,&ut,&jd,
        wait_flag,&exp_error_code,exp_mode)!=0){
       fprintf(stderr,"observe_next_field: ERROR taking exposure %d\n",n);
       return(-1);
     }
     ut_prev=ut;
     gettimeofday(&t2,NULL);
     if(verbose){
       fprintf(stderr,"observe_next_field: Exposure complete in %ld sec, ut = %9.6f\n",
       t2.tv_sec-t1.tv_sec,ut);
       fflush(stderr);
     }

     f->ut[f->n_done]=ut;
     f->jd[f->n_done]=jd;
     f->ha[f->n_done]=ha;
     f->lst[f->n_done]=lst;
     f->actual_expt[f->n_done]=actual_expt/3600.0;
     strncpy(f->filename+(f->n_done)*FILENAME_LENGTH,filename,FILENAME_LENGTH);
     f->n_done=f->n_done+1;

/* this line aded 2007 Jun 14 to fix bug */
     f->jd_next=jd+(f->interval/24.0);
 
     fprintf(stderr,
        "UT : %10.6f JD: %12.6f Exposed field : %d  RA: %9.6f  Dec: %9.5f n_done: %d n_wanted: %d time_left : %10.6f jd_next : %10.6f  description: %s\n",
         ut, jd-2450000,f->field_number, f->ra, f->dec, f->n_done, 
         f->n_required, f->time_left, f->jd_next, field_description);

     get_shutter_string(shutter_string,f->shutter,field_description); 

     if(output!=NULL){
        fprintf(output,"%10.6f %10.6f %s %d %6.1f %10.6f %11.6f %10.6f %s # %s %d",
        f->ra,f->dec,shutter_string,f->n_done,3600.0*expt,
        ha,jd,actual_expt,filename,field_description,f->field_number);
        /*ut,jd,actual_expt,filename,field_description,f->field_number);*/
        if(strstr(f->script_line,"#")!=NULL){
           fprintf(output,"%s",strstr(f->script_line,"#")+1);
        }
        else{
           fprintf(output,"\n");
        }
        fflush(output);
     }

     if(n<num_exposures){
        if(verbose){
          fprintf(stderr,
        "observe_next_field: waiting for readout of exposure %d\n",n);
          fflush(stderr);
        }
 
        if(wait_camera_readout(cam_status)!=0){
         fprintf(stderr,
           "observe_next_field: bad readout of exposure %d\n",n);
         fprintf(stderr,
           "observe_next_field: repeating exposure %d\n",n);
         n--;
         f->n_done=f->n_done-1;
         bad_read_count++;
         if(bad_read_count>MAX_BAD_READOUTS){
           fprintf(stderr,"observe_next_field: too many bad reads\n");
           return(-1);
         }
        }
        if(update_telescope_status(tel_status)!=0){
           fprintf(stderr,"observe_next_field: could not update telescope status\n");
           return(-1);
        }
        lst=tel_status->lst;
        ha=lst-f->ra; /* current hour angle of field */
        if(ha<-12)ha=ha+24.0;
        if(ha>12)ha=ha-24.0;
     } /* end if n<num_exposure */
    } /* end for n = 1 to num_exposures */

    *dt=t2.tv_sec-t0.tv_sec;
    *dt=*dt/3600.0;
#endif

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

int get_dither(int iteration, double *ra_dither, double *dec_dither, double step_size)
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
       *ra_dither=step_a;
       *dec_dither=step_b-step_a;
    }
    else if (side==1){
       *ra_dither=step_b-step_a+1;
       *dec_dither=step_a;
    }
    else if (side==2){
       *ra_dither=-step_a;
       *dec_dither=step_b-step_a+1;
    }
    else{
       *ra_dither=step_b-step_a;
       *dec_dither=-step_a;
    }

   *ra_dither=(*ra_dither)*step_size;
   *dec_dither=(*dec_dither)*step_size;
     
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
     int i,n_left,n_left_min,n_left_min_must_do;
     int i_min,i_max,status,n_ready,n_late;
     int n_do_now,i_min_dark, i_min_flat,i_min_do_now;
     int n_ready_must_do,n_late_must_do;
     char field_status[256];

     if(i_prev>=0&&i_prev<num_fields-1){
       f_prev=sequence+i_prev;
       f_next=sequence+i_prev+1;
     }
     else{
       f_prev=NULL;
       f_next=NULL;
     }

     n_left_min=100000;
     n_left_min_must_do=100000;
     n_ready=0;
     n_late=0;
     n_do_now=0;
     i_min_dark=-1;
     i_min_flat=-1;
     i_min_do_now=-1;
     n_ready_must_do=0;
     n_late_must_do=0;

     if(verbose){  
    fprintf(stderr,"get_next_field: updating field status\n");
     }

     for(i=0;i<num_fields;i++){
     f=sequence+i;

     update_field_status(f,jd,bad_weather);
     if(verbose1){
       get_field_status_string(f,field_status);
       fprintf(stderr,"field %d status %s\n",i,field_status);
     }

#if 1
     /* for any MUST-DO field with READY_STATUS, increment the count and update minimum
        value of n_left */

     if (f->status==READY_STATUS&&f->survey_code==MUSTDO_SURVEY_CODE){
        n_ready_must_do++;

        n_left=f->n_required-f->n_done;
        
        if(n_left<n_left_min_must_do){
         n_left_min_must_do=n_left;
        }

     }

     /* status of DO_NOW_STATUS  means must do now (i.e. darks or 2nd offset
        field).  Return field index */

     else if(f->status==DO_NOW_STATUS){
        n_do_now++;
        if(i_min_do_now==-1)i_min_do_now=i;
        if(f->shutter==DARK_CODE&&i_min_dark==-1)i_min_dark=i;
        if((f->shutter==DOME_FLAT_CODE||f->shutter==EVENING_FLAT_CODE||
          f->shutter==MORNING_FLAT_CODE)&&i_min_flat==-1)i_min_flat=i;
        /*return(i);*/
     }

#else
     /* status of DO_NOW_STATUS  means must do now (i.e. darks or 2nd offset
        field).  Return field index */

     if(f->status==DO_NOW_STATUS){
        n_do_now++;
        if(i_min_do_now==-1)i_min_do_now=i;
        if(f->shutter==DARK_CODE&&i_min_dark==-1)i_min_dark=i;
        if((f->shutter==DOME_FLAT_CODE||f->shutter==EVENING_FLAT_CODE||
          f->shutter==MORNING_FLAT_CODE)&&i_min_flat==-1)i_min_flat=i;
        /*return(i);*/
     }

     /* for any MUST-DO field with READY_STATUS, increment the count and update minimum
        value of n_left */

     else if (f->status==READY_STATUS&&f->survey_code==MUSTDO_SURVEY_CODE){
        n_ready_must_do++;

        n_left=f->n_required-f->n_done;
        
        if(n_left<n_left_min_must_do){
         n_left_min_must_do=n_left;
        }

     }
#endif

     /* for any other field with READY_STATUS, increment the count and update minimum
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
        if(f->survey_code==MUSTDO_SURVEY_CODE)n_late_must_do++;
     }

     } //for(i=0;i<num_fields;i++){

     /* If there are MUST_DO fields with READY_STATUS, 
    choose the one that has least time left to complete the 
    required observations */       

     if(n_ready_must_do>0){
       if(verbose){
       fprintf(stderr,"get_next_field: checking %d ready must-do fields \n",n_ready_must_do);
       }
      
       if(verbose) {
      fprintf(stderr,"get_next_field: %d must-do fields ready\n",n_ready_must_do);
       }

       time_left_min=10000.0;
       i_min=-1;

       for(i=0;i<num_fields;i++){
     f=sequence+i;
     if(f->status==READY_STATUS&&f->survey_code==MUSTDO_SURVEY_CODE){
        n_left=f->n_required-f->n_done;
        if((f->n_required==6||n_left==n_left_min_must_do)&&f->time_left<time_left_min){
         i_min=i;
         time_left_min=f->time_left;
        }
     }
       }

       if(verbose){
      fprintf(stderr,"get_next_field: returning ready must-do field : %d\n",i_min);
       }
       (sequence+i_min)->selection_code = LEAST_TIME_READY_MUST_DO;
       return(i_min);

     }

      /* If there are MUST-DO fields with TOO_LATE_STATUS, choose the one
    that has the least time left.  Shorten the interval so 
    that time_left=0.  If still doable, choose this field*/

     if (n_late_must_do>0){
    if(verbose1){
       fprintf(stderr,"get_next_field: checking %d too-late must-do fields \n",n_late_must_do);
    }

    if(verbose1) {
      fprintf(stderr,"get_next_field: %d must-do late fields\n",n_late_must_do);
    }

    i_min=-1;
    time_left_min=10000.0;
    for(i=0;i<=num_fields;i++){
      f=sequence+i;
      if(f->status==TOO_LATE_STATUS&&f->survey_code==MUSTDO_SURVEY_CODE&&f->time_left<time_left_min){
          time_left_min=f->time_left;
          i_min=i;
      }
    }

    // This shouldn't happen
    if(i_min<0){
       fprintf(stderr,
          "ERROR: get_next_field: n_late_must_do [%d] > 0 but non appear in the field list\n",
          n_late_must_do);
       fflush(stdout);
       fflush(stderr);
       //return(-1);
    }
       
    if(verbose1){
       fprintf(stderr,
        "get_next_field: choosing field %d to shorten intervals\n",
         i_min);
    }
       
    f=sequence+i_min;
    shorten_interval(f);
    update_field_status(f,jd,bad_weather);

    if(verbose1){
       fprintf(stderr,
          "get_next_field: interval shortened to %10.6f\n",
          f->interval*3600.0);
    }
    if(verbose1){
      fprintf(stderr,"get_next_field: returning late must-do field : %d\n",i_min);
    }
    (sequence+i_min)->selection_code = LEAST_TIME_LATE_MUST_DO;
    return(i_min);
     }

 
     /* If there were fields with DO_NOW_STATUS, choose the first flat,
    or else the first dark, or else the first field */

     if(n_do_now>0){
    if(verbose1){
       fprintf(stderr,"get_next_field: checking %d do_now fields\n",n_do_now);
    }

    if(i_min_flat>=0){
          if(verbose1){
          fprintf(stderr,"get_next_field: returning i_min_flat: %d\n",i_min_flat);
          }
          (sequence+i_min_flat)->selection_code = FIRST_DO_NOW_FLAT;
          return(i_min_flat);
    }
    else if(i_min_dark>=0){
          if(verbose1){
          fprintf(stderr,"get_next_field: returning i_min_dark: %d\n",i_min_dark);
          }
          (sequence+i_min_dark)->selection_code = FIRST_DO_NOW_DARK;
          return(i_min_dark);
    }
    else{
          if(verbose1){
          fprintf(stderr,"get_next_field: returning i_min_do_now: %d\n",i_min_do_now);
          }
          (sequence+i_min_do_now)->selection_code = FIRST_DO_NOW;
          return(i_min_do_now);
    }
     }
  
     /* if the pair to the previous fields is doable, choose the paired field */

     if(f_prev!=NULL&&paired_fields(f_next,f_prev)&&f_next->doable){
    if(verbose1){
        fprintf(stderr,"get_next_field: checking for doable pair to previous field %d\n",i_prev);
    }

    if(verbose1){
        fprintf(stderr,"get_next_field: field %d is paired with field %d \n",
            i_prev+1,i_prev);
    }
    if(f_next->status==READY_STATUS){
        if(verbose1){
        fprintf(stderr,"get_next_field: returning paired field %d \n", i_prev+1);
        }
        (sequence+i_prev+1)->selection_code = FIRST_READY_PAIR;
        return(i_prev+1);
    }
    else if(f_next->status==TOO_LATE_STATUS){
      shorten_interval(f_next);
      update_field_status(f_next,jd,bad_weather);
      if(f_next->status==READY_STATUS){
         if(verbose1){
        fprintf(stderr,"get_next_field: returning late paired field %d \n", i_prev+1);
         }
         (sequence+i_prev+1)->selection_code = FIRST_LATE_PAIR;
         return(i_prev+1);
      }
      else{
         if(verbose1){
           fprintf(stderr,"get_next_field: returning  not-ready paired field %d \n", i_prev+1);
         }
         (sequence+i_prev+1)->selection_code = FIRST_NOT_READY_LATE_PAIR;
         return(i_prev+1);
      }
    }
    else{
         if(verbose1){
           fprintf(stderr,
          "get_next_field: returning paired field %d that is neither ready nor too late\n",
          i_prev+1);
         }
         (sequence+i_prev+1)->selection_code = FIRST_NOT_READY_NOT_LATE_PAIR;
         return(i_prev+1);
    }
     }

     /* If there are fields with READY_STATUS, choose the one 
    that has least time left to complete the required observations */

     if(n_ready>0){
     if(verbose1){
        fprintf(stderr,"get_next_field: checking %d ready fields \n",n_ready);
     }
    
     time_left_min=10000.0;
     i_min=-1;

     for(i=0;i<num_fields;i++){
       f=sequence+i;
       if(f->status==READY_STATUS){
          n_left=f->n_required-f->n_done;
          if(n_left==n_left_min&&f->time_left<time_left_min){
           i_min=i;
           time_left_min=f->time_left;
          }
       }
     }
     if(verbose1){
        fprintf(stderr,"get_next_field: returning ready field : %d\n",i_min);
     }

     (sequence+i_min)->selection_code = LEAST_TIME_READY;
     return(i_min);

     }

     /* If there are no observable fields ready to observe,  but there are
    fields with TOO_LATE_STATUS, choose the first one with MUSTDO_SURVEY_CODE, or
    else the field that has the most time left. Shorten the interval so 
    that time_left=0.  If still doable, choose this field. Otherwise return -1 */

     if (n_late>0){

    if(verbose1){
        fprintf(stderr,"get_next_field: checking %d late fields \n",n_late);
    }

    i_max=-1;
    time_left_max=-1000;
    for(i=0;i<=num_fields;i++){
      f=sequence+i;
      if(f->status==TOO_LATE_STATUS&&f->time_left>time_left_max){
          time_left_max=f->time_left;
          i_max=i;
      }
    }


    if(i_max<0){
       if(verbose1)fprintf(stderr,"get_next_field: No fields to shorten\n");
       //return(-1);
    } 
    else{
       
      if(verbose1){
         fprintf(stderr,
          "get_next_field: choosing field %d to shorten intervals\n",
           i_max);
      }
         
      f=sequence+i_max;
      shorten_interval(f);
      update_field_status(f,jd,bad_weather);
      if(f->status==READY_STATUS){

         if(verbose1){
        fprintf(stderr,
           "get_next_field: interval shortened to %10.6f\n",
           f->interval*3600.0);
         }
         (sequence+i_max)->selection_code = MOST_TIME_READY_LATE;
         return(i_max);
      }
      else{
         if(verbose1){
        fprintf(stderr,
           "get_next_field: could not shorten interval of field %d\n",
           i_max);
         }
      }  // if (f->status==READY_STATUS
    } //if(i_max<0){
 
     }  // if(n_late>0)

     if(verbose) {
    fprintf(stderr,"get_next_field: No fields to observe\n");
     }
     return(-1);

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

int  get_field_status_string(Field *f, char *string)
{
    switch (f->status){

    case TOO_LATE_STATUS -1:
      sprintf(string,"Too late");
      break;
    case NOT_DOABLE_STATUS -1:
      sprintf(string,"Not doable");
      break;
    case READY_STATUS -1:
      sprintf(string,"Ready");
      break;
    case DO_NOW_STATUS -1:
      sprintf(string,"Do now");
      break;
    default:
      sprintf(string,"Unknown status");
      break;
    }

    return (0);
}

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

#if 1
      else if (f->jd_next-jd>(MIN_EXECUTION_TIME/24.0)){
#else
      else if (f->jd_next-jd>f->interval/(2.0*24.0)){
#endif
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

    f->time_up=(f->jd_set-jd)*24.0;

    /* time left is time up - time required to finish the
       observations.  */

    f->time_left=f->time_up-f->time_required; 
      
    if(f->time_left<0){
          f->status=TOO_LATE_STATUS;
#if DEBUG
  if ( f->field_number > 37 && f->field_number < 47 ){
    fprintf(stderr,"update_field_status: field: %d jd: %12.6f time_required: %10.6f  jd_set: %12.6f time_up: %10.6f  time_left: %10.6f  status: %s\n",
    f->field_number, jd, f->time_required, f->jd_set, f->time_up, f->time_left, "TOO_LATE");
  }
#endif
          return(TOO_LATE_STATUS);
    }
    else{
        f->status=READY_STATUS;
#if DEBUG
  if ( f->field_number > 37 && f->field_number < 47 ){
    fprintf(stderr,"update_field_status: field: %d jd: %12.6f time_required: %10.6f  jd_set: %12.6f time_up: %10.6f  time_left: %10.6f  status: %s\n",
    f->field_number, jd, f->time_required, f->jd_set, f->time_up, f->time_left, "READY_STATUS");
  }
#endif
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
        Site_Params *site, double jd,
        Telescope_Status *tel_status)
{
    int i,j,n_observable,n_up_too_short,n_moon_too_close,n_never_rise;
    int n_moon_too_close_later;
    int n_same_ra;
    Field *f;
    double am,ha,time_up,time_required;
    double dark_night_duration, whole_night_duration;
    double ra_prev,current_epoch;
    double max_airmass;
    double max_hourangle;
    double new_jd_start=0.0;
    double new_lst_start=0.0;

    n_up_too_short=0;
    n_moon_too_close=0;
    n_moon_too_close_later=0;
    n_never_rise=0;
    n_same_ra=1;
    ra_prev=0.0;
    
    /* if initializing fields after jd_start, set jd_start to
       current jd */

    if (jd > nt->jd_start){
    new_jd_start = jd;
    new_lst_start = nt->lst_start+(new_jd_start-nt->jd_start)*SIDEREAL_DAY_IN_HOURS;
    if(new_lst_start > 24.0) new_lst_start = new_lst_start - 24.0;
    if(verbose){
        fprintf(stderr,
            "adjusting jd_start from %10.6f to %10.6f\n",
            nt->jd_start-2450000,new_jd_start-2450000);
        fprintf(stderr,
            "adjusting lst_start from %10.6f to %10.6f\n",
            nt->lst_start,new_lst_start);
    }
    nt->lst_start=new_lst_start;
    nt->jd_start=new_jd_start;
    }

    /* calculate length of dark night remaining, and length of
       whole night remaining (time until sunrise) */

    dark_night_duration=(nt->jd_end-nt->jd_start)*24.0;
    whole_night_duration=(nt->jd_sunrise-jd)*24.0;

    if(verbose){
     fprintf(stderr,"current lst: %10.6f\n",tel_status->lst);
     fprintf(stderr,"current jd: %10.6f\n",jd-2450000);
     fprintf(stderr,"ut_start: %10.6f",nt->ut_start);
     fprintf(stderr,"ut_end: %10.6f\n",nt->ut_end);
     fprintf(stderr,"jd_start: %10.6f\n",nt->jd_start-2450000);
     fprintf(stderr,"jd_end: %10.6f\n",nt->jd_end-2450000);
     fprintf(stderr,"lst_start: %10.6f\n",nt->lst_start);
     fprintf(stderr,"lst_end: %10.6f\n",nt->lst_end);
     fprintf(stderr,"dark night duration : %10.6f\n",dark_night_duration);
     fprintf(stderr,"whole night duration : %10.6f\n",whole_night_duration);
    }

    n_observable=0;
    for (i=0;i<num_fields;i++){

    f=sequence+i;
    f->n_done=0;
    f->status=0;
    f->selection_code = NOT_SELECTED;
    for(j=0;j<f->n_required;j++){
       f->ut[j]=0.0;
       f->jd[j]=0.0;
       f->lst[j]=0.0;
       f->ha[j]=0.0;
       f->am[j]=0.0;
    }

    if(verbose1){
       fprintf(stderr,"checking field %d at ra %12.6f dec %12.6f\n",
             i,f->ra,f->dec);
    }

    /* get rise and set times of given position (the jd when the
       airmass crosses below and above the maximum airmass, MAX_AIRMASS). 
       If the object is already up at the start of the observing window
       (nt.jd_start), set jd_rise to nt.jd_start. If it is up at the 
       end of the observing window (nt.jd_end), set the jd_set to 
       nt.jd_end. If the object is not up at all during the observing 
       window, set jd_rise and jd_set to -1.  These are irrelevant
       for darks, flats, focus fields, and offset pointing */

    max_airmass=MAX_AIRMASS;
    max_hourangle=MAX_HOURANGLE;
    f->jd_rise=get_jd_rise_time(f->ra,f->dec,max_airmass,
        max_hourangle, nt,site,&am, &ha);
    f->jd_set=get_jd_set_time(f->ra,f->dec,max_airmass,
        max_hourangle, nt,site,&am, &ha);
    f->ut_rise = nt->ut_start + (f->jd_rise - nt->jd_start)*24.0;
    f->ut_set = nt->ut_start + (f->jd_set - nt->jd_start)*24.0;
    
    galact(f->ra,f->dec,2000.0,&(f->gal_long),&(f->gal_lat));
    eclipt(f->ra,f->dec,2000.0,nt->jd_start,&(f->epoch),&(f->ecl_long),&(f->ecl_lat));

    /* calculate total time object will be observable (jd_set-jd_rise)
       and the total time required to make all the observations. Again,
       these are irrelevant for darks, flats, focus fields, and offset fields */

    f->time_up=(f->jd_set-f->jd_rise)*24.0;
    f->time_required=(f->n_required-1)*f->interval;
    f->time_left=f->time_up-f->time_required; 

    /* Determine if the observation is doable (i.e. it rises during the
       night and there is enough time to do all the observations while
       it is up). If so, set doable=1 and initialize the jd_next to the
       earliest possible time to observe the position (either jd_start or
       jd_rise).  If not, set doable=0 and  jd_next = -1. */


    /* darks with 0 declination are  dark time only */

    if(f->shutter==DARK_CODE&&f->dec==0.0){
       if(jd<nt->jd_end){
           n_observable++;
           f->doable=1;
           f->jd_rise=nt->jd_start;
           if(jd>f->jd_rise){
          f->jd_next=jd;
          f->jd_rise=jd;
           }
           else{
          f->jd_next=f->jd_rise;
           }
           f->jd_set=nt->jd_end;
           f->time_up=(f->jd_set-f->jd_next)*24.0;
           f->time_left=(f->jd_set-jd)*24.0;

           if(verbose){
          fprintf(stderr,"field: %d  night-time dark\n",
          f->field_number);
           }
       }
       else{
           if(verbose){
          fprintf(stderr,"skipping field %d, night-time has ended\n",
          f->field_number);
           }
       }
    }

    /* darks with declinration -1.0  are evening twilight only */

    else if(f->shutter==DARK_CODE&&f->dec==-1.0){
       if(jd<nt->jd_start){
           n_observable++;
           f->doable=1;
           f->jd_rise=nt->jd_sunset+DARK_WAIT_TIME;
           if(jd>f->jd_rise){
          f->jd_next=jd;
          f->jd_rise=jd;
           }
           else{
          f->jd_next=f->jd_rise;
           }
           f->jd_set=nt->jd_start;
           f->time_up=(f->jd_set-f->jd_next)*24.0;
           f->time_left=(f->jd_set-jd)*24.0;

           if(verbose){
          fprintf(stderr,"field: %d  evening dark\n",
          f->field_number);
           }
       }
       else{
           if(verbose){
          fprintf(stderr,"skipping field %d, evening twilight has ended\n",
          f->field_number);
           }
       }
      }

    /* darks with declination +1.0  are morning twilight only */

    else if(f->shutter==DARK_CODE&&f->dec==1.0){
       if(jd<nt->jd_sunrise){
           n_observable++;
           f->doable=1;
           f->jd_set = nt->jd_sunrise-DARK_WAIT_TIME;
           if(jd>nt->jd_end){
           f->jd_next=jd;
           f->jd_rise=jd;
           f->time_up=(f->jd_set-jd)*24.0;
           f->time_left=f->time_up;
           }
           else{
           f->time_up=(f->jd_set-nt->jd_end)*24.0;
           f->jd_next=nt->jd_end;
           f->jd_rise=nt->jd_end;
           f->time_left=(f->jd_set-jd)*24.0;
           }

           if(verbose){
          fprintf(stderr,"field: %d  morning dark\n",
          f->field_number);
           }
        }
        else{
           if(verbose){
          fprintf(stderr,"skipping field %d, morning twilight has ended\n",
          f->field_number);
           }
        }
      }

    /* darks with dec != -1,0,+1 and dome flats are always doable.  Set time left to whole 
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
           fprintf(stderr,"field: %d %10.6f %10.6f darks\n",
            f->field_number,f->ra,f->dec);
           }
           else if(f->shutter==DOME_FLAT_CODE){
           fprintf(stderr,"field: %d %10.6f %10.6f dome flat\n",
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
              fprintf(stderr,"field: %d %10.6f %10.6f focus\n",
              f->field_number,f->ra,f->dec);
          }
          else if(f->shutter==OFFSET_CODE){
              fprintf(stderr,"field: %d %10.6f %10.6f offset\n",
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
           f->time_up=(f->jd_set-f->jd_next)*24.0;
           f->time_left=(f->jd_set-jd)*24.0;

           if(verbose){
          fprintf(stderr,"field: %d %10.6f %10.6f evening flat\n",
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
           f->time_up=(f->jd_set-jd)*24.0;
           f->time_left=f->time_up;
           }
           else{
           f->time_up=(f->jd_set-nt->jd_end)*24.0;
           f->jd_next=nt->jd_end;
           f->jd_rise=nt->jd_end;
           f->time_left=(f->jd_set-jd)*24.0;
           }

           if(verbose){
          fprintf(stderr,"field: %d %10.6f %10.6f morning flat\n",
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

       if(verbose)fprintf(stderr,"field: %d %10.6f %10.6f never rises\n",
            f->field_number,f->ra,f->dec);

    }

       /* Field too close to tonight's moon. Not doable */
    else if (moon_interference(f,nt,MIN_MOON_SEPARATION)){
        n_moon_too_close++;
        f->doable=0;
        f->jd_next=-1;

        if(verbose)fprintf(stderr,
        "field: %d %10.6f %10.6f moon too close\n",
        f->field_number,f->ra,f->dec);
 
    }

    else if (f->dec>MAX_DEC){
       f->doable=0;
       if(verbose)fprintf(stderr,"field: %d %10.6f %10.6f dec too high\n",
        f->field_number,f->ra,f->dec);
    }

    else if (f->dec<MIN_DEC){
       f->doable=0;
       if(verbose)fprintf(stderr,"field: %d %10.6f %10.6f dec too high\n",
        f->field_number,f->ra,f->dec);
    }

    /* not enough time for required obs. Not doable (unless it is a must do field) */
    else if(f->survey_code!=MUSTDO_SURVEY_CODE&&f->time_left<0){ 
       n_up_too_short++;
       f->doable=0;
       f->jd_next=-1;

       if(verbose)fprintf(stderr,"field: %d %10.6f %10.6f up too short\n",
        f->field_number,f->ra,f->dec);

    }


    /* below 30 deg galactic latitude, too much extinction for supernove */
    else if (f->survey_code==SNE_SURVEY_CODE&&fabs(f->gal_lat)<15.0){
       f->doable=0;
       if(verbose)fprintf(stderr,"field: %d %10.6f %10.6f galactic lat too low: %10.6f\n",
        f->field_number,f->ra,f->dec,f->gal_lat);
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
          "%d field: %d %10.6f %10.6f %10.6f %d jd_rise: %9.6f  jd_set: %9.6f next: %9.6f  time_up : %10.6f time_required: %10.6f time_left: %10.6f survey_code: %d ut_rise: %10.6f  ut_set: %10.6f\n",
          f->doable,f->field_number,f->ra,f->dec,
          f->expt,f->shutter,f->jd_rise-2450000,f->jd_set-2450000,
          f->jd_next-2450000,f->time_up, f->time_required, f->time_left,
          f->survey_code,f->ut_rise,f->ut_set);
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
fprintf(stderr,"field: %10.6f %10.6f moon: %10.6f %10.6f  dra, ddec, dmoon: %10.6f %10.6f %10.6f\n",
f->ra,f->dec,nt->ra_moon,nt->dec_moon, dra,ddec,dmoon);
#endif

    if(dmoon<min_separation)return(1);
     }
 
     return(0);
}

/************************************************************/

/* If object is already up at current jd, return current jd value.
   If it rises before the end of the night, return with the rise time.
   If it never rises, return -1
*/

double get_jd_rise_time(double ra,double dec, double max_am, double max_ha,
       Night_Times *nt, Site_Params *site, double *am, double *ha)
{
    double lst,jd,current_jd,dt;

    /* get lst, hour angle, jd, and airmass at start time of observing window.
       If am is less than max_am and absolute value of ha is less than max_am, 
       object is already up. Return with jd.
       Otherwise, stepwise search from jd_start to jd_end to find the 
       time when am < max_am and fabs(ha) < max_ha (the rise time)
       If it never rises, return -1. Otherwise return the jd when it does.
    */

    lst=nt->lst_start;
    jd=nt->jd_start;

    current_jd = get_jd();


    *ha=get_ha(ra,lst);
    *am=get_airmass(*ha,dec,site);

    if(verbose1){
       fprintf(stderr,"jd_start: %12.6f  lst_start: %10.6f\n",jd,lst);
       fprintf(stderr,"ra: %12.6f  dec: %12.6f\n",ra,dec);
       fprintf(stderr,"ha: %10.6f   am: %10.6f\n",*ha,*am);
    }

    if (*am < max_am && fabs (*ha) < max_ha ) {
       if(verbose1){
      fprintf(stderr,"init am and ha within limits. returning with jd = %12.6f\n",jd);
       }
       return (jd);
    }
     
    if(verbose1){
       fprintf(stderr,"init_fields: searching for jd at rise time\n");
    }

      
    while(jd<nt->jd_end&&(*am>max_am||fabs(*ha)>max_ha)){
    jd=jd+(LST_SEARCH_INCREMENT/SIDEREAL_DAY_IN_HOURS);
    lst=lst+LST_SEARCH_INCREMENT;
    if(lst>24.0)lst=lst-24.0;
    *ha=get_ha(ra,lst);
    *am=get_airmass(*ha,dec,site);
    //if(verbose1){
    //    fprintf(stderr,"jd: %12.6f lst: %10.6f am: %10.6f ha: %10.6f\n",
    //       jd,lst,*am,*ha);
    //}
    }

    if(*am>max_am){
       if(verbose1){
      fprintf(stderr,"field never rises below am %10.6f\n",max_am);
       }
       return(-1.0);
    }
    else if(fabs(*ha)>max_ha){
       if(verbose1){
      fprintf(stderr,"field ha is always greater than  %10.6f\n",max_ha);
       }
       return(-1.0);
    }
    else{
       if(verbose1){
      dt = (jd-current_jd)*24.0;
      fprintf(stderr,"field rises at jd  %10.6f (in %10.6f h)\n",jd,dt);
       }
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
    int n,n_fields,line,n1;
    char string[STR_BUF_LEN+1],*s_ptr,shutter_flag[3],s[256];
    int string_length=0;
    Field *f;

    /* if file can not be opened for reading, return error. Otherwise
     * load any new sequences
    */

    input=fopen(script_name,"r");
    if (input==NULL){
       fprintf(stderr,"load_sequence: can't open file %s\n",script_name);
       return(-1);
    }

    n_fields=0;
    line=0;
    string[STR_BUF_LEN-1]=0;
    while(fgets(string,STR_BUF_LEN,input)!=NULL){

      line++;
      // make sure last element if string is still 0. If not, the line read from the input is
      // longer than buffer length (STR_BUF_LEN)
      if(string[STR_BUF_LEN-1]!=0){
      fprintf(stderr,"load_sequence: WARNING: sequence line [%d] is too long. Ignoring \n",line);
      fflush(stderr);
      string[STR_BUF_LEN-1]=0;
      }
      else{

    /* get rid of leading spaces */
    s_ptr=string;
    while(strncmp(s_ptr," ",1)==0&&*s_ptr!=0)s_ptr++;

    /* get length of string, starting at s_ptr */
    string_length = strlen(s_ptr);

    /* add a space to the end of s_ptr to make sure it is processed
     * correctly by camera server
    */

    sprintf(s_ptr+string_length," ");
    string_length++;

    /* if there are more characters left in the string, and if the
       current character is not "#", then read in the next line */

     if (string_length<=1){
       /* line too short, pass */
       if(verbose){
         fprintf(stderr,"WARNING: line [%d] is too short [%s]\n",line,s_ptr);
       }
     }
     else if (strncmp(s_ptr,"#",1)==0){
       /* comment line. pass */
       if(verbose){
         fprintf(stderr,"line [%d] is a commented out [%s]\n",line,s_ptr);
       }
     }
     else if(strncmp(s_ptr,"FILTER",6) == 0 || strncmp(s_ptr,"filter",6)==0 ){
       sscanf(s_ptr,"%s %s",s,filter_name);
       filter_name_ptr=filter_name;
       if(check_filter_name(filter_name)!=0){
         fprintf(stderr,"WARNING: unexpeced filter name: %s",filter_name);
       }
     }
     else{

        f=sequence+n_fields;
        f->field_number=n_fields;
        f->line_number=line;
        strcpy(f->script_line,string);

        n=sscanf(s_ptr,"%lf %lf %s %lf %lf %d %d",
          &(f->ra),&(f->dec),shutter_flag,&(f->expt),&(f->interval),
          &(f->n_required),&(f->survey_code));

        if (f->survey_code == LIGO_SURVEY_CODE)f->survey_code = MUSTDO_SURVEY_CODE;
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

        else if(n!=7||f->ra<0.0||f->ra>24.0||f->dec<-90.0||f->dec>90.0||
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
     } //if (strlen<=1){
      } //if(string[STR_BUF_LEN-1]!=0{
    } //while(fgets(string,STR_BUF_LEN,input)!=NULL){

    fclose(input);

    return(n_fields);

}
/************************************************************/

/* if name is an expected filter name (see  FILTER_NAME in scheduler.h)
 * return 0. Otherwise return -1
*/

int check_filter_name(char *name)
{
    for (enum Filter_Index i =1; i<= NUM_FILTERS; i++){
      if(strcmp(name, FILTER_NAME[i-1]) == 0) return(0);
    }
    return(-1);
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

    fprintf(output,"Required : %d  Done: %d Interval : %10.6f LSTs : ",
        f->n_required,f->n_done,f->interval);
    for(i=0;i<f->n_done;i++){
    fprintf(output,"%10.6f ",f->lst[i]);
    }
#if 0
    fprintf(output," dLSTs: ");
    for(i=1;i<f->n_done;i++){
    dt=f->lst[i]-f->lst[i-1];
    if(dt<0.0)dt=dt+24.0;
    fprintf(output,"%10.6f ",dt);
    }
#endif
    fprintf(output," HAs: ");
    for(i=0;i<f->n_done;i++){
    fprintf(output,"%10.6f ",f->ha[i]);
    }


    fprintf(output," gal. lat: %10.6f ",f->gal_lat);
    fprintf(output," ecl. lat: %10.6f ",f->ecl_lat);

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
    char string[STR_BUF_LEN];
   
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

/*************************************************************************/

