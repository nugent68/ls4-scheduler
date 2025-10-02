/* scheduler_status.c

   routines to interpret camera status string.

*/

#include "scheduler.h"
#include <string.h>
#include <ctype.h>


char *state_name[NUM_STATES];

#define name (const char *[NUM_STATES])  {"NOSTATUS", "UNKNOWN", "IDLE", "EXPOSING", "READOUT_PENDING", "READING", "FETCHING", "FLUSHING", "ERASING", "PURGING", "AUTOCLEAR", "AUTOFLUSH", "POWERON", "POWEROFF", "POWERBAD", "FETCH_PENDING", "ERROR", "ACTIVE", "ERRORED"}


/*****************************************************/
int init_status_names()
{
  int i;
  for (i=0;i<NUM_STATES;i++){
      state_name[i]=(char *)malloc(1+sizeof(char)*strlen(name[i]));
      strcpy(state_name[i],name[i]);
  }
}

/*****************************************************/
/* parse  reply string to find status keyword and status value. If found, copy  
  value to value_string and return length of value string.
  If not found, return 0
  
  expected string format begings  "[", followed by "ERROR" or "DONE",
  followed by keyword/value pairs, where each pair is expressed

    'keyword': value

  where value is either True, False, or a string bracketed by "'",
  and separator "," appears between keyword/value expression.

  The last keyword/value expression is followed by "]".

  For example:

   [DONE 
    {'ready': True,
   'state': 'started',
   'error': False,
   'comment': 'started',
   'date': '2025-06-24T20:15:56.00',
   'NOSTATUS': '0000',
   'UNKNOWN': '0000',
   'IDLE': '1111',
   'EXPOSING': '0000',
   'READOUT_PENDING': '0000',
   'READING': '0000',
   'FETCHING': '0000',
   'FLUSHING': '0000',
   'ERASING': '0000',
   'PURGING': '0000',
   'AUTOCLEAR': '0000',
   'AUTOFLUSH': '0000',
   'POWERON': '1111',
   'POWEROFF': '0000',
   'POWERBAD': '0000',
   'FETCH_PENDING': '0000',
   'ERROR': '0000',
   'ACTIVE': '0000',
   'ERRORED': '0000', 
   'cmd_error':False, 
   'cmd_error_msg':'False', 
   'cmd_command':'', 
   'cmd_arg_value_list':'', 
   'cmd_reply':''
   ]
*/
int get_value_string(char *reply, char *keyword, char *separator, char *value_string)
{
  char *strptr1, *strptr2,temp_string[1024];
  int n;

  strptr1 = NULL;
  strptr2 = NULL;
  n = 0;
  *value_string = 0;

  //set strptr1 to point to the beginning of the specified keyword

  strptr1 = strstr(reply, keyword);
  if (strptr1 != NULL){
     // if found, advance strptr1 to the location of ":"  immediately following
     // the keyword
     strptr1 = strstr(strptr1,":");

     // if strptr1 is found, set strptr2 to the location of separator (i.e. ",")
     // immediately following ":"
     if (strptr1 != NULL)
	  strptr2 = strstr(strptr1, separator);

	
     // increment strptr1 to point to the first character following ":"
     strptr1 += 1;

     // decrement strptr2 to point the first character preceeding ","
     strptr2 -= 1;

  }

  // if both strptr1 and strptr2 are not NULL, assume the value string
  // is bracketed by these two pointers. If the value is a string also bracketed by
  // "'", then strip these characters from value string.
  //
  if (strptr1 != NULL && strptr2 != NULL){

      n=strptr2-strptr1+1;
      strncpy(value_string,strptr1,n);
      *(value_string + n) = 0;

      // copy value string into temp string and check temp string
      // for "'" at beginning and end
      strptr1 = NULL;
      strptr2 = NULL;
      strcpy(temp_string,value_string);
      strptr1 = strstr(temp_string,"'");


      if (strptr1 != NULL ){
	  strptr1 += 1;
	  strptr2 = strstr(strptr1,"'");
	  if (strptr2 != NULL)
	     strptr2 -= 1;
      }
      
      // if "'" found at beginning and end of temp_string, copy the
      // characters bracketed by "'" into value string.
      if (strptr1 != NULL &&  strptr2 != NULL){
        n=strptr2-strptr1+1;
        strncpy(value_string,strptr1,n);
        *(value_string + n) = 0;
      }

  }
  else{
      fprintf(stderr,"ERROR: can not find keyword [%s] in status string [%s]\n",
             keyword,reply);
  }
  return n;
}

