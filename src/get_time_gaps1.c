/* get_time_gaps.c 

   read log files from survey_sim output (run_all1.csh) and
   determine number of obs, min_gap, max_gap, and mean_gap per
   field

*/

#include <stdio.h>

#define MAX_GRID_POINTS 10000
#define RA1 0.0
#define RA2 360.0
#define DEC1 -90.0
#define DEC2 90.0
#define RA_INCR 4.0
#define DEC_INCR 4.5



#define RA_STEP0 0.0333 /* hours = 0.5 deg. Spacing between fingers */
#define DEG_TO_RAD (3.14159/180.0)

#define MAX_GAP_COUNT_VALUE 100 /* keep track of number of gaps with 
                           intervals not exceeding MAX_COUNT_VALUE/5 */



typedef struct {
    unsigned int gap_word;
    int n_obs;q
    double jd_first;
    double jd_last;
    double jd;
    int n_tno_gaps;
    int n_sne_gaps;
} Field;

int verbose=0;

int read_fields(char *log_file, Field *f, int *count);
Field *field_pointer(Field *f, double ra, double dec);
int print_field_counts(char *file, Field *field);

/*************************************************************/

main(int argc, char **argv)
{
    Field *field;
    int n_grid_points;
    int i,n_fields,n;
    int gap_count[MAX_GAP_COUNT_VALUE+1];

    if(argc!=3){
       fprintf(stderr,"syntax:get_time_gaps log_file output\n");
       exit(-1);
    }

    n_grid_points=2*(1+(RA2-RA1)/RA_INCR)*(1+(DEC2-DEC1)/DEC_INCR);
    if(n_grid_points>MAX_GRID_POINTS){
      fprintf(stderr,"too many grid points\n");
      exit(-1);
    }
          
                                                                                
    field=(Field *)malloc(n_grid_points*sizeof(Field));
    if(field==NULL){
        fprintf(stderr,"can't allocate field_grid memory\n");
        exit(-1);
    }
    if(verbose){
      fprintf(stderr,"%d grid points allocated\n",n_grid_points);
    }

    for(i=0;i<n_grid_points;i++){
           field[i].n_obs=0;
           field[i].gap_word=0;
           field[i].jd=0.0;
           field[i].n_tno_gaps=0;
           field[i].n_sne_gaps=0;
	   field[i].jd_first=0.0;
	   field[i].jd_last=0.0;
    }


    n_fields=read_fields(argv[1],field,gap_count);
    if(n_fields<=0){
        fprintf(stderr,"error reading fields\n");
        exit(-1);
    }
    else{
        printf("%d fields read\n",n_fields);
    }

    n=0;
    for(i=0;i<=MAX_GAP_COUNT_VALUE;i++){
      if(i>0)n=n+gap_count[i];
      printf("%03d %d %d\n",i*5,gap_count[i],n);
    }

    print_field_counts(argv[2],field);

    free(field);

    exit(0);
}
/*************************************************************/

int print_field_counts(char *file, Field *field)
{
    FILE *output;
    Field *f;
    double ra,dec;

    output=fopen(file,"w");
    if(output== NULL){
       fprintf(stderr,"can't open file %s for writing\n",file);
       return(-1);
    }

    for(ra=0;ra<=360;ra=ra+RA_INCR){
       for(dec=-90.0; dec<=90.0;dec=dec+DEC_INCR){
          f=field_pointer(field,ra,dec);
          if(f->n_obs>0){
             fprintf(output,"%10.6f %10.6f %03d %10.6f %d %d %d %d %d %d %d %d\n",
                ra/15.0,dec,f->n_obs,f->jd_last-f->jd_first,
               (f->gap_word&2)>>1,
               (f->gap_word&4)>>2,
               (f->gap_word&8)>>3,
               (f->gap_word&16)>>4,
               (f->gap_word&32)>>5,
               (f->gap_word&64)>>6,
               (f->gap_word&128)>>7,
               (f->gap_word&256)>>8);

          }
       }
    }

    fclose(output);

    return(0);
}

/*************************************************************/

int read_fields(char *file, Field *field, int *gap_count)
{
    FILE *input;
    int i;
    char string[1024];
    Field *f;
    double ra,dec,jd,dt;
    char s[256],shutter_flag[2];
    int n,p;

    input=fopen(file,"r");
    if(input==NULL){
         fprintf(stderr,"can't open file %s\n",file);
         return(-1);
    }

    for(i=0;i<=MAX_GAP_COUNT_VALUE;i++){gap_count[i]=0;}

 
    n=0;
    while(fgets(string,1024,input)!=NULL){
        if(strstr(string,"y")!=NULL){
            sscanf(string,"%lf %lf %s %s %s %s %lf",
                  &ra,&dec,shutter_flag,s,s,s,&jd);
            if(ra>=24.0)ra=ra-24.0;
               
            ra=ra*15.0;

            f=field_pointer(field,ra,dec);
            

            if(f->n_obs==0){
               n++;
               f->gap_word=0;
               f->jd_first=jd;
               f->jd_last=jd;
            }
            else{
               /*if(jd-f->jd_first<100)*/f->jd_last=jd;
               dt=jd-f->jd;
               if(dt<0.5){
                   p=0;
               }
               else if(dt<3){
                   p=1;
               }
               else{
                  p=dt/5.0;
                  p=p+2;
               }
               if(p>sizeof(int))p=sizeof(int)-1;
               f->gap_word=f->gap_word|(2<<p);

               p=0.5+(dt/5);
               if(p<MAX_GAP_COUNT_VALUE){
                 gap_count[p]=gap_count[p]+1;
               }
            }
            
            f->n_obs=f->n_obs+1;
            f->jd=jd;
        }
    }

    fclose(input);
    return(n);
}
  

/*************************************************************/

Field *field_pointer(Field *f, double ra, double dec)
{
    int i,j,n_ra;
    
    n_ra=360.0/RA_INCR;
    i=ra/RA_INCR;
    j=(dec+90.0)/DEC_INCR;
/*fprintf(stderr,"%10.6f %10.6f %d %d\n",ra,dec,i,j);*/
    return(f+(j*n_ra)+i);
}

/*************************************************************/

