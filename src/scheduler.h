/* scheduler.h

   Include files and defines for scheduler.c

   DLR 2007 Mar 5

*/


#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include "sky_utils.h"
#include "socket.h"
#include "scheduler_camera.h"

#define False false
#define True true

#define SNE_SHIFT 0 /* set to 1 to shift paired fields by 1.0 deg, 0 for 0.5 deg */
#define FAKE_RUN 0 /* set to 1 for simulated obs */
#define UT_OFFSET 0.00 /* ut offset for debugging */
#define DEEP_DITHER_ON 0 /* turn on dithering for deep coadds */
#
#define USE_TELESCOPE_OFFSETS 0 /* change to 1 to use telescope_offsets file for offset */

#define DEBUG 0
/*#define POINTING_TEST*/ /* define to break up all exposures longer than LONG_EXPOSURE,
                         both west and east of the meridian */

#define POINTING_CORRECTIONS_ON 0 /* set to 1 to apply empirical pointing corrections*/
#define TRACKING_CORRECTIONS_ON 0 /* set to 1 to apply empirical tracking corrections*/

#define DEG_TO_RAD (3.14159/180.0)
#define SIDEREAL_DAY_IN_HOURS 23.93446972

#define SKYFLAT_WAIT_TIME (0.5/24) /* wait half hour after sunset or stop
                                      half hour before sunset for skyflats */

#define DARK_WAIT_TIME (0.0/24) /* wait half hour after sunset or stop
                                      half hour before sunset for skyflats */


#define HISTORY_FILE "survey.hist"
#define SELECTED_FIELDS_FILE "fields.completed"
#define LOG_OBS_FILE "log.obs"
#define OBS_RECORD_FILE "scheduler.bin"  /* binary record of fields */

#define DEGTORAD 57.29577951 /* 180/pi */
//#define LST_SEARCH_INCREMENT 0.0166 /* 1 minute in hours */
#define LST_SEARCH_INCREMENT 0.00166 /* 1 minute in hours */
#define FAKE_RUN_TIME_STEP  0.0167 /* 1 minute in hours */

#define MAX_AIRMASS 2.0
#define MAX_HOURANGLE 4.3
#define MAX_OBS_PER_FIELD 100 /* maximum number of observations per field */
#define MAX_FIELDS 500  /* maximum number of fields per script */
#define OBSERVATORY_SITE "La Silla"
#define MAX_EXPT 1000.0
#define MAX_INTERVAL (43200.0/3600.0)
#define MIN_INTERVAL /*(900.0/3600.0)*/ 0.0
#if 1
#define MIN_DEC -89.0 /* no decs lower than this */
#else
/* put in conservative lower limit to dec to prevent cables from
   getting tangled when telescope points far south */
#define MIN_DEC -45.0 /* no decs lower than this */
#endif
#define MAX_DEC 30.0 /* no decs higher than this */
#define MIN_FOCUS 24.0 /* no focus setting (mm) less than this */
#define MAX_FOCUS 28.0/* no focus setting (mm) less than this */
#define MIN_FOCUS_INCREMENT 0.025 /* no focus increment (mm) less than this */
#define MAX_FOCUS_INCREMENT 0.10 /* no focus increment (mm) less than this */
#define MAX_FOCUS_CHANGE 0.3 /* maximum change from expected default mm */
#define MIN_MOON_SEPARATION 15.0 /* minimum pointing separation (deg) from moon */
#define MAX_BAD_READOUTS 3 /* quit trying exposure after this many bad readouts */
#ifdef POINTING_TEST
#define LONG_EXPTIME (60.0/3600.0) /* expsure time longer than this must be split into
				       shorter exposure times west of the meridian */
#else
/* Used to use 100.0/3600.0 before tracking rate fix */
/*#define LONG_EXPTIME (100.0/3600.0)*/ /* expsure time longer than this must be split into
				       shorter exposure times west of the meridian */
/*#define LONG_EXPTIME (300.0/3600.0) */ /* expsure time longer than this must be split into
				       shorter exposure times west of the meridian */
