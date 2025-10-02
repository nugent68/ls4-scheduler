#!/bin/tcsh
#
# get_best_focus.csh
#
# given the headers on the command lines of a series of focus stares,
# find the focus of the A14, B14, and C14 chips. Take the median
# value as the best focus
#
unalias rm
set CHIP1 = "A14"
set CHIP2 = "B14"
set CHIP3 = "C14"
set OUTPUT = "/tmp/best_focus.tmp"
set TEMP_FILE = "/tmp/best_focus.tmp1"
set TIME_OUT = 300
#
if ( $FAKE_OBS ) then
   set dir =  $QUESTNEATDATADIR/`date +"%Y%m%d"`
   set dir  = $dir.fake
   set test_dir = /scr1/observer/quest/20070119
else
   set dir =  $QUESTNEATDATADIR/`get_ut_date`
endif

if ( ! -e $dir ) then
  echo "can't find $dir" >> $OUTPUT
  exit
endif
cd $dir
#
if ( -e $OUTPUT ) rm $OUTPUT
if ( -e $TEMP_FILE ) rm $TEMP_FILE
touch $OUTPUT
#
set N_FILES = $#argv
if ( $N_FILES < 5 ) then
  echo "not enough focus images: $N_FILES"  >> $OUTPUT
  exit
endif
#
echo "get_best_focus.csh $argv"
#
# wait for final focus image to appear on disk
# Time out after $TIME_OUT seconds
#
echo "waiting for final image  $argv[$N_FILES]" 
set n = 0
set t1 = `date +"%s"`
set dt = 0
while ( $n < 3 )
    set t2 = `date +"%s"`
    @ dt = $t2 - $t1
    if ( $dt > $TIME_OUT ) then
       echo "timeout waiting for $argv[$N_FILES]"
       exit
    endif
    set n1 = `ls "$argv[$N_FILES]"."$CHIP1".*fits |& grep -ve "No" | wc -l`
    set n2 = `ls "$argv[$N_FILES]"."$CHIP2".*fits |& grep -ve "No" | wc -l`
    set n3 = `ls "$argv[$N_FILES]"."$CHIP3".*fits |& grep -ve "No" | wc -l`
    @ n = $n1 + $n2 + $n3
    if ( $n < 3 ) then
       sleep 1 
       echo $dt "still waiting ..."
    endif
end
#
if ( ! $FAKE_OBS ) then
set i = 1
set files = ""
while ( $i <= $N_FILES )
    set file = "$argv[$i]"."$CHIP1".*fits
    set files = "$files $file"
    @ i = $i + 1
end
else
   set files = $test_dir/*"$CHIP1".*fits
endif
echo "focus $files"
focus $files >& $TEMP_FILE
cat $TEMP_FILE
cat $TEMP_FILE | grep -e "best focus" >> $OUTPUT
#echo "focus $files | grep -e "best focus" >> $OUTPUT"
#focus $files | grep -e "best focus" >> $OUTPUT
#
set i = 1
if ( ! $FAKE_OBS ) then
set i = 1
set files = ""
while ( $i <= $N_FILES )
    set file = "$argv[$i]"."$CHIP2".*fits
    set files = "$files $file"
    @ i = $i + 1
end
echo "focus $files | grep -e "best focus" >> $OUTPUT"
else
   set files = $test_dir/*"$CHIP2".*fits
endif
focus $files >& $TEMP_FILE
cat $TEMP_FILE
cat $TEMP_FILE | grep -e "best focus" >> $OUTPUT
#echo "focus $files | grep -e "best focus" >> $OUTPUT"
#focus $files | grep -e "best focus" >> $OUTPUT
#
set i = 1
if ( ! $FAKE_OBS ) then
set i = 1
set files = ""
while ( $i <= $N_FILES )
    set file = "$argv[$i]"."$CHIP3".*fits
    set files = "$files $file"
    @ i = $i + 1
end
echo "focus $files | grep -e "best focus" >> $OUTPUT"
else
   set files = $test_dir/*"$CHIP3".*fits
endif
focus $files >& $TEMP_FILE
cat $TEMP_FILE
cat $TEMP_FILE | grep -e "best focus" >> $OUTPUT
#echo "focus $files | grep -e "best focus" >> $OUTPUT"
#focus $files | grep -e "best focus" >> $OUTPUT
#
