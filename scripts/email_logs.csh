#!/bin/tcsh
#
# email quest point&shoot logs from previous night to distribution
#
#set d = `date -u +"%Y%m%d"`
set d = `get_ut_date`
set log_dir = $QUESTDATA/$d/logs
set recipients = "quest-log@hepmail.physics.yale.edu"
set sender = "david.rabinowitz@yale.edu"
set TEMP_FILE = "/tmp/email_logs.tmp"
#
set CAMERA_MODE_FILE = "/home/observer/.camera_mode"
#
# check if there is a camera_mode file. If so, and the first line as the
# word "driftscan", then the current night is scheduled for driftscan.
# Send a message to this effect
#
if ( -e $CAMERA_MODE_FILE ) then
  set n = `cat $CAMERA_MODE_FILE | grep -e "driftscan" | wc -l`
  if ( $n > 0 ) then
      printf "Subject: Palomar Quest Report for $d. No obs. Driftscan night.\n" >! $TEMP_FILE
      printf "\n" >> $TEMP_FILE
      printf "\n" >> $TEMP_FILE
#      cat $TEMP_FILE | sendmail -fdavid.rabinowitz@yale.edu $RECIPIENTS
      cat $TEMP_FILE | sendmail -fdavid.rabinowitz@yale.edu david.rabinowitz@yale.edu
      exit
  endif
endif
#
if ( -e $log_dir ) then
   cd $log_dir
else
   echo " $log_dir does not exist"
   exit
endif
#
set l = `/home/observer/palomar/scripts/make_report.csh $d > report_$d.txt`
set files = "`ls $d*` logfile log.obs qa.log report_$d.txt"
echo $files
#
set title = "Palomar Quest Report for $d"
dirmail -s "$title"  -t $recipients -f $sender $files
#


