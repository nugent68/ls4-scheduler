#include <stdio.h>
#include <string.h>
#include <math.h>

#define east_longitude (-156.25613)

#define observatory_latitude (20.7061*3.14159/180.0)
#define OBLIQUITY 0.4092797 /*23.0+(27.0/60.0))*(pi/180.0)*/
#define PIE 3.141592563
#define RAD_PER_DEG (PIE/180.0)
#define COS_OBL 0.9174077
#define SIN_OBL 0.3979486

extern double cos(),asin(),sin(),sqrt(),fabs();
double my_asin(double x);
double get_lamda(double ra, double dec, double lat);
double get_beta(double ra, double dec);


/************************************************/
void ecliptic_to_equator(lon,lat,ra,dec)
double lon,lat,*ra,*dec;
{
	double cos_dec,sin_dec,sin_ra;
	int reflect_flag;

	if(lon>90.0&&lon<270.0){
	    reflect_flag=1;
	}
	else {
	    reflect_flag=0;
	}


	lon=lon*RAD_PER_DEG;
	lat=lat*RAD_PER_DEG;


	sin_dec=COS_OBL*sin(lat) + SIN_OBL*sin(lon)*cos(lat);

	*dec=my_asin(sin_dec)/RAD_PER_DEG;

	cos_dec=sqrt(1-sin_dec*sin_dec);

	sin_ra=(COS_OBL*sin(lon)*cos(lat) - SIN_OBL*sin(lat))/cos_dec; 

	*ra=my_asin(sin_ra)/RAD_PER_DEG;

	if(reflect_flag==1){
		*ra=180.0-*ra;
	}

	while(*ra<0.0)*ra=*ra+360.0;
	while(*ra>360.0)*ra=*ra-360.0;

	return;
}
/*****************************************************************************/
void equator_to_ecliptic(ra,dec,lon,lat)
double ra,dec,*lon,*lat;
{
	ra=ra*RAD_PER_DEG;
	dec=dec*RAD_PER_DEG;

	*lat=get_beta(ra,dec);
	*lon=get_lamda(ra,dec,*lat);

	*lat=*lat/RAD_PER_DEG;
	*lon=*lon/RAD_PER_DEG;

	while(*lon<0.0)*lon=*lon+360.0;
	while(*lon>360.0)*lon=*lon-360.0;

	return;
}
/*****************************************************************************/

double get_beta(a,d)
double a,d;
{
	double b;

	b=my_asin(sin(d)*COS_OBL - cos(d)*sin(a)*SIN_OBL);

	return(b);
}

/*****************************************************************************/

double get_lamda(a,d,b)
double a,d,b;
{
	double l;


	l= ( (sin(d)*SIN_OBL) + (cos(d)*sin(a)*COS_OBL) )/cos(b);
	l = my_asin(l);

	if(cos(d)*cos(a)/cos(b)<0.0)l=PIE-l;
	
	return(l);
}

/*****************************************************************************/

double my_asin(x)
double x;
{

	if(fabs(x)>1.01){
		printf("argument of asin exceeds bounds: %f\n",x);
	}
	else if (x>1.0){
		x=1.0;
	}
	else if(x<-1.0){
		x=-1.0;
	}
	return(asin(x));
}
/*****************************************************************************/
