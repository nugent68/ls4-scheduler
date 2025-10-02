#!/bin/csh
#
# grab_obsplan.csh
#
#grab obsplan from guest directory and move to obsplan directory inside
#folder with correct date
#
#
unalias rm
unalias cp
set guest_dir = /home/guest
set obsplan_dir = /home/observer/obsplans
set TEMP_FILE = "/tmp/grab_obsplan.tmp"
set TEMP_FILE1 = "/tmp/grab_obsplan.tmp1"
# last check is local time $HOUR_LIMIT:00 
set HOUR_LIMIT = 20
setenv SHELL /bin/tcsh
#
set RECIPIENTS = "guest,quest-log@hepmail.physics.yale.edu"
set CAMERA_MODE_FILE = "/home/observer/.camera_mode"
set d = `get_ut_date`
#
# check if there is a camera_mode file. If so, and the first line as the
# word "driftscan", then the current night is scheduled for driftscan.
# Send a message to this effect
#
if ( -e $CAMERA_MODE_FILE ) then
  set n = `cat $CAMERA_MODE_FILE | grep -e "driftscan" | wc -l`
  if ( $n > 0 ) then
      printf "Subject: Palomar Quest: Driftscan scheduled for $d. Obsplan not loaded\n" >! $TEMP_FILE1
      printf "\n" >> $TEMP_FILE1
      printf "\n" >> $TEMP_FILE1
      cat $TEMP_FILE1 | sendmail -fdavid.rabinowitz@yale.edu $RECIPIENTS
#      cat $TEMP_FILE1 | sendmail -fdavid.rabinowitz@yale.edu david.rabinowitz@yale.edu
      exit
  endif
endif
#
if ( ! -e $obsplan_dir/$d ) then
   echo "mkdir $obsplan_dir/$d"
   mkdir $obsplan_dir/$d
endif
#
if ( -e $guest_dir/$d.obsplan ) then
   date >! $TEMP_FILE
   echo "" >> $TEMP_FILE
   echo " cp $guest_dir/$d.obsplan $obsplan_dir/$d" 
   cp $guest_dir/$d.obsplan $obsplan_dir/$d
   echo "$d.obsplan found and installed" >> $TEMP_FILE
   echo "" >> $TEMP_FILE
   cat $obsplan_dir/$d/$d.obsplan >> $TEMP_FILE
#   mail -s "Palomar Quest $d.obsplan installed" $RECIPIENTS < $TEMP_FILE
   printf "Subject: Palomar Quest $d.obsplan installed\n" >! $TEMP_FILE1
   printf "\n" >> $TEMP_FILE1
   cat $TEMP_FILE >> $TEMP_FILE1
   printf "\n" >> $TEMP_FILE1
   cat $TEMP_FILE1 | sendmail -fdavid.rabinowitz@yale.edu $RECIPIENTS

else
   set h = `date +"%H"`
   date >! $TEMP_FILE
   echo "" >> $TEMP_FILE
   echo "no obsplan yet for tonight ( $d ) !" >> $TEMP_FILE

   if ( $h < $HOUR_LIMIT ) then
#          echo "SHELL is $SHELL" >> $TEMP_FILE
          echo "rechecking in 30 minutes " >> $TEMP_FILE
   else
          echo "giving up. No more checks for tonight" >> $TEMP_FILE
   endif

#   mail -s "Palomar Quest: No obsplan yet for $d" $RECIPIENTS < $TEMP_FILE
   printf "Subject: Palomar Quest: No obsplan yet for $d\n" >! $TEMP_FILE1
   printf "\n" >> $TEMP_FILE1
   printf "\n" >> $TEMP_FILE1
   cat $TEMP_FILE1 | sendmail -fdavid.rabinowitz@yale.edu $RECIPIENTS

   if ( $h < $HOUR_LIMIT ) then
       at -f "/home/observer/palomar/scripts/grab_obsplan.csh" now + 30 minutes
   endif
endif
#