#define LONG_EXPTIME (3600.0/3600.0) /* expsure time longer than this must be split into
				       shorter exposure times west of the meridian */
#endif
                               

#define CLEAR_INTERVAL 0.1 /* hours since last exposure to start  clear */

#define USE_12DEG_START 1 /* 1 to use 12-deg twilight, 0 to use 18-deg twilight */
#define STARTUP_TIME /*0.5*/0.0 /* hours to startup after end of twilight */
#define MIN_EXECUTION_TIME 0.029 /* minimum time (hours) to make an observation */
#define FOCUS_OVERHEAD 0.00555 /* time to change focus (hours) = 20 sec */

#define FLAT_DITHER_STEP 0.002778 /* 10 arcsec in deg */
#define DEEPSEARCH_DITHER_STEP 0.001389 /* 5 arcsec in deg */

#if SNE_SHIFT
#define RA_STEP0 0.10 /*0.06666*/ /* hours difference in RA between paired fields */
#else
#define RA_STEP0 0.05 /*0.03333*/ /* hours difference in RA between paired fields */
#endif

#define FILENAME_LENGTH 16 /* length of prefix to FITS image file names 
                           (yyyymmddhhmmssx) */

#define STR_BUF_LEN 1024 /* buffer size for various text strings */

/* field status values */

#define TOO_LATE_STATUS -1
#define NOT_DOABLE_STATUS 0
#define READY_STATUS 1
#define DO_NOW_STATUS 2

/* nominal focus start, increment, and default setting (mm) */
#define NOMINAL_FOCUS_START 25.30
#define NOMINAL_FOCUS_INCREMENT 0.05
#define NOMINAL_FOCUS_DEFAULT 25.30

#define NUM_FOCUS_ITERATIONS 2 /* number of times to send focus command on each change
                                  of focus */
#define MAX_FOCUS_DEVIATION 0.05 /* maximum difference between set focus and returned
                                    focus */

// selection codes set by get_next_field()
enum Selection_Code {NOT_SELECTED,FIRST_DO_NOW_FLAT, FIRST_DO_NOW_DARK, FIRST_DO_NOW, FIRST_READY_PAIR, FIRST_LATE_PAIR,
      FIRST_NOT_READY_LATE_PAIR, FIRST_NOT_READY_NOT_LATE_PAIR, LEAST_TIME_LATE_MUST_DO,
      LEAST_TIME_READY_MUST_DO, LEAST_TIME_READY, MOST_TIME_READY_LATE};

// define accepted filter names and enumerate an index for each filter name
#define FILTER_NAME (const char*[]) { "rgzz", "none", "fake", "clear", NULL }
enum Filter_Index { RGIZ_INDEX, NONE_INDEX, FAKE_INDEX, CLEAR_INDEX, NUM_FILTERS };


typedef struct {
    int status; /* 0 if not doable, 2 if must observe pronto, 1 if ready to observe,
                   -1 if not enough time to observe remaining fields */
    int doable; /* 1 if all required observations possible, 0 if not */
    enum Selection_Code selection_code;
    int field_number;
    int line_number;
    char script_line[STR_BUF_LEN];    
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
    int survey_code; /* type of survey field (TNO_SURVEY_CODE or SNE_SURVEY_CODE) */
    double ut_rise; /* ut (hrs) when field rises above airmass threshold */
    double ut_set;  /* ut (hrss) when field sets below airmass threshold */
    double jd_rise; /* jd (days) when field rises above airmass threshold */
    double jd_set;  /* jd (days) when field sets below airmass threshold */
    double jd_next; /* jd (days) for next observation */
    int n_done; /* number of observations completed */
    double time_up; /* total time (hours) object will be up (if not yet risen) 
                       or else the remaining time up  (already risen)*/
    double time_required; /* total time (hours) required to complete 
                             remaining observations */
    double time_left; /* time remaining (hours) before time_required 
                         exceeds time_up -- i.e. time_up-time_required */
    double ut[MAX_OBS_PER_FIELD]; /* ut (hours) of completed obs (start time) */
    double jd[MAX_OBS_PER_FIELD]; /* lst (hours) of completed obs */
    double lst[MAX_OBS_PER_FIELD]; /* lst (hours) of completed obs */
    double ha[MAX_OBS_PER_FIELD]; /* hour angle (hours) of completed obs  */
    double am[MAX_OBS_PER_FIELD]; /* airmass of completed obs  */
    double actual_expt[MAX_OBS_PER_FIELD]; /* actual exposure time (hours) of obs*/
    char filename[FILENAME_LENGTH*MAX_OBS_PER_FIELD]; /* filename prefix */
} Field;

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

