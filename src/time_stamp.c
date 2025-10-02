/* time_stamp.c 

   read standard input.
   Write out each line prefaced my current date and time

   DLR 2007 Mar 8
*/

#include <stdio.h>
#include <time.h>

int get_date(char *date);
double get_ut(struct tm *tm_out);

/************************************************************/

main()
{
    char string[1024],date[1024];

    while(fgets(string,1024,stdin)!=NULL){
         get_date(date);
         fprintf(stdout,"%s : %s",date,string);
         fflush(stdout);
    }
 
    exit(0);
}

/************************************************************/

int get_date(char *date)
{
   double ut;

   struct tm tm;
   ut=get_ut(&tm);

   sprintf(date,"%04d%02d%02d:%7.6f",
    tm.tm_year,tm.tm_mon,tm.tm_mday,ut/24.0);

   return(0);

}
/*****************************************************/

double get_ut(struct tm *tm_out) {

  time_t t;
  struct tm *tm;
  double ut;

  time(&t);
  tm = gmtime(&t);
  tm->tm_mon=tm->tm_mon+1;
  tm->tm_year=tm->tm_year+1900;
  ut = tm->tm_hour + tm->tm_min/60. + tm->tm_sec/3600.;

  if(tm_out!=NULL)*tm_out=*tm;

  return(ut);
}


/************************************************************/

