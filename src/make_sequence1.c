/*
make_sequence.c

Make sequence of fields to be surveyed in one night given
specified ra range and dec range relative to the ecliptic.
IF any of these fields overlaps the fields of the previos range
(file specified) by more than max_overlap_percent, then skip
this field in the sequence. This only becomes important at
high declinations where the RA circles converge.

syntax: make_sequence.csh ra1 ra2 dec1 dec2 incr
*/

#include <stdio.h>

#define FIELD_WIDTH 0.26667 /* RA width of dithered pair of field in hours */
#define MAX_FIELDS 1000 /* maximum number of fields per sequence */
#define RA_STEP0 0.0333 /* hours = 0.5 deg. Spacing between fingers */
#define DEG_TO_RAD (3.141592654/180.0)

typedef struct {
    double ra; /* hours */
    double dec; /* deg */
    double field_width ;/* RA width of field in hours */
    int flag;
} Field;

int fill_sequence(Field *field, double ra1, double ra2, double ra_incr, 
        double dec1, double dec2, double dec_incr);

int print_sequence(Field *field, int n_fields,double interval);

int read_sequence(char *file, Field *field);

int flag_overlap(Field *sequence, int n_fields, 
               Field * prev_sequence, int n_prev_fields, 
               double max_overlap);

double clock_difference(double h1,double h2);

