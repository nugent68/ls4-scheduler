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

#define MAX_GAP_COUNT 100 /* keep track of number of gaps with 
                           intervals not exceeding MAX_COUNT_VALUE */



typedef struct {
    double ra;
    double dec;
    int n_obs;
    double jd_first;
    double jd_last;
    double jd;
    int gap_count[MAX_GAP_COUNT+1];
    int n_sne_gaps;
    int n_tno_gaps;
    double fom; /* figure of merit from lookup table */
} Field;

int verbose=0;
double sne_gap_min; /* minimum gap (days) for sne detection */
double sne_gap_max; /* maximum gap (days) for sne detection */
double fom_table[MAX_GAP_COUNT+1];
int max_fom_gap;

int init_fom_lookup_table(double *fom_table);
double get_fom(double gap);
int read_fields(char *log_file, Field *f, int *count);
Field *field_pointer(Field *f, double ra, double dec);
int print_field_counts(char *file, Field *field, int n_grid_points);

/*************************************************************/

main(int argc, char **argv)
{
    Field *field;
    int n_grid_points;
    int i,j,n_fields;
    int gap_count[MAX_GAP_COUNT+1];

    if(argc!=5){
       fprintf(stderr,"syntax:get_time_gaps log_file output sne_gap_min sne_gap_max\n");
       exit(-1);
    }

    sscanf(argv[3],"%lf",&sne_gap_min);
    sscanf(argv[4],"%lf",&sne_gap_max);

    max_fom_gap=init_fom_lookup_table(fom_table);

    /*n_grid_points=2*(1+(RA2-RA1)/RA_INCR)*(1+(DEC2-DEC1)/DEC_INCR);*/
    n_grid_points=2*(1+(360.0)/RA_INCR)*(1+(180.0)/DEC_INCR);
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
           field[i].jd=0.0;
           field[i].n_tno_gaps=0;
           field[i].n_sne_gaps=0;
	   field[i].jd_first=0.0;
	   field[i].jd_last=0.0;
	   field[i].n_tno_gaps=0;
	   field[i].n_sne_gaps=0;
	   field[i].fom=0.0;
           for(j=0;j<MAX_GAP_COUNT;j++){
               field[i].gap_count[j]=0;
           }
    }


    for(i=0;i<=MAX_GAP_COUNT;i++)gap_count[i]=0;

    n_fields=read_fields(argv[1],field,gap_count);
    if(n_fields<=0){
        fprintf(stderr,"error reading fields\n");
        exit(-1);
    }
    else{
        fprintf(stderr,"# %03d fields read\n",n_fields);
    }

    for(i=0;i<=MAX_GAP_COUNT;i++){
      printf("%03d %d\n",i,gap_count[i]);
    }

    print_field_counts(argv[2],field, n_grid_points);

    free(field);

    exit(0);
}
/*************************************************************/

