/* scheduler_corrections.c 

   functionto return ra and dec corrections for telescope
   pointing on repeat exposure of field. Depends on
   initial hour angle, ha0, of the field and the
   current hour angle, ha.

   corrections are to be subtracted from the ra and
   dec of the field before positioning the telescope.

*/

#include "scheduler.h"

#define DEG_TO_RADIANS (3.14159/180.0)

/* parameters determined from fields taken
   2007 Mar 12 and 13 UT */

#define RA_HA_CHANGE 0.0 /* hour */
#define RA_SLOPE1 -0.002375  /* deg/hour */
#define RA_SLOPE2 -0.021  /* deg/hour */

#define DEC_HA_CHANGE1 0.0 /* hour */
#define DEC_HA_CHANGE2 2.0 /* hour */
#define DEC_SLOPE1 -0.0008125  /* deg/hour */
#define DEC_SLOPE2 -0.00325  /* deg/hour */
#define DEC_SLOPE3 -0.00675  /* deg/hour */

/* corrections on equator to ra and dec rates east
   and west of the meridian (ha < 0 and > 0, respectively.
*/

#define WEST_RA_RATE_CORRECTION 0.0 /* "/hour */
#define EAST_RA_RATE_CORRECTION 0.0 /* "/hour */
#define WEST_DEC_RATE_CORRECTION 0.0 /* "/hour */
#define EAST_DEC_RATE_CORRECTION 0.0 /* "/hour */

/*
double get_ra_correction(double ha0, double ha);
double get_dec_correction(double ha0, double ha);
double get_ra_rate(double ha, double dec);
double get_dec_rate(double ha, double dec);
*/

extern int verbose;
/****************************************************************/

/* ha is in hours, dec in degrees. Return offset to RA tracking rate
   (to be subtracted from siderial rate) to correct tracking error. Units are
    arcsec/hour */

double get_ra_rate(double ha, double dec)
{
  double ra_rate;

  if (ha > 0.0 ) {
      if(dec<-30.0){
        ra_rate = 0.010;
      }
      else if (dec < 30.0 ){
        ra_rate= 0.017;
      }
      else if (dec < 50.0){
        ra_rate= 0.013;
      }
      else{
        ra_rate= 0.010;
      }
  }
  else{
      if(dec<-30.0){
        ra_rate = 0.003;
      }
      else if (dec < 30.0 ){
        ra_rate= 0.005;
      }
      else if (dec < 50.0){
        ra_rate= 0.003;
      }
      else{
        ra_rate= 0.003;
      }
  }
  ra_rate=-ra_rate; /* negative values make telescope move faster than sidereal, which
                       is what we need to correct the tracking error */
  ra_rate=ra_rate*3600.0;
  ra_rate = ra_rate/cos(dec*DEG_TO_RADIANS);

  if(verbose){
     fprintf(stderr,"get_ra_rate: ha = %7.3f dec = %7.3f\n ra_rate = %7.3f\n",ha,dec,ra_rate);
  }

  return(ra_rate);

}

/****************************************************************/

/* ha is in hours, dec in degrees. Return offset to Dec tracking
   Units are arcsec/hour */

double get_dec_rate(double ha, double dec)
{

  double dec_rate;

  dec_rate = 0.004 + (-0.012*(ha+2.0)/6.0);
  dec_rate = -dec_rate*3600.0;

  if(verbose){
     fprintf(stderr,"get_dec_rate: ha = %7.3f dec = %7.3f\n dec_rate = %7.3f\n",ha,dec,dec_rate);
  }

  return(dec_rate);

}

/*****************************************************************/

/* ha0 and ha are in hours. Returns correction in deg */

double get_dec_correction(double ha0, double ha)
{
    double correction;

    if(ha0<DEC_HA_CHANGE1){
       if(ha<DEC_HA_CHANGE1){
           correction=DEC_SLOPE1*(ha-ha0);
       }
       else if (ha<DEC_HA_CHANGE2){
           correction=DEC_SLOPE1*(DEC_HA_CHANGE1-ha0)+
                       DEC_SLOPE2*(ha-DEC_HA_CHANGE1);
       }
       else {
           correction=DEC_SLOPE1*(DEC_HA_CHANGE1-ha0)+
                       DEC_SLOPE2*(DEC_HA_CHANGE2-DEC_HA_CHANGE1)+
                       DEC_SLOPE3*(ha-DEC_HA_CHANGE2);
       }
    }
    else if(ha0<DEC_HA_CHANGE2){
       if(ha<DEC_HA_CHANGE1){
           correction=DEC_SLOPE1*(ha-DEC_HA_CHANGE1)+
                      DEC_SLOPE2*(DEC_HA_CHANGE1-ha0);
       }
       else if (ha<DEC_HA_CHANGE2){
           correction=DEC_SLOPE2*(ha-ha0);
       }
       else {
           correction=DEC_SLOPE2*(DEC_HA_CHANGE2-ha0)+
                      DEC_SLOPE3*(ha-DEC_HA_CHANGE2);
       }
    }
    else{
      if(ha<DEC_HA_CHANGE1){
           correction=DEC_SLOPE1*(ha-DEC_HA_CHANGE1)+
                      DEC_SLOPE2*(DEC_HA_CHANGE1-DEC_HA_CHANGE2)+
		      DEC_SLOPE3*(DEC_HA_CHANGE2-ha0);
       }
       else if (ha<DEC_HA_CHANGE2){
           correction=DEC_SLOPE2*(ha-DEC_HA_CHANGE2)+
		      DEC_SLOPE3*(DEC_HA_CHANGE2-ha0);
       }
       else {
           correction=DEC_SLOPE3*(ha-ha0);
       }
    }

    return(correction);
}

/****************************************************************/

/* ha0 and ha are in hours. Returns correction in deg */

double get_ra_correction(double ha0, double ha)
{
    double correction;

    if(ha0<RA_HA_CHANGE){
       if(ha<RA_HA_CHANGE){
           correction=RA_SLOPE1*(ha-ha0);
       }
       else{
           correction=RA_SLOPE1*(RA_HA_CHANGE-ha0)+
                       RA_SLOPE2*(ha-RA_HA_CHANGE);
       }
    }
    else{
       if(ha>RA_HA_CHANGE){
           correction=RA_SLOPE2*(ha-ha0);
       }
       else{
           correction=RA_SLOPE2*(RA_HA_CHANGE-ha0)+
                       RA_SLOPE1*(ha-RA_HA_CHANGE);
       }
    }

    return(correction);
}
    
/****************************************************************/
