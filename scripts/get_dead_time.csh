#!/bin/csh
#
# get_dead_time.csh
#
# Read through log file from scheduler and determined total time
# there were no fields ready to observe between UT start and end
#
# syntax: get_dead_time.csh  log_file
# output: total dead time (h)
#
set TEMP_FILE = "/tmp/get_dead_time.tmp"
#
if ( $#argv != 1 ) then
  echo "syntax: get_dead_time.csh log_file"
  exit
endif
#
set log = $argv[1]
#
if ( ! -e $log ) then
   echo "file $log doesn't exist"
   exit
endif
#
set l = `grep -e "ut start" $log`
if ( $#l != 5 ) then
   echo "format unexpected for ut start: $l"
   exit
endif
# 
set ut_start = $l[5]
#
set l = `grep -e "ut end" $log`
if ( $#l != 5 ) then
   echo "format unexpected for ut end: $l"
   exit
endif
# 
set ut_end = $l[5]
#
set night_length = `echo "scale=6; $ut_end - $ut_start" | bc`
#
grep -e "No fields ready to observe" $log > $TEMP_FILE
set n = `cat $TEMP_FILE | wc -l`
if ( $n == 0 ) then
  printf "%7.3f %7.3f\n" 0.0 $night_length
  exit
endif
#
set i = 1
set t = 0
while ( $i <= $n )
  set l = `head -n $i $TEMP_FILE | tail -n 1`
  if ( $#l != 9 ) then
     echo "unexpected format in log file: $l"
     exit
  endif
  set ut = $l[4]
  set a = `echo "scale=6; if ( $ut > $ut_start && $ut < $ut_end ) 1 else 0" | bc`
  if ( $a > 0 ) then
     @ t = $t + 1
  endif
  @ i = $i + 1
end
set dead_time = `echo "scale=6; $t / 60.0" | bc`
printf "%7.3f %7.3f\n" $dead_time  $night_length $ut_start $ut_end
#
