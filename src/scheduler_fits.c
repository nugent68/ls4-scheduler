/* scheduler_fits.c

   DLR 2007 Mar 7

   Routines to initialize and update FITS header for each FITS image
*/

#include "scheduler.h"

#define BLANK_VALUE "UNKNOWN"


extern int verbose;
extern int verbose1;

int init_fits_header(Fits_Header *header);
int update_fits_header(Fits_Header *header, char *keyword, char *value);
int add_fits_word(Fits_Header *header, char *keyword, char *value);


/************************************************************/

int update_fits_header(Fits_Header *header, char *keyword, char *value)
{
    int i;

    if(strlen(value)==0||strcmp(value," ")==0)strcpy(value,BLANK_VALUE);

    if(verbose){
       fprintf(stderr,"update_fits_header: setting %s to %s\n",
         keyword,value);
    }

    i=0;
    while(strncmp(header->fits_word[i].keyword,keyword,strlen(keyword))!=0&&
         i<header->num_words){
        i++;
     }

    if(i>=header->num_words){
      fprintf(stderr,"update_fits_header: keyword %s not recognized\n",
		keyword);
      return(-1);
    }

    strcpy(header->fits_word[i].value,value);

    if(verbose){
       fprintf(stderr,"update_fits_header: %d %s %s\n",
         i,header->fits_word[i].keyword,header->fits_word[i].value);
    }
    return(0);
}
/************************************************************/

int init_fits_header(Fits_Header *header)
{       
	int n;

        header->num_words=0;
        if(add_fits_word(header,FILTERNAME_KEYWORD,BLANK_VALUE)<0)return(-1);
        else if(add_fits_word(header,FILTERID_KEYWORD,"0")<0)return(-1);
        else if(add_fits_word(header,LST_KEYWORD,"0.0")<0)return(-1);
        else if(add_fits_word(header,HA_KEYWORD,"0.0")<0)return(-1);
        else if(add_fits_word(header,IMAGETYPE_KEYWORD,BLANK_VALUE)<0)return(-1);
        else if(add_fits_word(header,DARKFILE_KEYWORD,BLANK_VALUE)<0)return(-1);
        else if(add_fits_word(header,FLATFILE_KEYWORD,BLANK_VALUE)<0)return(-1);
        else if(add_fits_word(header,SEQUENCE_KEYWORD,BLANK_VALUE)<0)return(-1);
        else if(add_fits_word(header,RA_KEYWORD,"0.0")<0)return(-1);
        else if(add_fits_word(header,DEC_KEYWORD,"0.0")<0)return(-1);
	else if(add_fits_word(header,FOCUS_KEYWORD,"0.0")<0)return(-1);
	else if(add_fits_word(header,COMMENT_KEYWORD,BLANK_VALUE)<0)return(-1);

        if(verbose1){
          fprintf(stderr,
            "init_fits_header: %d keywords added\n",header->num_words);
          fflush(stderr);
        }

        return(0);

}
/************************************************************/

int add_fits_word(Fits_Header *header,char *keyword, char *value)
{
    int n;

    n=header->num_words;
    
    if(n>=MAX_FITS_WORDS)return(-1);

    strcpy(header->fits_word[n].keyword,keyword);
    strcpy(header->fits_word[n].value,value);

    n++;
    header->num_words=n;

    return(0);
}

/************************************************************/
