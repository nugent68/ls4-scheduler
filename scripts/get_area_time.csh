#!/bin/csh
#
# get_area_time.csh
#
# concatenate all point and shoot logs
# grep for sky fields
# sort by JD
# get time interval distribution for all fields
#
# syntax  get_area_time.csh yyyymmdd
#
if ( $#argv != 1 ) then
   echo "syntax  get_area_time.csh yyyymmdd"
   exit
endif
#
unalias rm
alias s '~/csh.dir/sort.csh'
set LOG_ALL =  "/scr1/observer/quest/pt_logs.all"
set LOG_LIST = "/scr1/observer/quest/pt_logs.list"
set TEMP_FILE = "/tmp/get_area_time.tmp"
set TEMP_FILE1 = "/tmp/get_area_time.tmp1"
set TEMP_FILE2 = "/tmp/get_area_time.tmp2"
set FIELD_SIZE = 8.5
#
if ( ! -e $LOG_ALL ) then
   echo "can't find file $LOG_ALL"
   exit
endif
#
if ( ! -e $LOG_LIST ) then
   echo "can't find file $LOG_LIST"
   exit
endif
#
set obsdate = $argv[1]
set d = $QUESTDATA/$obsdate
if ( ! -e $d ) then
  echo "can't find directory $d"
  exit
endif
#
set log = $d/logs/log.obs

if ( ! -e $log ) then
   echo "can't find file $log"
   exit
endif
#
#
cd $d
set n = `grep $obsdate $LOG_LIST | wc -l`
if ( $n == 0 ) then
   echo $obsdate >> $LOG_LIST
   cat $log >> $LOG_ALL
endif
  
# SNE fields only
grep -e " s 2 " $LOG_ALL | grep -e " sky " | grep -e " 60.0 " > $TEMP_FILE
s 7 $TEMP_FILE $TEMP_FILE1
set l = `head -n 1 $log | tail -n 1`
set jd = $l[7]
~/scheduler/bin/get_time_history  $TEMP_FILE1 $jd >! $TEMP_FILE2
set l = `cat $TEMP_FILE2 | grep -e " fields"`
set n_total = $l[2]
set l = `cat $TEMP_FILE2 | grep -e "3-day interval"`
set n_3day = $l[2]
set l = `cat $TEMP_FILE2 | grep -e "interval-sum"`
set n_sum = $l[2]
set area_time = `echo "scale=6;$n_sum * $FIELD_SIZE" | bc`
@ n_1day = $n_total - $n_3day
printf "\n"
printf "completed SNE fields only:\n"
printf  "    %d interval <= 3 days\n" $n_1day
printf  "    %d interval  > 3 days\n" $n_3day
printf  "    %7.3f deg-days covered \n" $area_time
#
# TNO + SNE fields only
grep -e " s 2 " $LOG_ALL | grep -e " sky " > $TEMP_FILE
s 7 $TEMP_FILE $TEMP_FILE1
set l = `head -n 1 $log | tail -n 1`
set jd = $l[7]
~/scheduler/bin/get_time_history  $TEMP_FILE1 $jd >! $TEMP_FILE2
set l = `cat $TEMP_FILE2 | grep -e " fields"`
set n_total = $l[2]
set l = `cat $TEMP_FILE2 | grep -e "3-day interval"`
set n_3day = $l[2]
set l = `cat $TEMP_FILE2 | grep -e "interval-sum"`
set n_sum = $l[2]
set area_time = `echo "scale=6;$n_sum * $FIELD_SIZE" | bc`
@ n_1day = $n_total - $n_3day
printf "\n"
printf "All completed fields:\n"
printf  "    %d interval <= 3 days\n" $n_1day
printf  "    %d interval  > 3 days\n" $n_3day
printf  "    %7.3f deg-days covered \n" $area_time
printf "\n"
#



