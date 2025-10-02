/* get_time_history.c 

   read time-sorted log files and determine
   time-gap distribution for SNE fields

*/

#include <stdio.h>
#include <string.h>

#define MAX_OBS_PER_FIELD 1000
#define MAX_FIELDS 5000
#define MAX_INTERVAL 100

typedef struct {
   int index;
   int n_obs; 
   double jd[MAX_OBS_PER_FIELD];
} Field;

main(int argc, char **argv)
{
   Field *f;
   FILE *input;
   int i, j, n_fields,index,count5;
   char string[1024],s[256];
   double jd0,jd,expt;
   int count[MAX_INTERVAL],dt,dt_max;
   int n_one_nighters,n_total,integral;

   if(argc!=3){
      fprintf(stderr,"syntax: get_time_history log_file jd_start\n");
      exit(-1);
   }

   sscanf(argv[2],"%lf",&jd0);
   input=fopen(argv[1],"r");
   if(input==NULL){
       fprintf(stderr,"can't open file %s for input\n",argv[1]);
       exit(-1);
   }

   f=(Field *)malloc(MAX_FIELDS*sizeof(Field));
   if(f==NULL){
      fprintf(stderr,"could not get memory for Fields\n");
      exit(-1);
   }

   for(i=0;i<MAX_FIELDS;i++){f[i].n_obs=0;}
   n_total=0;

   while(fgets(string,1024,input)!=NULL){
/*
13.359190  13.626470 s 2   60.0  10.646 2454207.943576  60.250 20070417103845s # sky 15 2 2800
2.733350   9.084310 s 2   60.0   2.149 2455467.880706  60.160 20100928090813s # sky 179 2548
*/
     sscanf(string,"%s %s %s %s %lf %s %lf %s %s %s %s %s %s %d",
	s,s,s,s,&expt,s,&jd,s,s,s,s,s,s,&index);
#if 0
      if(strstr(string,"track")==NULL&&strstr(string,"TNO")==NULL&&(expt==60.0||expt==240.0||expt==80.0)){
        if(index<0 || index>MAX_FIELDS){
           fprintf(stderr,"index %d out of range\n",index);
           exit(-1);
        }
        if(f[index].n_obs==MAX_OBS_PER_FIELD){
           fprintf(stderr,"too many obs of field %d\n",index);
           fprintf(stderr,"%s\n",string);
           exit(-1);
        }
        f[index].jd[f[index].n_obs]=jd;
        f[index].n_obs=f[index].n_obs+1;
        if(jd>jd0)n_total++;
      }
#else
      if(strstr(string,"track")==NULL&&strstr(string,"TNO")==NULL&&(expt==60.0||expt==240.0||expt==80.0)&&index>0&&index<=MAX_FIELDS){
        if(f[index].n_obs==MAX_OBS_PER_FIELD){
           fprintf(stderr,"too many obs of field %d\n",index);
           exit(-1);
        }
        f[index].jd[f[index].n_obs]=jd;
        f[index].n_obs=f[index].n_obs+1;
        if(jd>jd0)n_total++;
      }
#endif
   }

   fclose(input);
   for(i=0;i<MAX_INTERVAL;i++){count[i]=0;}

   n_one_nighters=0;
   n_fields=0;
   count5=0;
   integral = 0;
   for(i=0;i<MAX_FIELDS;i++){
        if(f[i].n_obs==1&&f[i].jd[0]>=jd0){
            count[0]=count[0]+1;
	    n_fields++;
            n_one_nighters++;
        }
        else if(f[i].n_obs>1){
          /*if(f[i].jd[f[i].n_obs-1]>=jd0)n_fields++;*/
          dt_max=0;
          for(j=1;j<f[i].n_obs;j++){
             if(f[i].jd[j]>=jd0){
                 n_fields++;
                 dt=f[i].jd[j]-f[i].jd[j-1];
                 if(dt>7){
                    count5++;
                    integral=integral + 7.0;
                 }
                 else if(dt>3){
                    count5++;
                    integral=integral + dt;
                 }
                 else if (dt > 1 ){
                    integral=integral + dt;
                 }
                 else if(dt<0||dt>MAX_INTERVAL){
                      fprintf(stderr,"dt %d out of range\n",dt);
                      exit(-1);
                 }
                 if(dt>dt_max)dt_max=dt;
                 count[dt]=count[dt]+1;
             }
          }
          if(dt_max==1)n_one_nighters++;
        }
    }
      
    printf("# %d total_obs\n# %d fields\n# %d one_nighters\n# %d >3-day intervals\n# %d days interval-sum\n",
              n_total, n_fields,n_one_nighters,count5,integral);

/*
    for(i=0;i<MAX_INTERVAL;i++){
      printf("%d %d\n",i,count[i]);
    }
*/
    free(f);
  
    exit(0);
}
  


             
       
