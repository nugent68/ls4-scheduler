/* bin.c */

#include <stdio.h>
#include <string.h>
#include <math.h>
#define MAX_BINS 1000


extern char *get_col();
extern double sqrt();

main(argc,argv)
int argc;
char **argv;
{
    char string[1024],s[256],*s1;
    float n_bins,h1,h2,dh,h;
    float sum,sy,syy;
    int i,count[MAX_BINS],col,i_max,count_max;
    FILE *input;
    
    if(argc!=6){
        printf("syntax: bin column h1 h2 dh file\n");
        exit(-1);
    }
    
    sscanf(argv[1],"%d",&col);
    sscanf(argv[2],"%f",&h1);
    sscanf(argv[3],"%f",&h2);
    sscanf(argv[4],"%f",&dh);
    
    n_bins=(h2-h1)/dh;
    
    if(n_bins>MAX_BINS){
        printf("too many bins\n");
        exit(0);
    }
    
    input=fopen(argv[5],"r");
    if(input==NULL){
        printf("can't open file %s\n",argv[5]);
        exit(-1);
    }
    
    for(i=0;i<n_bins;i++)count[i]=0;
    sum=0.0;
    sy=0.0;
    syy=0.0;
    i_max=0;
    count_max=0;
    
    while(fgets(string,1024,input)!=0){
       if(strstr(string,"#")==NULL){
           s1=get_col(string,col);
           if(s1==NULL){
	        printf("not enough columns in string: %s\n",string);
	        exit(-1);
           }
           sscanf(s1,"%f",&h);
           if (h != 0.0 ) {
             sum = sum + 1.0;
             sy=sy+h;
             syy=syy+(h*h);
           }
           i=0.5+((h-h1)/dh);
           if(i<n_bins&&i>=0)count[i]=count[i]+1;
           if(count[i]>count_max){
              count_max=count[i];
              i_max=i;
           }
        }
    }
    fclose(input);
    
    sy=sy/sum;
    syy=syy/sum;
    syy=sqrt(syy-sy*sy);
    printf("# mean = %10.6f\n",sy);
    printf("# rms = %10.6f\n",syy);
    printf("# mode = %10.6f\n",h1+i_max*dh);
    for(i=0;i<n_bins;i++){
       printf("%7.3f %d\n",h1+i*dh,count[i]);
    }
    
    exit(0);
}


char *get_col(s,col)
char *s;
int col;
{
    int i,n;
    char *s1,*st,*sc;
    char tab_char[2],space_char[2];
    sprintf(tab_char,"\t");
    sprintf(space_char," ");

    i=0;
    s1=s;
    i++;
    while(i<col){
         while(strncmp(s1,space_char,1)==0||strncmp(s1,tab_char,1)==0)s1++;

         st=strstr(s1,tab_char);
         sc=strstr(s1,space_char);
         if(st==NULL&&sc==NULL)return(NULL);
         if(sc==NULL){
           s1=st;
         }
         else if (st==NULL){
           s1=sc;
         }
         else if(st<sc){
           s1=st;
         }
         else{
           s1=sc;
         }
         
         i++;
    }
    return(s1);
}

       
    
    
    
