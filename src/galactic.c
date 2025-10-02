/* galactic.c  Calculate galactic coords given RA and DEC
  Code abstracted from "sykcalc.c"  by John Thorstensen

  D. Rabinowitz
*/

#include <stdio.h>
#include <math.h>
#include <ctype.h>
#include <string.h>

#define TEST

/* a couple of the system-dependent magic numbers are defined here */

#define SYS_CLOCK_OK 1    /* 1 means ANSI-standard time libraries do work,
   2 means they don't.  This is used by compiler switches in file 5 and
   the main program.  */

#define LOG_FILES_OK 1  /* 1 means that log files are enabled.
			Any other value means they're not.  */

#define MAX_OBJECTS 500
#define MINSHORT -32767   /* min, max short integers and double precision */
#define MAXSHORT 32767
#define MAXDOUBLE 1.0e38
#define MINDOUBLE -1.0e38
#define BUFSIZE 150

/* some (not all) physical, mathematical, and astronomical constants
   used are defined here. */

#define  PI                3.14159265358979
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


extern void galact(),precrot(),xyz_cel();
extern double atan(),tan(),cos(),asin(),sin(),asin(),cot();
extern double atan_circ(),fabs();

/***********************************************/
#ifdef TEST
main()
{
	float ra,dec,glong,glat,glong_min,glat_min,dec_min;
	double epoch;

	epoch=2000.0;

	for(ra=0.0;ra<=24.00;ra=ra+1.0){

           dec_min=-90;
           glat_min=1000.0;
           for(dec=-90;dec<=90;dec=dec+1.0){
	      galact(ra,dec,epoch,&glong,&glat);
              if(fabs(glat)<glat_min){
                 glat_min=fabs(glat);
                 dec_min=dec;
              }
           }
	   galact(ra,dec_min,epoch,&glong,&glat);

	   printf("%f %f %f %f\n",ra,dec_min,glong,glat);

	}

	exit(0);
}
#endif
/***********************************************/

void galact(ra1,dec1,epoch,glong1,glat1)

	float ra1,dec1,*glong1,*glat1;
	double epoch;

{
	double ra,dec,glong,glat;

	/* Homebrew algorithm for 3-d Euler rotation into galactic.
	   Perfectly rigorous, and with reasonably accurate input
	   numbers derived from original IAU definition of galactic
	   pole (12 49, +27.4, 1950) and zero of long (at PA 123 deg
	   from pole.) */

	double  p11= -0.066988739415,
		p12= -0.872755765853,
		p13= -0.483538914631,
		p21=  0.492728466047,
		p22= -0.450346958025,
		p23=  0.744584633299,
		p31= -0.867600811168,
		p32= -0.188374601707,
		p33=  0.460199784759;  /* derived from Euler angles of
		theta   265.610844031 deg (rotates x axis to RA of galact center),
		phi     28.9167903483 deg (rotates x axis to point at galact cent),
		omega   58.2813466094 deg (rotates z axis to point at galact pole) */

	double r1950,d1950,
		x0,y0,z0,x1,y1,z1,
		check;

/*   EXCISED CODE .... creates matrix from Euler angles. Resurrect if
     necessary to create new Euler angles for better precision.
     Program evolved by running and calculating angles,then initializing
     them to the values they will always have, thus saving space and time.

	cosphi = cos(phi); and so on
	p11 = cosphi * costhet;
	p12 = cosphi * sinthet;
	p13 = -1. * sinphi;
	p21 = sinom*sinphi*costhet - sinthet*cosom;
	p22 = cosom*costhet + sinthet*sinphi*sinom;
	p23 = sinom*cosphi;
	p31 = sinom*sinthet + cosom*sinphi*costhet;
	p32 = cosom*sinphi*sinthet - costhet*sinom;
	p33 = cosom * cosphi;

	printf("%15.10f %15.10f %15.10f\n",p11,p12,p13);
	printf("%15.10f %15.10f %15.10f\n",p21,p22,p23);
	printf("%15.10f %15.10f %15.10f\n",p31,p32,p33);

	check = p11*(p22*p33-p32*p23) - p12*(p21*p33-p31*p23) +
		p13*(p21*p32-p22*p31);
	printf("Check: %lf\n",check);  check determinant .. ok

    END OF EXCISED CODE..... */

	ra=ra1;
	dec=dec1;

	/* precess to 1950 */
	precrot(ra,dec,epoch,1950.,&r1950,&d1950);
	r1950 = r1950 / HRS_IN_RADIAN;
	d1950 = d1950 / DEG_IN_RADIAN;

	/* form direction cosines */
	x0 = cos(r1950) * cos(d1950);
	y0 = sin(r1950) * cos(d1950);
	z0 = sin(d1950);

	/* rotate 'em */
	x1 = p11*x0 + p12*y0 + p13*z0;
	y1 = p21*x0 + p22*y0 + p23*z0;
	z1 = p31*x0 + p32*y0 + p33*z0;

	/* translate to spherical polars for Galactic coords. */
	glong = atan_circ(x1,y1)*DEG_IN_RADIAN;
	glat = asin(z1)*DEG_IN_RADIAN;

	*glong1=glong;
	*glat1=glat;
}

void precrot(rorig, dorig, orig_epoch, final_epoch, rf, df)

	double rorig, dorig, orig_epoch, final_epoch, *rf, *df;