typedef struct{
    double temperature;
    double humidity;
    double wind_speed;
    double wind_direction;
    double dew_point;
    int dome_states; /* -1 unknown, 0 closed, 1 is open */
} Weather_Info;

typedef struct {
    double lst; /* local siderial time in hours */
    char filter_string[256];
    double focus; /* focus setting in mm */
    int dome_status; /* 0 for closed, 1 for open */
    double ut; /* universal time in hours */
    double ra; /* RA pointing of telescope (hours, current epoch ?) */
    double dec; /* Dec pointing of telescope (deg, current epoch ?) */
    double ra_offset; /* Correction to RA pointing in degrees */
    double dec_offset; /* Correction to Dec  pointing in degrees */
    Weather_Info weather;
    double update_time;
} Telescope_Status;

typedef struct {
    unsigned char nostatus;
    unsigned char unknown;
    unsigned char idle;
    unsigned char exposing;
    unsigned char readout_pending;
    unsigned char fetch_pending;
    unsigned char reading;
    unsigned char fetching;
    unsigned char flushing;
    unsigned char erasing;
    unsigned char purging;
    unsigned char autoclear;
    unsigned char autoflush;
    unsigned char poweron;
    unsigned char poweroff;
    unsigned char powerbad;
    unsigned char error;
    unsigned char active;
    unsigned char errored;
} Controller_State;

enum {NOSTATUS, UNKNOWN, IDLE, EXPOSING, READOUT_PENDING, READING,
      FETCHING, FLUSHING, ERASING, PURGING, AUTOCLEAR, AUTOFLUSH,
      POWERON, POWEROFF, POWERBAD, FETCH_PENDING, ERROR, ACTIVE, ERRORED };

#define NUM_STATES 19

#define ALL_POSITIVE_VAL 15 /* all controllers have the positive state */
#define ALL_NEGATIVE_VAL 0  /* all controllers have the same negative flag */

typedef struct {
    bool ready;
    bool error;
    int error_code;
    char state[STR_BUF_LEN];
    char comment[STR_BUF_LEN];
    char date[STR_BUF_LEN];
    double read_time;
    int state_val[NUM_STATES];

} Camera_Status;

#define MAX_FITS_WORDS 100

typedef struct{
     char keyword[256];
     char value[256];
} Fits_Word;

typedef struct {
  int num_words;
  Fits_Word fits_word[MAX_FITS_WORDS];
} Fits_Header;

#define FILTERNAME_KEYWORD "filterna"
#define FILTERID_KEYWORD "filterid"
#define LST_KEYWORD "lst"
#define HA_KEYWORD "ha"
#define IMAGETYPE_KEYWORD "imagetyp"
#define DARKFILE_KEYWORD "darkfile"
#define FLATFILE_KEYWORD "flatfile"
#define SEQUENCE_KEYWORD "sequence"
#define RA_KEYWORD "tele-ra"
#define DEC_KEYWORD "tele-dec"
#define FOCUS_KEYWORD "focus"
#define FIELDID_KEYWORD "fieldid"
#define UJD_KEYWORD "ujd"
#define COMMENT_KEYWORD "comment"

/* survey codes */

/* MIN_SURVEY_CODE is minimum possible value of the survey code */
#define MIN_SURVEY_CODE 0
#define NO_SURVEY_CODE 0
#define TNO_SURVEY_CODE 1
#define SNE_SURVEY_CODE 2
#define MUSTDO_SURVEY_CODE 3
#define LIGO_SURVEY_CODE 4
/* MAX_SURVEY_CODE is maximum possible value of the survey code */
#define MAX_SURVEY_CODE 4

