#ifndef __sky_utils_h
#define __sky_utils_h


#include <stdio.h>
#include <math.h>
#include <ctype.h>
#include <stdarg.h>
#include <string.h>

/* default observatory and timezone */

/*#define DEFAULT_OBSCODE 'p'*/ /* Palomar */
#define DEFAULT_OBSCODE 'e' /* La Silla */
#define FAKE_OBSCODE 'f' /* La Silla  + 12 h*/

/* a couple of the system-dependent magic numbers are defined here */

#define SYS_CLOCK_OK 1    /* 1 means ANSI-standard time libraries do work,
   2 means they don't.  This is used by compiler switches in file 5 and
   the main program.  */

#define LOG_FILES_OK 0  /* 1 means that log files are enabled.
			Any other value means they're not.  */

#define MAX_OBJECTS 500
#define MINSHORT -32767   /* min, max short integers and double precision */
#define MAXSHORT 32767
#define MAXDOUBLE 1.0e38
#define MINDOUBLE -1.0e38
#define BUFSIZE 150

/* some (not all) physical, mathematical, and astronomical constants
   used are defined here. */

#ifndef PI
#define  PI                3.14159265358979
#endif

#define  ARCSEC_IN_RADIAN  206264.8062471
#define  DEG_IN_RADIAN     57.2957795130823
#define  HRS_IN_RADIAN     3.819718634205
#define  KMS_AUDAY         1731.45683633   /* km per sec in 1 AU/day */
#define  SS_MASS           1.00134198      /* solar system mass in solar units */
#define  J2000             2451545.        /* Julian date at standard epoch */
#define  SEC_IN_DAY        86400.
#define  FLATTEN           0.003352813   /* flattening of earth, 1/298.257 */
#define  EQUAT_RAD         6378137.    /* equatorial radius of earth, meters */
#define  ASTRO_UNIT        1.4959787066e11 /* 1 AU in meters */
#define  RSUN              6.96000e8  /* IAU 1976 recom. solar radius, meters */
#define  RMOON             1.738e6    /* IAU 1976 recom. lunar radius, meters */
#define  PLANET_TOL        3.          /* flag if nearer than 3 degrees
						to a major planet ... */
#define  KZEN              0.172       /* zenith extinction, mag, for use
				     in lunar sky brightness calculations. */
#define FIRSTJD            2415387.  /* 1901 Jan 1 -- calendrical limit */
#define LASTJD             2488070.  /* 2099 Dec 31 */

/* MAGIC NUMBERS which might depend on how accurately double-
   precision floating point is handled on your machine ... */

#define  EARTH_DIFF        0.05            /* used in numerical
   differentiation to find earth velocity -- this value gives
   about 8 digits of numerical accuracy on the VAX, but is
   about 3 orders of magnitude larger than the value where roundoff
   errors become apparent. */

#define  MIDN_TOL          0.00001         /* this is no longer
   used -- it was formerly
   how close (in days) a julian date has to be to midnight
   before a warning flag is printed for the reader.  VAX
   double precision renders a Julian date considerably
   more accurately than this.  The day and date are now based
   on the same rounding of the julian date, so they should
   always agree. */

/*  FUNCTION PROTOTYPES and type definitions ....
    These are used in breaking the code into function libraries.
    They work properly on a strictly ANSI compiler, so they
    apparently comply with the ANSI standard format.  */

struct coord
   {
     short sign;  /* carry sign explicitly since -0 not neg. */
     double hh;
     double mm;
     double ss;
   };

struct date_time
   {
	short y;
	short mo;
	short d;
	short h;
	short mn;
	float s;
   };

typedef struct 
{
  double jd_start;
  double jd_end;
  double ut_start;
  double ut_end;
  double lst_start;
  double lst_end;
  double jd_evening12;		/* jd of 12 degree twilight */
  double jd_evening18;		/* jd of 18 degree twilight */
  double jd_morning12;		/* jd of 12 degree twilight */
  double jd_morning18;		/* jd of 18 degree twilight */
  double jd_sunrise;
  double jd_sunset;
  double ut_sunset;
  double ut_evening12;		/* time of 12 degree twilight */
  double ut_evening18;		/* time of 18 degree twilight */
  double ut_midnight;
  double ut_morning12;
  double ut_morning18;
  double ut_sunrise;
  double ut_moonrise;
  double ut_moonset;
  double lst_sunset;
  double lst_evening12;
  double lst_evening18;
  double lst_midnight;
  double lst_morning12;
  double lst_morning18;
  double lst_sunrise;
  double lst_moonrise;
  double lst_moonset;
  double ra_moon;
  double dec_moon;
  double percent_moon;
} Night_Times ;  



void oprntf(char *fmt, ...);

void load_site(double *longit, double *lat, double *stdz, short *use_dst,
	       char *zone_name, char *zabr, double *elevsea, double *elev,
	       double *doublehoriz, char *site_name);

int get_sys_date(struct date_time *date, short use_dst, short enter_ut, 
		 short night_date, double stdz, double toffset);

void print_all(double jdin);
double date_to_jd(struct date_time date);

void set_zenith(struct date_time date, short use_dst, short enter_ut, 
		short night_date, double stdz, double lat,
		double longit, double epoch, double *ra, double *dec);

void print_tonight(struct date_time date, double lat, double longit,
		   double elevsea, double elev, double horiz,
		   char *site_name, double stdz, char *zone_name,
		   char zabr, short use_dst, double *jdb, double *jde,
		   short short_long, Night_Times *ntimes, int print_flag);


double altit(double dec, double ha, double lat, double *az);
 
double secant_z(double alt);


#endif