/********************************************************/
main(int argc, char **argv)
{
   double ra1,ra2,ra_incr,dec1,dec2,dec_incr,interval;
   int n_fields,n_prev_fields,verbose,n_rejected;
   Field *sequence,*prev_sequence;
   char prev_sequence_file[1024];
   double max_overlap;

    if ( argc != 11 ) {
       fprintf(stderr,
          "syntax:make_sequence.csh ra1 ra2 ra_incr dec1 dec2 dec_incr interval prev_sequence max_overlap_percent verbose_flag\n");
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
    strcpy(prev_sequence_file,argv[8]);
    sscanf(argv[9],"%lf",&max_overlap);
    sscanf(argv[10],"%d",&verbose);

    ra1=ra1/15.0;
    ra2=ra2/15.0;
    ra_incr=ra_incr/15.0;

    if(ra2<ra1)ra2=ra2+24.0;


    sequence=(Field *)malloc(MAX_FIELDS*sizeof(Field));
    if(sequence==NULL){
        fprintf(stderr,"can't allocate sequence memory\n");
        exit(-1);
    }

    prev_sequence=(Field *)malloc(MAX_FIELDS*sizeof(Field));
    if(prev_sequence==NULL){
        fprintf(stderr,"can't allocate prev_sequence memory\n");
        exit(-1);
    }

    n_prev_fields=read_sequence(prev_sequence_file,prev_sequence);
    if(n_prev_fields<=0){
       fprintf(stderr,"error reading previous sequence\n");
       exit(-1);
    }

    n_fields=fill_sequence(sequence,ra1,ra2,ra_incr,dec1,dec2,dec_incr);
 
    n_rejected=flag_overlap(sequence,n_fields,prev_sequence,
                                n_prev_fields, max_overlap);
    fprintf(stderr,"# %d fields rejected for overlap\n",n_rejected);

    if(n_fields<0){
      fprintf(stderr,"error filling sequence\n");
    }
    else{
       print_sequence(sequence,n_fields,interval);
    }

    free(prev_sequence);
    free(sequence);

    exit(0);
}

/*****************************************************/

int read_sequence(char *file, Field *field)
{
    FILE *input;
    int n;
    char string[1024];
    Field *f;

    input=fopen(file,"r");
    if(input==NULL){
        fprintf(stderr,"can't open file %s for reading\n",file);
        return(-1);
    }

    n=0;

    while(fgets(string,1024,input)!=NULL){
      if(strstr(string,"n")==NULL){ /* skip darks */
         f=field+n;
         sscanf(string,"%lf %lf",&(f->ra),&(f->dec));
         f->field_width=(FIELD_WIDTH/cos(DEG_TO_RAD*f->dec));

         n++;
         if(n>=MAX_FIELDS){
            fprintf(stderr,"too many fields in file %s\n",file);
            fclose(input);
            return(-1);
         }
      }
    }

    fclose(input);
    return(n);
}

/*****************************************************/

int fill_sequence(Field *field, double ra1, double ra2, double ra_incr, 
        double dec1, double dec2, double dec_incr)
{
    int i,n;
    double ra,ra3,ra4,ra_width,dec,lon,lat,deca,decb;
    double deca_priority, decb_priority, dec_low, dec_high;


    i=0;
    for(ra=ra1;ra<=ra2;ra=ra+ra_incr){

      ra3=ra;
      if(ra3>24)ra3=ra3-24;
      if(ra3<0)ra3=ra3+24;

      /* find ecliptic latitude at current ra, 0 dec */
      equator_to_ecliptic(ra3*15,0.0,&lon,&lat);

      deca=fabs(dec1-lat);
      n=deca/dec_incr;
      deca=dec_incr*n;
      if(dec1-lat<0)deca=-deca;

      decb=fabs(dec2-lat);
      n=decb/dec_incr;
      decb=dec_incr*n;
      if(dec2-lat<0)decb=-decb;

      deca_priority = -lat - 4*dec_incr;
      decb_priority = -lat + 4*dec_incr;

      /* first time through loop, priority fields only */

      for(dec=deca;dec<=decb;dec=dec+dec_incr){

        if( dec >= deca_priority && dec <= decb_priority ){

           /* first field of pair */
           ra_width=FIELD_WIDTH/cos(dec*DEG_TO_RAD);
           field[i].flag=1;
           field[i].ra=ra3;
           field[i].dec=dec;
	   field[i].field_width=ra_width;

           /* second field of pair */
           i++;
           field[i].flag=1;
           ra4=ra3+RA_STEP0/cos(dec*DEG_TO_RAD);
           field[i].ra=ra4;
           if(field[i].ra>24)field[i].ra=field[i].ra-24;
           field[i].dec=dec;
	   field[i].field_width=ra_width;
 
           i++;
           if(i>=MAX_FIELDS){
              fprintf(stderr,"grid limit exceeded\n");
              return(-1);
           }
         }
       }


      /* second time through, low priority fields */

      dec_low=deca;
      while(dec_low<=deca_priority-dec_incr)dec_low=dec_low+dec_incr;

      dec_high=decb;
      while(dec_high>=decb_priority+dec_incr)dec_high=dec_high-dec_incr;

      while(dec_low>=deca || dec_high <= decb){

        if( dec_high <= decb){

           dec=dec_high;

           /* first field of pair */
           ra_width=FIELD_WIDTH/cos(dec*DEG_TO_RAD);
           field[i].flag=1;
           field[i].ra=ra3;
           field[i].dec=dec;
	   field[i].field_width=ra_width;

           /* second field of pair */
           i++;
           field[i].flag=1;
           ra4=ra3+RA_STEP0/cos(dec*DEG_TO_RAD);
           field[i].ra=ra4;
           if(field[i].ra>24)field[i].ra=field[i].ra-24;
           field[i].dec=dec;
	   field[i].field_width=ra_width;
 
           i++;
           if(i>=MAX_FIELDS){
              fprintf(stderr,"grid limit exceeded\n");
              return(-1);
           }

           dec_high=dec_high+dec_incr;
        }

        if( dec_low >= deca){

           dec=dec_low;

           /* first field of pair */
           ra_width=FIELD_WIDTH/cos(dec*DEG_TO_RAD);
           field[i].flag=1;
           field[i].ra=ra3;
           field[i].dec=dec;
	   field[i].field_width=ra_width;

           /* second field of pair */
           i++;
           field[i].flag=1;
           ra4=ra3+RA_STEP0/cos(dec*DEG_TO_RAD);
           field[i].ra=ra4;
           if(field[i].ra>24)field[i].ra=field[i].ra-24;
           field[i].dec=dec;
	   field[i].field_width=ra_width;
 
           i++;
           if(i>=MAX_FIELDS){
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

int flag_overlap(Field *field, int n_fields, 
               Field *prev_field, int n_prev_fields, 
               double max_overlap)
{

    int i,j;
    int n_rejects;
   
    n_rejects=0;
    for(i=0;i<n_fields-1;i=i+2){
        for(j=0;j<=n_prev_fields-1;j=j+2){
          if(field[i].dec==prev_field[j].dec&&
               check_overlap(field+i,prev_field+j,max_overlap)){
             field[i].flag=0;
             field[i+1].flag=0;
             n_rejects++;
          }
        }
    }

    return(n_rejects);
}

/*****************************************************/

int check_overlap(Field *f1, Field *f2, double max_overlap)
{
     double dra,overlap;

     dra=fabs(clock_difference(f1->ra,f2->ra));
     if(dra<f1->field_width){
        overlap=(f1->field_width-dra)/f1->field_width;
        if(overlap>max_overlap)return(1);
     }

     return(0);
}

/*****************************************************/

int print_sequence(Field *field, int n_points, double interval)
{
    int i;

    for(i=0;i<n_points;i++){
      if(field[i].flag==1){
          fprintf(stdout,"%10.6f %10.5f Y 60.0 %7.3f 3 %d\n",
	    field[i].ra,field[i].dec,interval,i);
      }
      else{
          fprintf(stdout,"# %10.6f %10.5f Y 60.0 %7.3f 3 %d overlap\n",
	    field[i].ra,field[i].dec,interval,i);
      }
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
/*****************************************************/