/* sequence shutter codes and strings (string is used in sequence file,
   code is representation in source code. Postive shutter codes require
   telescope pointing.  */

#define BAD_CODE -1
#define DARK_CODE 0
#define SKY_CODE 1
#define FOCUS_CODE 2
#define OFFSET_CODE 3
#define EVENING_FLAT_CODE 4
#define MORNING_FLAT_CODE 5
#define DOME_FLAT_CODE 6
#define LIGO_CODE 7

#define BAD_STRING "?"
#define DARK_STRING "N"
#define SKY_STRING "Y"
#define FOCUS_STRING "F"
#define OFFSET_STRING "P"
#define EVENING_FLAT_STRING "E"
#define MORNING_FLAT_STRING "M"
#define DOME_FLAT_STRING "L"

#define BAD_STRING_LC "?"
#define DARK_STRING_LC "d"
#define SKY_STRING_LC "s"
#define FOCUS_STRING_LC "f"
#define OFFSET_STRING_LC "p"
#define EVENING_FLAT_STRING_LC "e"
#define MORNING_FLAT_STRING_LC "m"
#define DOME_FLAT_STRING_LC "l"

#define BAD_FIELD_TYPE "unknown"
#define DARK_FIELD_TYPE "dark"
#define SKY_FIELD_TYPE "sky"
#define FOCUS_FIELD_TYPE "focus"
#define OFFSET_FIELD_TYPE "offset"
#define EVENING_FLAT_TYPE "pmskyflat"
#define MORNING_FLAT_TYPE "amskyflat"
#define DOME_FLAT_TYPE "domeskyflat"

int add_new_fields(Field *sequence, int num_fields,
		Field *new_sequence, int num_new_fields);

int load_obs_record(char *file_name, Field *sequence, FILE **obs_record);
int save_obs_record(Field *sequence, FILE *obs_record, int num_fields,
         struct tm *tm);

int adjust_date(struct date_time *date, int n_days);

int init_night(struct date_time date, Night_Times *nt, 
                      Site_Params *site, int print_flag);

int load_sequence(char *script_name, Field *sequence);

int check_weather(FILE *input, double jd, 
			struct date_time *date, Night_Times *nt);

int get_day_of_year(struct date_time *date);

int observe_next_field(Field *sequence, int index, int index_prev, 
                double jd, double *time_taken, 
		Night_Times *nt, bool wait_flag, FILE *output,
		Telescope_Status *tel_status,Camera_Status *cam_status,
                Fits_Header *fits_header, char *exp_mode);

int do_stop(double ut,Telescope_Status *status);

int do_stow(double ut,Telescope_Status *status);

int get_dither(int iteration, double *ra_dither, double *dec_dither, double step_size);

int init_fields(Field *sequence, int num_fields, 
                Night_Times *nt, Night_Times *nt_5day,
		Night_Times *nt_10day, Night_Times *nt_15day,
		Site_Params *site, double jd, Telescope_Status *tel_status);

int moon_interference(Field *f, Night_Times *nt, double separation);

double get_jd_rise_time(double ra,double dec, double max_am, double max_ha,
       Night_Times *nt, Site_Params *site, double *am, double *ha);

double get_jd_set_time(double ra,double dec, double max_am, double max_ha,
       Night_Times *nt, Site_Params *site, double *am, double *ha);

double get_airmass(double ha, double dec, Site_Params *site);

double get_ha(double ra, double lst);

double clock_difference(double h1,double h2);

int get_next_field(Field *sequence,int num_fields, int i_prev, double jd,
                    int bad_weather);

int paired_fields(Field *f1, Field *f2);

int update_field_status(Field *sequence, double jd, int bad_weather);
int  get_field_status_string(Field *f, char *string);


int shorten_interval(Field *f);

int print_field_status (Field *f, FILE *output);

int print_history(double lst,Field *sequence, int num_fields,FILE *output);

double get_lst();