/*****************************************************/
/* convert a string to boolean  assuming the string is one of the following:
 * "True","true","TRUE","False","false", or "FALSE"
*/
bool string_to_bool(char *string)
{
  
  if(strstr(string,"True") != NULL || strstr(string,"true") != NULL ||
	strstr(string,"TRUE") != NULL){
    return True;
  }
  else if( strstr(string,"False") != NULL || strstr(string,"false") != NULL ||
	strstr(string,"FALSE") != NULL){
    return False;
  }
  else{
    fprintf(stderr,"WARNING: string [%s] does not express a boolean value\n",string);
    fflush(stderr);
    return False;
  }
}

/*****************************************************/
/* convert ascii string of zeros and ones to an integer*/
int binary_string_to_int(char *binaryString) 
{
    int result = 0;
    int len = strlen(binaryString);

    for (int i = 0; i < len; i++) {
        if (!isdigit(binaryString[i])) {
            return -1;
        }
        if (binaryString[i] != '0' && binaryString[i] != '1') {
	     fprintf(stderr,"ERROR: can not convert string [%s] to integer\n",binaryString);
	     fflush(stderr);
             return -1;
        }
        result = (result << 1) | (binaryString[i] - '0');
    }
    return result;
}


/*****************************************************/
/* convert integer to string binary representation of the integer*/
int int_to_binary_string(int n, char *string)
{
    char s[2];
    for (int i = 4; i > 0; i--) {
      sprintf(s,"%1d", (n & 1));
      strncpy(string+4-i,s,1);
      n >> 1;
    }
    *(string+4)=0;
    return (0);
}

/*****************************************************/
/* find keyword in reply string and interpret value as  boolean */

bool get_bool_status(char *keyword, char *reply)
{
  char string[1024];
  int n;

  n = get_value_string(reply,keyword,",",string);
  if (n>0) 
     return string_to_bool(string);
  else
     return False;
}

/*****************************************************/
/* look for specified keyword in reply string and copy the associastedi value
 * string  into the status string */

int  get_string_status(char *keyword, char *reply, char *status)
{
  char string[1024];
  int n;

  strcpy(status,"UNKNOWN");
  n = get_value_string(reply,keyword,",",string);
  if (n>0) strcpy(status,string);

  return(n);
}

/*****************************************************/

/* parse the keyword values from the reply string and store in
 * Camera_Status record.
*/

int parse_status(char *reply,Camera_Status *status)
{
  int i;
  char temp_string[1024];

  status->ready= get_bool_status("ready",reply);
  status->error= get_bool_status("error",reply);

  get_string_status("state",reply,status->state);
  get_string_status("comment",reply,status->comment);
  get_string_status("date",reply,status->date);

  for (i=0;i<NUM_STATES;i++){
     status->state_val[i]=-1;
     get_string_status(state_name[i],reply,temp_string);
     status->state_val[i]=binary_string_to_int(temp_string);
  }

  return (0);
}

/*****************************************************/

/* print the values in the specified Camera_Status record */
int print_camera_status(Camera_Status *status,FILE *output)
{
   fprintf(output,"%20s  : %s\n","UT date",status->date);  
   fprintf(output,"%20s  : %s\n","state ",status->state); 
   fprintf(output,"%20s  : %s\n","comment",status->comment); 
   fprintf(output,"%20s  : %s\n","ready",status->ready ? "True" : "False");
   fprintf(output,"%20s  : %s\n","error",status->error ? "True" : "False");

   int i;
   char s[32];
   for (i=0;i<32;i++){s[i]=0;}

   for (i=0;i<NUM_STATES;i++){
      int_to_binary_string(status->state_val[i],s);
      fprintf(output,"%20s  : %s\n",state_name[i],s);  
   }

   return(0);
}
