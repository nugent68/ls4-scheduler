#!/bin/tcsh
#
# quest_neat_transfer.csh
# DLR 2007 Mar 12
#
# As new data arrives at $QUESTNEATDATADIR, use offsets2.csh to determine image quality.
# Save results to qa.log at $QUESTNEATDATADIR/yyyymmdd/logs.
#
# optional command argument is ut date of QUESTNEATDATADIR, in format "yyyymmdd"
#
unalias rm
set TEST = 0
# verbose messaging if VERBOSE = 1

set VERBOSE = 1

#
set PROCESS_SCRIPT = "~/palomar/scripts/offsets2.csh"
#
set SHORT_DELAY = 5

# get current ut_date, or read it from the command lines
 
if ( $#argv == 1 ) then
   set d_ut = $argv[1]
else
#   set d_ut = `date -u +%Y%m%d`
   set d_ut = `get_ut_date`
endif

#set d_ut = "20060413"

# set source directory

set SOURCE_DIR = "$QUESTNEATDATADIR/$d_ut"

#if ( $FAKE_OBS == 1 ) then
#  set SOURCE_DIR = $SOURCE_DIR.fake
#endif

#make sure SOURCE_DIR exists, otherwise exit

if ( ! -e $SOURCE_DIR ) then
   echo "$SOURCE_DIR does not exist, exiting"
   exit
endif

# set log directory

set LOG_DIR = "$SOURCE_DIR/logs"

#make sure LOG_DIR exists, otherwise exit

if ( ! -e $LOG_DIR ) then
   echo "$LOG_DIR does not exist, exiting"
   exit
endif

set LOG = "$LOG_DIR/quest_neat_qa.log"
set QA_LOG = "$LOG_DIR/qa.log"
 
# initialize log  and qa_log files if they don't exist

if ( ! -e $LOG ) then
   touch $LOG
endif

if ( ! -e $QA_LOG ) then
   touch $QA_LOG
   echo "# image             ndetected   sky & sig   fwhm & sig    a     b   theta  bias usnozp ra_offset dec_offset" >> $QA_LOG
   echo "#" >> $QA_LOG
endif

echo `date` ": starting quest_neat_qa.csh" >> $LOG
echo `date` ": source directory $SOURCE_DIR" >> $LOG

# Enter while loop that repeats every SHORT_DELAY seconds. On each loop,
# see if there is a new image to process. If so, process and append
# results to log.


set x = `pwd`
set done = 0
while ( $done == 0 )
#
      set process_flag = 0
#
#     get time-ordered listing of all A14 sky images
#
      cd $SOURCE_DIR
      set l = `ls -rt *s.A14.*fits |& grep -ve "No match"`
      cd $x
#
#     Go through each image in the list and see if any have not yet been
#     processed. If so, process the first one found
#
      if ( $#l > 0 ) then
#
#        set file name to name of first A14 image that is not already
#        listed as processed in the QA_LOG. If none are found, set process_flag=0.
#
#        check all the A14 images in list l, indexed by i. If a new, unprocessed
#        image is found, set i to the number of list member + 1. This will
#        skip a check of all the remaining images in the list.
#
         set i = 1
#
         while ( $i <= $#l ) 
#
           set file = $l[$i]

           set file_name = `echo $file | cut -c 1-14`
#
#          look for the appriate file name followed by "complete" in the log file
#          If it is not there, this is the next file to process.
             
           set n = `grep $file_name $QA_LOG | wc -l`
#
#          if the line is not found, n will 0.  Set process_flag to 1 and i = $#i + 1
#          to exit the loop.
#
           if ( $n == 0) then 
             echo "$file_name not yet processed: $n instances in $QA_LOG" >> $LOG
             set process_flag = 1
             set obs_number = $i
             @ i = $#l + 1
#
#          otherwise keep checking through the list
           else
             @ i = $i + 1
           endif
         end
#
      endif

#

   if ( $process_flag ) then


       set process_time1 = `date +%s`
       echo `date` ": processing $file " >> $LOG

       cd $LOG_DIR
       ln -s $SOURCE_DIR/$file ./$file
       set l = `$PROCESS_SCRIPT $file`
       if ( $#l == 2 ) then
         set ra_offset = `printf "%8.6f" $l[1]`
         set dec_offset = `printf "%8.6f" $l[2]`
       else
         set ra_offset = 0.0
         set dec_offset = 0.0
       endif
       if ( -e $file_name.stats) then
          set l1 = `cat $file_name.stats`
          set name = $l1[1]
          echo $l1 $ra_offset $dec_offset >> $QA_LOG
          rm $LOG_DIR/$file_name.stats
          rm $LOG_DIR/$file
       else
         echo "# $file could not process" >> $QA_LOG
         echo "# $file could not process" >> $LOG   
         rm $LOG_DIR/$file
       endif

       set process_time2 = `date +%s`

       @ dt = $process_time2 - $process_time1

       echo `date` ": $file processing complete in $dt seconds" >> $LOG
  
    else

       if  ( $VERBOSE ) then
         echo `date` ": no new images to process" >> $LOG
       endif

    endif
    
    sleep $SHORT_DELAY 

end  