int close_files();

int do_exit(int code);

int get_shutter_string(char *string, int shutter, char *description);

int get_shutter_code(char *string);

int advance_tm_day(struct tm *tm);

int leap_year_check(int year);

int check_filter_name(char *name);

/* from scheduler_telescope.c */
int init_telescope_offsets(Telescope_Status *status);
int get_telescope_offsets(Field *f, Telescope_Status *status);
int point_telescope(double ra, double dec,double ra_rate, double dec_rate);
int update_telescope_status(Telescope_Status *status);
int do_telescope_command(char *command, char *reply,int timeout, char *host);
int do_daytime_telescope_command(char *command, char *reply,int timeout, char *host);
int print_telescope_status(Telescope_Status *status, FILE *output);
double get_tm(struct tm *tm_out);
double get_ut();
double get_jd();
int focus_telescope(Field *f, Telescope_Status *status, double focus_default);
int get_filename(char *filename,struct tm *tm,int shutter);
double get_median_focus(char *file);
int set_telescope_focus(double focus);
int get_telescope_focus (double *focus);


/* from scheduler_camera.c */
int init_semaphores();
int take_exposure(Field *f, Fits_Header *header, double *actual_expt,
		    char *name, double *ut, double *jd,
		    bool wait_flag, int *exp_error_code, char *exp_mode);
int get_filename(char *filename,struct tm *tm,int shutter);
int imprint_fits_header(Fits_Header *header);
double wait_exp_done(int expt);
int init_camera();
int clear_camera();
int update_camera_status(Camera_Status *cam_status);
void *do_camera_command_thread(void *args);
int do_status_command(char *command, char *reply, int timeout_sec,int id, char *host);
int do_camera_command(char *command, char *reply, int timeout_sec, int id, char *host);
int do_command(char *command, char *reply, int timeout_sec, int port, int id, char *host);
int bad_readout();
int get_filename(char *filename,struct tm *tm,int shutter);
int wait_camera_readout(Camera_Status *status);
int print_camera_status(Camera_Status *status, FILE *output);
double expose_timeout (char *exp_mode, double exp_time, bool wait_flag);

/* from scheduler_status.c*/

int init_status_names();
int binary_string_to_int(char *binaryString) ;
int int_to_binary_string(int n, char *string);
int get_value_string(char *reply, char *keyword, char *separator, char *value_string);
int get_string_status(char *keyword, char *reply, char *status);
int parse_status(char *reply,Camera_Status *status);
int print_camera_status(Camera_Status *status,FILE *output);

/* from scheduler_corrections.c */

double get_ra_correction(double ha0, double ha);
double get_dec_correction(double ha0, double ha);
double get_ra_rate(double ha, double dec);
double get_dec_rate(double ha, double dec);

/* from scheduler_signals.c */

int install_signal_handlers();
void sigterm_handler();
void sigusr1_handler();
void sigusr2_handler();

/* from scheduler_fits.c */

int init_fits_header(Fits_Header *header);
int update_fits_header(Fits_Header *header, char *keyword, char *value);
int add_fits_word(Fits_Header *header, char *keyword, char *value);

/* from scheduler_socket.c */
int send_command(char *command, char *reply, char *machine, 
			int port, int timeout_sec);

/* from scheduler_telescope.c*/


int init_telescope_offsets(Telescope_Status *status);
int get_telescope_offsets(Field *f, Telescope_Status *status);
int focus_telescope(Field *f, Telescope_Status *status, double focus_default);
int stow_telescope();
int set_telescope_focus(double focus);
int get_telescope_focus(double *focus);
int stop_telescope();
int point_telescope(double ra, double dec, double ra_rate, double dec_rate);
int update_telescope_status(Telescope_Status *status);
int print_telescope_status(Telescope_Status *status,FILE *output);
int do_telescope_command(char *command, char *reply, int timeout, char *host);
int do_daytime_telescope_command(char *command, char *reply, int timeout, char *host);
int advance_tm_day(struct tm *tm);
int leap_year_check(int year);

extern double sin(),fabs();

/************************************************************/