/*  orig_epoch, rorig, dorig  years, decimal hours, decimal degr.
    final_epoch;
    *rf, *df final ra and dec */

   /* Takes a coordinate pair and precesses it using matrix procedures
      as outlined in Taff's Computational Spherical Astronomy book.
      This is the so-called 'rigorous' method which should give very
      accurate answers all over the sky over an interval of several
      centuries.  Naked eye accuracy holds to ancient times, too.
      Precession constants used are the new IAU1976 -- the 'J2000'
      system. */

{
   double ti, tf, zeta, z, theta;  /* all as per  Taff */
   double cosz, coszeta, costheta, sinz, sinzeta, sintheta;  /* ftns */
   double p11, p12, p13, p21, p22, p23, p31, p32, p33;
      /* elements of the rotation matrix */
   double radian_ra, radian_dec;
   double orig_x, orig_y, orig_z;
   double fin_x, fin_y, fin_z;   /* original and final unit ectors */

   ti = (orig_epoch - 2000.) / 100.;
   tf = (final_epoch - 2000. - 100. * ti) / 100.;

   zeta = (2306.2181 + 1.39656 * ti + 0.000139 * ti * ti) * tf +
    (0.30188 - 0.000344 * ti) * tf * tf + 0.017998 * tf * tf * tf;
   z = zeta + (0.79280 + 0.000410 * ti) * tf * tf + 0.000205 * tf * tf * tf;
   theta = (2004.3109 - 0.8533 * ti - 0.000217 * ti * ti) * tf
     - (0.42665 + 0.000217 * ti) * tf * tf - 0.041833 * tf * tf * tf;

   /* convert to radians */

   zeta = zeta / ARCSEC_IN_RADIAN;
   z = z / ARCSEC_IN_RADIAN;
   theta = theta / ARCSEC_IN_RADIAN;

   /* compute the necessary trig functions for speed and simplicity */

   cosz = cos(z);
   coszeta = cos(zeta);
   costheta = cos(theta);
   sinz = sin(z);
   sinzeta = sin(zeta);
   sintheta = sin(theta);

   /* compute the elements of the precession matrix */

   p11 = coszeta * cosz * costheta - sinzeta * sinz;
   p12 = -1. * sinzeta * cosz * costheta - coszeta * sinz;
   p13 = -1. * cosz * sintheta;

   p21 = coszeta * sinz * costheta + sinzeta * cosz;
   p22 = -1. * sinzeta * sinz * costheta + coszeta * cosz;
   p23 = -1. * sinz * sintheta;

   p31 = coszeta * sintheta;
   p32 = -1. * sinzeta * sintheta;
   p33 = costheta;

   /* transform original coordinates */

   radian_ra = rorig / HRS_IN_RADIAN;
   radian_dec = dorig / DEG_IN_RADIAN;

   orig_x = cos(radian_dec) * cos(radian_ra);
   orig_y = cos(radian_dec) *sin(radian_ra);
   orig_z = sin(radian_dec);
      /* (hard coded matrix multiplication ...) */
   fin_x = p11 * orig_x + p12 * orig_y + p13 * orig_z;
   fin_y = p21 * orig_x + p22 * orig_y + p23 * orig_z;
   fin_z = p31 * orig_x + p32 * orig_y + p33 * orig_z;

   /* convert back to spherical polar coords */

   xyz_cel(fin_x, fin_y, fin_z, rf, df);

}

void xyz_cel(x, y, z, r, d)

	double x, y, z, *r, *d;

     /* Cartesian coordinate triplet */

{
   /* converts a coordinate triplet back to a standard ra and dec */

   double mod;    /* modulus */
   double xy;     /* component in xy plane */
   short sign;    /* for determining quadrant */
   double radian_ra, radian_dec;

   /* this taken directly from pl1 routine - no acos or asin available there,
       as it is in c. Easier just to copy, though */

   mod = sqrt(x*x + y*y + z*z);
   x = x / mod;
   y = y / mod;
   z = z / mod;   /* normalize 'em explicitly first. */

   xy = sqrt(x*x + y*y);

   if(xy < 1.0e-10) {
      radian_ra = 0.;  /* too close to pole */
      radian_dec = PI / 2.;
      if(z < 0.) radian_dec = radian_dec * -1.;
   }
   else {
      if(fabs(z/xy) < 3.) radian_dec = atan(z / xy);
	 else if (z >= 0.) radian_dec = PI / 2. - atan(xy / z);
	 else radian_dec = -1. * PI / 2. - atan(xy / z);
      if(fabs(x) > 1.0e-10) {
	 if(fabs(y / x) < 3.) radian_ra = atan(y / x);
	 else if ((x * y ) >= 0.) radian_ra = PI / 2. - atan(x/y);
	 else radian_ra = -1. *  PI / 2. - atan(x / y);
      }
      else {
	 radian_ra = PI / 2.;
	 if((x * y)<= 0.) radian_ra = radian_ra * -1.;
      }
      if(x <0.) radian_ra = radian_ra + PI ;
      if(radian_ra < 0.) radian_ra = radian_ra + 2. * PI ;
   }

   *r = radian_ra * HRS_IN_RADIAN;
   *d = radian_dec * DEG_IN_RADIAN;

}

double atan_circ(x,y)

	double x,y;

{
	/* returns radian angle 0 to 2pi for coords x, y --
	   get that quadrant right !! */

	double theta;

	if(x == 0.) {
		if(y > 0.) theta = PI / 2.;
		else if(y < 0.) theta = 3.* PI / 2.;
		else theta = 0.;   /* x and y zero */
	}
	else theta = atan(y/x);
	if(x < 0.) theta = theta + PI;
	if(theta < 0.) theta = theta + 2.* PI;
	return(theta);
}

