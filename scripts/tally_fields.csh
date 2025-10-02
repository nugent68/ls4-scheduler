#!/bin/csh
#
# tally_fields.csh
#
# Read through log file from scheduler and determine:
# Total number of darks, flats, offsets fields, TNO, SNE, MUST-DO, and None fields and
# fraction of the time taken by each type
#
#
set TEMP_FILE = "/tmp/tally_fields.tmp"
set TEMP_FILE1 = "/tmp/tally_fields.tmp1"
set TEMP_PLAN = "/tmp/tall_fields.tmp2"
set UT_INDEX = 3
set INDEX_INDEX = 9
#set INDEX_INDEX = 7 
set MAX_INTERVAL = 0.16667
#
if ( $#argv != 1 ) then
   echo "syntax: tally_fields.csh yyyymmdd"
   exit
endif
#
set d = $argv[1]
set log = $d.log
set obs_plan = $d.obsplan
if ( -e $TEMP_PLAN) rm $TEMP_PLAN
# strip blank lines from obs_plan 
set n = `cat -n $obs_plan | wc -l`
set i = 1
while ( $i <= $n )
  set l = `head -n $i $obs_plan | tail -n 1`
#echo " set l = head -n $i $obs_plan | tail -n 1"
#printf "$l\n"
  if ( $#l > 0 ) then
    printf "$l\n" >> $TEMP_PLAN
  endif
  @ i = $i + 1
end
#
#
if ( ! -e  $log ) then
   echo "can't find $log"
   exit
endif
#
#
# Get length of night, total dead time, and start and end times of darkness
#
set l = `grep -e "Starting observations" $log`
set ut_start = $l[3]
set l = `/home/observer/palomar/scripts/get_dead_time.csh $log`
set dead_time = $l[1]
set night_length  = $l[2]
set ut12_start  = $l[3]
set ut12_end  = $l[4]
#
#
#
# Get number of fields planned and completed for each
# survey code (0 to other, 1 to TNO, 2 for SN, 3 for MUST-DO)
#
set f_night_dark = `grep -e " N " $TEMP_PLAN | grep -ve "1.0 " | wc -l`
set f_pm_dark = `grep -e " N " $TEMP_PLAN | grep -e "-1.0 " | wc -l`
set f_am_dark = `grep -e " N " $TEMP_PLAN | grep -e " 1.0 " | grep -ve " -1.0" | wc -l`
set f_pm_flat = `grep -e " E " $TEMP_PLAN | wc -l`
set f_am_flat = `grep -e " M " $TEMP_PLAN |  wc -l`
set f_other = `grep -e " 0 #" $TEMP_PLAN | wc -l`
set f_focus = `grep -e " F " $TEMP_PLAN | wc -l`
set f_offset = `grep -e " P " $TEMP_PLAN | wc -l`
set f_tno = `grep -e " 1 #" $TEMP_PLAN | wc -l`
set f_sne = `grep -e " 2 #" $TEMP_PLAN | wc -l`
set f_must_do = `grep -e " 3 #" $TEMP_PLAN | wc -l`
@ f_sky = $f_tno + $f_sne + $f_must_do + $f_other + $f_focus + $f_offset
#
set f_night_dark_done = `grep -e " N " fields.completed | grep -ve "1.0 " | wc -l`
set f_pm_dark_done = `grep -e " N " fields.completed | grep -e " -1.0 " | wc -l`
set f_am_dark_done = `grep -e " N " fields.completed | grep -e " 1.0 " | grep -ve " -1.0" | wc -l`
set f_pm_flat_done = `grep -e " E " fields.completed | wc -l`
set f_am_flat_done = `grep -e " M " fields.completed |  wc -l`
set f_other_done = `grep -e " 0 #" fields.completed | wc -l`
set f_focus_done = `grep -e " F " fields.completed | wc -l`
set f_offset_done = `grep -e " P " fields.completed | wc -l`
set f_tno_done = `grep -e " 1 #" fields.completed | wc -l`
set f_sne_done = `grep -e " 2 #" fields.completed | wc -l`
set f_must_do_done = `grep -e " 3 #" fields.completed | wc -l`
@ f_sky_done = $f_tno_done + $f_sne_done + $f_must_do_done + $f_other_done + $f_focus_done + $f_offset_done

# get relevant lines from log file, and sort by UT
#
grep -e "Exposed" $log | grep -e "UT" > $TEMP_FILE
grep -e "No fields ready to observe" $log | grep -e "UT" | cut -c 2-100 >> $TEMP_FILE
csh /home/observer/csh.dir/sort.csh  3 $TEMP_FILE $TEMP_FILE1
#
# Go through each line and tally number of observations and cumulative time
# for focus, offset, dark, and sky field
#
set n_sky = 0
set sky_time = 0.0
set n_night_dark = 0
set n_night_dark1 = 0
# n_night_dark1 and night_dark_time1 refer to darks between 12-deg twilights timies
set night_dark_time = 0.0
set night_dark_time1 = 0.0
set n_pm_dark = 0
set pm_dark_time = 0.0
set n_am_dark = 0
set am_dark_time = 0.0
set n_pm_flat = 0
set pm_flat_time = 0.0
set n_am_flat = 0
set am_flat_time = 0.0
set n_offset = 0
set offset_time = 0.0
set n_focus = 0
set focus_time = 0.0
set n_other = 0
set n_tno = 0
set n_sne = 0
set n_must_do = 0
set other_time = 0.0
set tno_time = 0.0
set sne_time = 0.0
set must_do_time = 0.0
#
set n = `cat $TEMP_FILE1 | wc -l`
set i  = 1
set ut_prev = $ut_start
if ( -e $TEMP_FILE ) rm $TEMP_FILE
grep -v "Palomar" $TEMP_PLAN > $TEMP_FILE
while ( $i <= $n )
    set l = `head -n $i $TEMP_FILE1 | tail -n 1`
#echo " set l = head -n $i $TEMP_FILE1 | tail -n 1"
#echo $l
    set ut = $l[$UT_INDEX]
    set inside = `echo "scale=6; if ( $ut > $ut12_start && $ut < $ut12_end ) 1 else 0" | bc`
# if ( $inside ) then
    set dt = `echo "scale=6; $ut - $ut_prev " | bc`
    if ( `echo "scale=6; if ( $dt > $MAX_INTERVAL ) 1 else 0" | bc` ) then
       set dt = 0.0
    endif
    set ut_prev = $ut
#    set n1 = `echo "$l" | grep -e " sky" | wc -l`
    set n1 = `echo "$l" | grep -ve " No" | wc -l`
    if ( $n1 == 1 ) then
       set index = $l[$INDEX_INDEX]
       @ index = $index + 1
#echo " set l1 = head -n $index $TEMP_FILE | tail -n 1"
       set l1 = `head -n $index $TEMP_FILE | tail -n 1`
#echo $l1
       set survey_code = $l1[7]
       set shutter_code = $l1[3]
       set dec = $l1[2]
       if ( $shutter_code == "N" || $shutter_code == "n" ) then
          if ( `echo "scale=6; if ( $dec < 0 ) 1 else 0" | bc` ) then
               @ n_pm_dark = $n_pm_dark + 1
               set pm_dark_time = `echo "scale=6; $pm_dark_time +  $dt" | bc`
          else if ( `echo "scale=6; if ( $dec == 0 ) 1 else 0" | bc` ) then
               @ n_night_dark = $n_night_dark + 1
               set night_dark_time = `echo "scale=6; $night_dark_time +  $dt" | bc`
               if( $inside ) then
                 @ n_night_dark1 = $n_night_dark1 + 1
                 set night_dark_time1 = `echo "scale=6; $night_dark_time1 +  $dt" | bc`
               endif
          else if ( `echo "scale=6; if ( $dec > 0 )  1 else 0" | bc` ) then
               @ n_am_dark = $n_am_dark + 1
               set am_dark_time = `echo "scale=6; $am_dark_time +  $dt" | bc`
          endif
       else if ( $shutter_code == "P" || $shutter_code == "p" ) then
          @ n_offset = $n_offset + 1
          set offset_time = `echo "scale=6; $offset_time +  $dt" | bc`
          @ n_sky = $n_sky + 1
          set sky_time = `echo "scale=6; $sky_time +  $dt" | bc`
       else if ( $shutter_code == "F" || $shutter_code == "f" ) then
          @ n_focus = $n_focus + 1
          set focus_time = `echo "scale=6; $focus_time +  $dt" | bc`
          @ n_sky = $n_sky + 1
          set sky_time = `echo "scale=6; $sky_time +  $dt" | bc`
       else if ( $shutter_code == "E" ) then
         @ n_pm_flat = $n_pm_flat + 1
         set pm_flat_time = `echo "scale=6; $pm_flat_time +  $dt" | bc`
       else if ( $shutter_code == "M" ) then
         @ n_am_flat = $n_am_flat + 1
         set am_flat_time = `echo "scale=6; $am_flat_time +  $dt" | bc`
       else if ( $survey_code == 0 ) then
         @ n_other = $n_other + 1
         set other_time = `echo "scale=6; $other_time +  $dt" | bc`
         @ n_sky = $n_sky + 1
         set sky_time = `echo "scale=6; $sky_time +  $dt" | bc`
       else if ( $survey_code == 1 ) then
         @ n_tno = $n_tno + 1
         set tno_time = `echo "scale=6; $tno_time +  $dt" | bc`
         @ n_sky = $n_sky + 1
         set sky_time = `echo "scale=6; $sky_time +  $dt" | bc`
       else if ( $survey_code == 2 ) then
         @ n_sne = $n_sne + 1
         set sne_time = `echo "scale=6; $sne_time +  $dt" | bc`
         @ n_sky = $n_sky + 1
         set sky_time = `echo "scale=6; $sky_time +  $dt" | bc`
       else if ( $survey_code == 3 ) then
         @ n_must_do = $n_must_do + 1
         set must_do_time = `echo "scale=6; $must_do_time +  $dt" | bc`
         @ n_sky = $n_sky + 1
         set sky_time = `echo "scale=6; $sky_time +  $dt" | bc`
       endif
    endif
#
    @ i = $i + 1
end
#
#
printf "\n"
#
# get dome open and close times.
#
grep -e "UT" $log | grep -e "dome" > $TEMP_FILE
set n = `cat -n $TEMP_FILE | wc -l`
set l = `grep -e "open" $TEMP_FILE | wc -l`
#
# If dome never opened, report the information and exit
#
if ( $n == 0 ) then
   echo "No obs. Dome never opened"
   set dome_open_time = 0.0
else if ( $#l == 0 )then
   echo "No obs. Dome never opened"
   set dome_open_time = 0.0
else 
#
# Get the total time the dome was opened
#
set i = 1
set ut_open = -1
set ut_closed = -1
set dome_state_prev = "unknown"
set dome_open_time = 0.0
while ( $i <= $n )
  set l = `head -n $i $TEMP_FILE | tail -n 1`
  set ut = $l[3]
  set dome_state = $l[15]
#
  if ( $i == 1 && $dome_state != "open" ) then
     set ut_closed = $ut
     set dome_state = "closed"
     echo "Dome closed : $ut_closed  Start of night"
#
  else if ( $i == 1 && $dome_state == "open" ) then
     set ut_open = $ut
     echo "Dome opened : $ut_open    Start of night" 
#
# dome was previously closed and now is open. Record the opening time
#
  else if ( $dome_state != $dome_state_prev && $dome_state == "open" ) then
     set ut_open = $ut
     echo "Dome opened : $ut_open" 
#
# Dome is no longer open but was previously. Record the closing time.
# Also add the time since last opening to the cumulative total.
#
  else if ( $dome_state_prev == "open"  && $dome_state != "open" ) then
     set ut_closed = $ut
     set dome_state = "closed"
     echo "Dome closed : $ut_closed"
     set dome_open_time = `echo "scale=6; $dome_open_time + $ut_closed - $ut_open " | bc`
#
# This is the last line in the file and the dome state is open. 
# Assume the dome closed. Add the time since last opening to 
# the cumulative total.
#
  else if ( $i == $n && $dome_state == "open" ) then
     set dome_state = "closed"
     set ut_closed = $ut
     echo "Dome closed : $ut_closed  End of night"
     if ( $dome_state_prev == "open" ) then
        set dome_open_time = `echo "scale=6; $dome_open_time + $ut_closed - $ut_open " | bc`
     endif
  endif
#
# Keep going through the file
#
  set dome_state_prev = $dome_state
  @ i = $i + 1
end
endif
#
#
# compute night fraction for each survey code
#
set fraction_dark = `echo "scale=6; $night_dark_time / ( $night_length - $dead_time)" | bc`
set fraction_dark1 = `echo "scale=6; $night_dark_time1 / ( $night_length - $dead_time)" | bc`
set fraction_focus = `echo "scale=6; $focus_time / ( $night_length - $dead_time)" | bc`
set fraction_offset = `echo "scale=6; $offset_time / ( $night_length - $dead_time)" | bc`
set fraction_sky = `echo "scale=6; $sky_time /( $night_length - $dead_time)" | bc`
set fraction_tno = `echo "scale=6; $tno_time /( $night_length - $dead_time)" | bc`
set fraction_sne = `echo "scale=6; $sne_time /( $night_length - $dead_time)" | bc`
set fraction_must_do = `echo "scale=6; $must_do_time /( $night_length - $dead_time)" | bc`
set fraction_other = `echo "scale=6; $other_time /( $night_length - $dead_time)" | bc`
#
echo " "
echo   "    UT start: $ut12_start hours"
echo   "      UT end: $ut12_end hours"
echo   "   dead time: $dead_time hours"
echo   "night length: $night_length hours"
printf "   dome open: %5.3f hours\n" $dome_open_time
printf "\n"
printf "_______________________________________________________________\n"
printf "Field      Number     Number    Number     Time       Night\n"
printf "Type       Planned   Completed  Exposures  Used (h)   Fraction\n"
printf "_______________________________________________________________\n"
printf "PM Dark     %03d       %03d        %03d       %05.3f             \n" $f_pm_dark $f_pm_dark_done $n_pm_dark $pm_dark_time 
printf "AM Dark     %03d       %03d        %03d       %05.3f             \n" $f_am_dark $f_am_dark_done $n_am_dark $am_dark_time 
printf "PM Flat     %03d       %03d        %03d       %05.3f             \n" $f_pm_flat $f_pm_flat_done $n_pm_flat $pm_flat_time 
printf "AM Flat     %03d       %03d        %03d       %05.3f             \n" $f_am_flat $f_am_flat_done $n_am_flat $am_flat_time 
printf "Night Dark  %03d       %03d        %03d       %05.3f      %0.2f\n" $f_night_dark $f_night_dark_done $n_night_dark $night_dark_time1 $fraction_dark1
printf "Night Sky   %03d       %03d        %03d       %05.3f      %0.2f\n" $f_sky $f_sky_done $n_sky $sky_time $fraction_sky

printf "\n"
printf "sky breakdown:\n"
printf "      TNO   %03d       %03d        %03d       %05.3f      %0.2f\n" $f_tno $f_tno_done $n_tno $tno_time $fraction_tno
printf "      SNE   %03d       %03d        %03d       %05.3f      %0.2f\n" $f_sne $f_sne_done $n_sne $sne_time $fraction_sne
printf "  MUST-DO   %03d       %03d        %03d       %05.3f      %0.2f\n" $f_must_do $f_must_do_done $n_must_do $must_do_time $fraction_must_do
printf "    Focus   %03d       %03d        %03d       %05.3f      %0.2f\n" $f_focus $f_focus_done $n_focus $focus_time $fraction_focus
printf "   Offset   %03d       %03d        %03d       %05.3f      %0.2f\n" $f_offset $f_offset_done $n_offset $offset_time $fraction_offset
printf "    Other   %03d       %03d        %03d       %05.3f      %0.2f\n" $f_other $f_other_done $n_other $other_time $fraction_other
printf "_______________________________________________________________\n"
printf "\n"
#
