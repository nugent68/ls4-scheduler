/* get_airmass.c */

 

#include "scheduler.h"

int verbose=0;

/************************************************************/

main(int argc, char **argv)
{
        double ha,dec,ra,ut,lst;
        Site_Params site;
        Night_Times nt;
	int year,month,day;
        struct date_time date;

        if(argc!=3&&argc!=7){
           fprintf(stderr,"get_airmass ha (hour) dec (deg)\n");
           fprintf(stderr,"or get_airmass ra (hour) dec (deg) ut (hour)  yyyy mm dd\n");
           exit(-1);
        }

        /* initialize site parameters for DEFAULT observatory (ESO La Silla) */

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

        if(argc==3){
          sscanf(argv[1],"%lf",&ha);
          sscanf(argv[2],"%lf",&dec);
          fprintf(stdout,"%7.3f\n",get_airmass(ha,dec,&site));
        }
        else if (argc==7){
          sscanf(argv[1],"%lf",&ra);
          sscanf(argv[2],"%lf",&dec);
          sscanf(argv[3],"%lf",&ut);
          sscanf(argv[4],"%d",&(date.y));
          sscanf(argv[5],"%d",&(date.mo));
          sscanf(argv[6],"%d",&(date.d));
	  date.h=0;
	  date.mn=0;
          date.s=0;
          init_night(date,&nt,&site);
          lst=ut+nt.lst_sunrise-nt.ut_sunrise;
          ha = get_ha(ra, lst);
          fprintf(stdout,"%7.3f %7.3f\n",ha,get_airmass(ha,dec,&site));
        }
 
        exit(0);

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
 
int init_night(struct date_time date, Night_Times *nt,
                                     Site_Params *site)
{
 
        /* initialize night_time values */
        print_tonight(date,site->lat,site->longit,site->elevsea,site->elev,site->horiz,
                      site->site_name,site->stdz,site->zone_name,site->zabr,site->use_dst,
                      &(site->jdb),&(site->jde),2,nt,0);
 
 
 
          nt->ut_start=nt->ut_evening12+STARTUP_TIME;
          nt->ut_end=nt->ut_morning12-MIN_EXECUTION_TIME;
          nt->lst_start=nt->lst_evening12+STARTUP_TIME;
          nt->lst_end=nt->lst_morning12-MIN_EXECUTION_TIME;
          nt->jd_start=nt->jd_evening12+(STARTUP_TIME/24.0);
          nt->jd_end=nt->jd_morning12-(MIN_EXECUTION_TIME/24.0);
 
        if(nt->ut_start>24.0)nt->ut_start=nt->ut_start-24.0;
        if(nt->lst_start>24.0)nt->lst_start=nt->lst_start-24.0;
 
        if(nt->ut_end<0.0)nt->ut_end=nt->ut_end+24.0;
        if(nt->lst_end<0.0)nt->lst_end=nt->lst_end+24.0;
 
 
 
        return(0);
}
 
/************************************************************/