int print_field_counts(char *file, Field *field, int n_grid_points)
{
    FILE *output;
    Field *f;
    double ra,dec,total_fom;
    int i,n_tno_gaps,n_sne_gaps,n_tno_fields,n_sne_fields;

    output=fopen(file,"w");
    if(output== NULL){
       fprintf(stderr,"can't open file %s for writing\n",file);
       return(-1);
    }

    n_tno_gaps=0;
    n_sne_gaps=0;
    n_tno_fields=0;
    n_sne_fields=0;
    total_fom=0.0;
    for(i=0;i<=n_grid_points;i++){
          f=field+i;
          if(f->n_obs>0){
             fprintf(output,
                "%10.6f %10.6f %03d %7.3f %03d %03d %7.3f\n",
                f->ra,f->dec,f->n_obs,f->jd_last-f->jd_first,
                f->n_tno_gaps,f->n_sne_gaps,f->fom);
             n_tno_gaps=n_tno_gaps+f->n_tno_gaps;
             n_sne_gaps=n_sne_gaps+f->n_sne_gaps;
             total_fom=total_fom+f->fom;
             if(f->n_tno_gaps>0)n_tno_fields++;
             if(f->n_sne_gaps>0)n_sne_fields++;
 
          }
    }

    fclose(output);

    fprintf(stderr,"# %03d sne gaps\n",n_sne_gaps);
    fprintf(stderr,"# %03d tno gaps\n",n_tno_gaps);
    fprintf(stderr,"# %03d sne fields\n",n_sne_fields);
    fprintf(stderr,"# %03d tno fields\n",n_tno_fields);
    fprintf(stderr,"# %7.3f total fom\n",total_fom);

 
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

    for(i=0;i<=MAX_GAP_COUNT;i++){gap_count[i]=0;}

 
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
               f->jd_first=jd;
 	       f->ra=ra;
               f->dec=dec;
	       f->n_tno_gaps=0;
               f->n_sne_gaps=0;
	       f->fom=0.0;
               f->fom=f->fom+get_fom(dt);
            }


            else{
 
               dt=jd-f->jd;

               if(dt>0.5&&dt<3.0){
 	           f->n_tno_gaps=f->n_tno_gaps+1;
               }
               else if (dt>=sne_gap_min&&dt<=sne_gap_max){
 	           f->n_sne_gaps=f->n_sne_gaps+1;
               }
 
               p=0.5+dt;
               if(p<MAX_GAP_COUNT){
                 gap_count[p]=gap_count[p]+1;
                 f->gap_count[p]=f->gap_count[p]+1;
               }

               f->fom=f->fom+get_fom(dt);
            }

            f->n_obs=f->n_obs+1;
            f->jd=jd;
	    f->jd_last=jd;

            printf("field %03d  %010.6f %010.6f %07.3f %03d %03d %03d\n",
                   (int)(f-field),f->ra,f->dec,f->jd-f->jd_first,
                   f->n_obs,f->n_tno_gaps,f->n_sne_gaps);
           
        }
    }

    fclose(input);
    return(n);
}
  

/*************************************************************/

Field *field_pointer(Field *f, double ra, double dec)
{
    int i,i1,j,n_ra;
    double ra1;

    ra=ra+0.01;
    
    n_ra=360.0/RA_INCR;
    n_ra=n_ra*2;

    i=ra/RA_INCR;
    i1=i*2;


    if((i*RA_INCR)+0.03<ra)i1++;

 

    j=(dec+90.0)/DEC_INCR;

    return(f+(j*n_ra)+i1);
}

/*************************************************************/

double get_fom (double gap)
{
      int i;

      if(gap==0.0||gap>=15.0){
         return (1.0);
      }
      else{
         return(gap/15.0);
      }
#if 0

      i=gap;
      if(i>=0&&i<max_fom_gap){
        return(fom_table[i]);
      }
      else{
        return(0.0);
      }
#endif
}
      
/*************************************************************/

int init_fom_lookup_table(double *table)
{
      int i;

      i=0;

      table[i++]= 0.0;
      table[i++]= 0.161616;
      table[i++]= 0.280808;
      table[i++]= 0.399038;
      table[i++]= 0.533981;
      table[i++]= 0.626515;
      table[i++]= 0.693754;
      table[i++]= 0.758876;
      table[i++]= 0.751987;
      table[i++]= 0.738517;
      table[i++]= 0.700908;
      table[i++]= 0.660781;
      table[i++]= 0.626344;
      table[i++]= 0.590692;
      table[i++]= 0.560145;
      table[i++]= 0.529299;
      table[i++]= 0.502232;
      table[i++]= 0.478862;
      table[i++]= 0.459184;
      table[i++]= 0.440627;
      table[i++]= 0.423801;
      table[i++]= 0.409159;
      table[i++]= 0.397399;
      table[i++]= 0.387263;
      table[i++]= 0.379368;
      table[i++]= 0.372236;
      table[i++]= 0.345984;
      table[i++]= 0.322728;
      table[i++]= 0.302808;
      table[i++]= 0.288663;
      table[i++]= 0.279361;
      table[i++]= 0.274451;
      table[i++]= 0.270980;
      table[i++]= 0.268569;
      table[i++]= 0.266890;
      table[i++]= 0.265401;
      table[i++]= 0.264338;

      while(i<=MAX_GAP_COUNT){table[i++]=0.0;}

      return(i);
}

/*************************************************************/

