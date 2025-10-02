/*
make_sequence.c

make grid of observations on the  sky

syntax: make_sequence.csh ra1 ra2 dec1 dec2 incr
*/

#include <stdio.h>

#define MAX_GRID_POINTS 10000
#define RA_STEP0 0.0333 /* hours = 0.5 deg. Spacing between fingers */
#define DEG_TO_RAD (3.14159/180.0)

typedef struct {
    double ra;
    double dec;
} Field;

int fill_grid(Field *grid, double ra1, double ra2, double ra_incr, 
        double dec1, double dec2, double dec_incr);
int print_grid(Field *grid, int n_grid_points,double interval);

/********************************************************/
main(int argc, char **argv)
{
   double ra1,ra2,ra_incr,dec1,dec2,dec_incr,interval;
   int n_grid_points,verbose;
   Field *field_grid;

    if ( argc != 9 ) {
       fprintf(stderr,
          "syntax:make_sequence.csh ra1 ra2 ra_incr dec1 dec2 dec_incr interval verbose_flag\n");
       fprintf(stderr,
          "where ra1, ra2, ra_incr are in hours, dec1, dec2, dec_incr are in deg\n");
       exit(-1);
    }
 
    sscanf(argv[1],"%lf",&ra1);
    sscanf(argv[2],"%lf",&ra2);
    sscanf(argv[3],"%lf",&ra_incr);
    sscanf(argv[4],"%lf",&dec1);
    sscanf(argv[5],"%lf",&dec2);
    sscanf(argv[6],"%lf",&dec_incr);
    sscanf(argv[7],"%lf",&interval);
    sscanf(argv[8],"%d",&verbose);

    ra1=ra1/15.0;
    ra2=ra2/15.0;
    ra_incr=ra_incr/15.0;

    if(ra2<ra1)ra2=ra2+24.0;

    /* grid spacing times 2, since there will be two exposures stepped
       in RA by RA_STEP0 per gridpoint */

    n_grid_points=2*(1+(ra2-ra1)/ra_incr)*(1+(dec2-dec1)/dec_incr);
    if(n_grid_points>MAX_GRID_POINTS){
      fprintf(stderr,"too many grid points\n");
      exit(-1);
    }

    field_grid=(Field *)malloc(n_grid_points*sizeof(Field));
    if(field_grid==NULL){
        fprintf(stderr,"can't allocate field_grid memory\n");
        exit(-1);
    }
    if(verbose){
      fprintf(stderr,"%d grid points allocated\n",n_grid_points);
    }


    n_grid_points=fill_grid(field_grid,ra1,ra2,ra_incr,dec1,dec2,dec_incr);
    if(n_grid_points<0){
      fprintf(stderr,"error filling grid\n");
    }
    else{
       print_grid(field_grid,n_grid_points,interval);
    }

    free(field_grid);
    exit(0);
}

/*****************************************************/

int fill_grid(Field *grid, double ra1, double ra2, double ra_incr, 
        double dec1, double dec2, double dec_incr)
{
    int i,n,m;
    double ra,ra3, dec,lon,lat,deca,decb,dec_low,dec_high;
    double deca_priority, decb_priority;


    i=0;
    for(ra=ra1;ra<=ra2;ra=ra+ra_incr){

      ra3=ra;
      if(ra3>24)ra3=ra3-24;
      if(ra3<0)ra3=ra3+24;

      /* find ecliptic latitude at current ra, 0 dec. This value
         of latitude gives the separation between the equator
         and the ecliptic along the declination direction  */
      equator_to_ecliptic(ra3*15,0.0,&lon,&lat);


      /* shift dec limits by -lat so that dec range is
         with respect to the declination of the ecliptic at
         the current ra */

      deca=fabs(dec1-lat);
      n=deca/dec_incr;
      deca=dec_incr*n;
      if(dec1-lat<0)deca=-deca;

      decb=fabs(dec2-lat);
      n=decb/dec_incr;
      decb=dec_incr*n;
      if(dec2-lat<0)decb=-decb;

      /* Get dec limits of priority range = -lat +/- 4*dec_incr */

      deca_priority = -lat - 4.0*dec_incr;
      decb_priority = -lat + 4.0*dec_incr;

      /* first time through this loop, only allow fields with 
         ecliptic latitude within priority range of the ecliptic
         (+/- 4.5*dec_incr). Second time through, add points
         outside of the priority range */

      for(dec=deca;dec<=decb;dec=dec+dec_incr){

       if( dec >= deca_priority && dec <= decb_priority ){

        grid[i].ra=ra3;
        grid[i].dec=dec;
        i++;
        grid[i].ra=ra3+RA_STEP0/cos(dec*DEG_TO_RAD);
        if(grid[i].ra>24)grid[i].ra=grid[i].ra-24;
        grid[i].dec=dec;
        i++;
        if(i>=MAX_GRID_POINTS-2){
           fprintf(stderr,"grid limit exceeded\n");
           return(-1);
        }
       }
      }

      /* intialize dec_low and dec_high to dec values just below
         and above the priority range */
      dec_low = deca;
      while(dec_low<=deca_priority-dec_incr)dec_low=dec_low+dec_incr;

      dec_high = decb;
      while(dec_high>=decb_priority+dec_incr)dec_high=dec_high-dec_incr;

      /* now keep adding fields alternately at dec_high and dec_low,
         each time decrementing dec_low and incrementing dec_high,
         until they exceeds the limits deca and decb, repectively */

      while (dec_low >= deca || dec_high <= decb){

       if( dec_high <= decb ){
        dec=dec_high;
        grid[i].ra=ra3;
        grid[i].dec=dec;
        i++;
        grid[i].ra=ra3+RA_STEP0/cos(dec*DEG_TO_RAD);
        if(grid[i].ra>24)grid[i].ra=grid[i].ra-24;
        grid[i].dec=dec;
        i++;
        if(i>=MAX_GRID_POINTS-2){
           fprintf(stderr,"grid limit exceeded\n");
           return(-1);
        }
        dec_high=dec_high+dec_incr;
       }

       if( dec_low >= deca ) {
        dec=dec_low;
        grid[i].ra=ra3;
        grid[i].dec=dec;
        i++;
        grid[i].ra=ra3+RA_STEP0/cos(dec*DEG_TO_RAD);
        if(grid[i].ra>24)grid[i].ra=grid[i].ra-24;
        grid[i].dec=dec;
        i++;
        if(i>=MAX_GRID_POINTS-2){
           fprintf(stderr,"grid limit exceeded\n");
           return(-1);
        }
        dec_low=dec_low-dec_incr;
       }

      }


    }

    return(i);
}

/*****************************************************/

int print_grid(Field *grid, int n_points, double interval)
{
    int i;

    for(i=0;i<n_points;i++){
      fprintf(stdout,"%10.6f %10.5f Y 60.0 %7.3f 3 %d\n",
	grid[i].ra,grid[i].dec,interval,i);
    }

    return(0);
}

  
/*****************************************************/

