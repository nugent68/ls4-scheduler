#!/bin/csh
#
# make_report.csh
#
# Read through log file from scheduler and determine:
# Total number of TNO and SN fields
# Fraction times for each program
# total deadtime
# dome open and close time
#
# syntax: make_report.csh yyyymmdd
#
set TEMP_FILE = "/tmp/make_report.tmp"
set TEMP_FILE1 = "/tmp/make_report.tmp1"
set READ_TIME = 41.4
set SCHEDULER_PATH = "/home/observer/scheduler"
#
if ( $#argv != 1 ) then
   echo "syntax: make_report.csh yyyymmdd"
   exit
endif
#
set d = $argv[1]
set log = $d.log
#
echo "Palomar Quest Survey Report for $d"
echo " "
#
if ( ! -e  $log ) then
   echo "can't find $log"
   exit
endif
#
# clean bad lines out of qa.log
if ( -e qa.log ) then
   grep -ve " -1.0 " qa.log | grep -ve "process" > $TEMP_FILE1
endif
#
# get mean and rms seeing
if ( -e qa.log ) then
   $SCHEDULER_PATH/bin/make_histogram 5 0 6 0.1 $TEMP_FILE1 > $TEMP_FILE
   set l = `grep mean $TEMP_FILE`
   if ( $#l == 4 ) then
      set mean_seeing = $l[4]
   else
      set mean)seeing = 0.0
   endif
#
   set l = `grep rms $TEMP_FILE`
   if ( $#l == 4 ) then
      set rms_seeing = $l[4]
   else
      set rms_seeing = 0.0
   endif
   set mean_seeing = `echo "scale=6; $mean_seeing * 0.878" | bc`
   set rms_seeing = `echo "scale=6; $rms_seeing * 0.878" | bc`
else
    set mean_seeing = 0.0
    set rms_seeing = 0.0
endif
#
# get mean and rms zero points
#
# first get all sky observations from log.obs
#
  grep -e " sky " log.obs > $TEMP_FILE
#
# For each sky observation, get the USNO zeropoint from the corresponding line in qa.log
#
if ( -e qa.log ) then
set n = `cat $TEMP_FILE | wc -l`
set i = 1
set n_points = 0
set z = 0.0
set z2 = 0.0
while ( $i <= $n )
  set l = `head -n $i $TEMP_FILE | tail -n 1`
  set name = $l[9]
  set expt = $l[5]
  set l = `grep -e "$name" $TEMP_FILE1`
  if ( $#l >= 11 ) then
    set zp1 = $l[11]
    set zp = `echo "scale = 6; $zp1 - ( 2.5 * l( $expt ) / l ( 10.0 ) ) " | bc -l`
    @ n_points = $n_points + 1
    set z = `echo "scale=6; $z + $zp" | bc`
    set z2 = `echo "scale=6; $z2 +  ( $zp * $zp )" | bc`
  endif
  @ i = $i + 1
end
else
   set n_points = 0
endif
if ( $n_points >= 2 ) then
   set mean_zp = `echo "scale=6; $z / $n_points" | bc`
   set z2 = `echo "scale=6; $z2 / $n_points" | bc`
   set rms_zp = `echo "scale=6; sqrt ( $z2 - ( $mean_zp * $mean_zp ) ) " | bc -l`
else if ( $n_points == 1 ) then
   set mean_zp = $z
   set rms_zp = 0.0
else
   set mean_zp = 0.0
   set rms_zp = 0.0
endif
#
printf " seeing: %4.3f +/- %4.3f arcsec\n" $mean_seeing $rms_seeing
printf "USNO zp: %5.3f +/- %5.3f\n" $mean_zp $rms_zp
/home/observer/palomar/scripts/get_area_time.csh $d
/home/observer/palomar/scripts/tally_fields.csh $d 

#
