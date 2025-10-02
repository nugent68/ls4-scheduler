#!/bin/tcsh
#
# quest_neat_transfer.csh
# DLR 2007 Mar 12
#
# As new data arrives at $QUESTNEATDATADIR, compress and copy to  $REALTIME_TRANSFER_DIR
#
# optional command argument is ut date of QUESTNEATDATADIR, in format "yyyymmdd"
#
# assume NEAT format for image names: yyyymmddhhmmssx.yyy.fits
# where yyyy mm dd hh mm ss is the UT date, x is "s" for sky image, "d" for dark,
# and yyy is A01, A02,..., D28.
#
# Make a tar for file for the 112 images from each star consisting of the compressed
# images. Tar file name is yyyymmddhhmmssx.zzz.fits where zzz is the obs
# number ( starting with 1 for the first image of the night).
#
#
set TEST = 0
# verbose messaging if VERBOSE = 1

set VERBOSE = 1

#
set SCRATCH_AREA = "/scr1/scratch"
#
# time (sec) to wait after last check for new frame

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
#set SOURCE_DIR = "$QUESTNEATDATADIR"

#

#make sure SOURCE_DIR exists, otherwise exit

if ( ! -e $SOURCE_DIR ) then
   echo "$SOURCE_DIR does not exist, exiting"
   exit
endif

# make sure REALTIME_TRANSFER_DIR exists, otherwise exit

if ( ! -e $REALTIME_TRANSFER_DIR ) then
   echo "$REALTIME_TRANSFER_DIR does not exist, exiting"
   exit
endif

 
# make transfer directory exists if it doesn't already exist
 
set TRANSFER_DIR = "$REALTIME_TRANSFER_DIR"

if ( $TEST ) then
   set TRANSFER_DIR = "$TRANSFER_DIR/test"
endif

if ( ! -e  $TRANSFER_DIR) then

   mkdir $TRANSFER_DIR

endif

# make sure scratch area exists. If so, erase its contents

if ( ! -e $SCRATCH_AREA) then
    echo "ERROR: no scratch area $SCRATCH_AREA"
    exit
endif

unalias rm
rm $SCRATCH_AREA/*


set LOG = "$TRANSFER_DIR/transfer.log.$d_ut"
 
# initialize log file if it doesn't exist

if ( ! -e $LOG ) then
   touch $LOG
endif

echo `date` ": starting realtime_stare_transfer.csh" >> $LOG

# Enter while loop that repeats every SHORT_DELAY seconds. On each loop,
# see if there is a new image to transfer. If so, compress and tar the
# images (112 per exposure). Another transfer script will see the tar file
# and transfer it the appropriate place


set x = `pwd`
set done = 0
while ( $done == 0 )
#
      if ( $VERBOSE ) then
         echo `date` ":  checking for new images" >> $LOG
      endif
#
      set transfer_flag = 0
#
#     get time-ordered listing of all A01 images
#
      cd $SOURCE_DIR/
      set l = `ls -rt *A01.*fits |& grep -ve "No match"`
      cd $x
#
#     Go through each image in the list and see if any have not yet been
#     transfered. If so, transfer the first one found
#
      if ( $#l > 0 ) then
#
#        set file name to name of first A image that is not already
#        listed as transfered in the LOG. If none are found, set transfer_flag=0.
#
#        check all the A images in list l, indexed by i. If a new, untransferred
#        image is found, set i to the number of list member + 1. This will
#        skip a check of all the remaining images in the list.
#
         set i = 1
#
         while ( $i <= $#l ) 
#
           set file = $l[$i]

           set file_name = `echo $file | cut -c 1-15`
#
#          look for the appriate file name followed by "complete" in the log file
#          If it it not there, this is the next file to transfer.
             
           set n = `grep $file_name $LOG | grep complete | wc -l`
#
#          if the line is not found, n will 0.  Set transfer_flag to 1 and i = $#i + 1
#          to exit the loop.
#
           if ( $n == 0) then 
             echo "$file_name not yet transferred" >> $LOG
             set transfer_flag = 1
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

   if ( $transfer_flag ) then


       set transfer_time = `date +%s`

       set name = $file_name

       set obs_string = `printf "%03d" $obs_number`
       set TAR_FILE = $name.$obs_string.tar
       set SUM_FILE = $name.$obs_string.chk

       set time = `date +%s`
       set dt = 0

       cd $x

       echo `date` ": $name.$obs_string  starting transfer" >> $LOG

       if ( $VERBOSE == 1 ) then
           echo `date` ": copying image $name.$obs_string to scratch area" >> $LOG
       endif

       cp $SOURCE_DIR/$name.*.fits $SCRATCH_AREA
       if ( $VERBOSE == 1 ) then
           echo `date` ": compressing image $name.$obs_string" >> $LOG
       endif

       set x = `pwd`
       gzip $SCRATCH_AREA/$name.*.fits
       cd $x

       if ( $VERBOSE == 1 ) then
           echo `date` ": tarring image $name.$obs_string to $TAR_FILE" >> $LOG
       endif

       set x = `pwd`
       cd $SCRATCH_AREA
       tar cf $TRANSFER_DIR/$TAR_FILE ./$name.*.fits.gz  >>& $LOG
       rm ./$name.*.fits.gz
       cd $x

       if ( $VERBOSE == 1 ) then 
          echo `date` ": generating checksum file $SUM_FILE" >> $LOG
       endif

       set x = `pwd`
       cd $TRANSFER_DIR
       md5sum $TAR_FILE > $SUM_FILE
       set l4 = `cat $SUM_FILE`
       echo `date` ": $TAR_FILE $l4[1] checksum " >> $LOG
       cd $x

#      add .ready suffix to completed files

       set x = `pwd`
       cd $TRANSFER_DIR
       mv $TAR_FILE $TAR_FILE.ready
       mv  $SUM_FILE  $SUM_FILE.ready
       cd $x
      
       echo `date` ": $name.$obs_string transfer complete" >> $LOG
#
#      record transfer time 
       set transfer_time = `date +%s`
  
    else

       if  ( $VERBOSE ) then
         echo `date` ": no new images to transfer" >> $LOG
       endif

    endif
    
    if ( $VERBOSE == 1 ) then
      echo `date` ": sleeping $SHORT_DELAY sec" >> $LOG
    endif

    sleep $SHORT_DELAY 

end  

