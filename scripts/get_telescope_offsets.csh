#!/bin/tcsh
#
# get_telescope_offsets.csh
#
# given the header on the command line of the name of an offset stare,
# find the pointing offsets using chup A14.
#
set CHIP = "A14"
set TELESCOPE_OFFSETS_FILE = "/home/observer/telescope_offsets.dat"
set TELESCOPE_OFFSETS_LOGFILE = "/home/observer/telescope_offsets.log"
set TIME_OUT = 300
#
unalias rm
#
if ( ! -e $TELESCOPE_OFFSETS_FILE ) then
   echo "can't find $TELESCOPE_OFFSETS_FILE"
   exit
endif
#
#set dir =  $QUESTNEATDATADIR/`date -u +"%Y%m%d"`
set dir =  $QUESTNEATDATADIR/`get_ut_date`
if ( $FAKE_OBS ) then
   set dir  = $dir.fake
   set test_dir = /scr1/observer/quest/20070119

endif

if ( ! -e $dir ) then
  echo "can't find $dir" 
  exit
endif

cd $dir
#
#
set N_FILES = $#argv
if ( $N_FILES < 1 ) then
  echo "syntax: get_telescope_offsets.csh $filename"
  exit
endif
set file = $argv[1]
#
echo "get_telescope_offsets.csh $file"
#
# wait for offset image to appear on disk
# Time out after $TIME_OUT seconds
#
echo "waiting for  image  $file"
set n = 0
set t1 = `date +"%s"`
set dt = 0
while ( $n < 1 )
    set t2 = `date +"%s"`
    @ dt = $t2 - $t1
    if ( $dt > $TIME_OUT ) then
       echo "timeout waiting for $argv[$N_FILES]"
       exit
    endif
    set n = `ls "$file"."$CHIP".*fits |& grep -ve "No" | wc -l`
    if ( $n < 1 ) then
       sleep 1 
       echo $dt "still waiting ..."
    endif
end
#
if ( $FAKE_OBS ) then
  set image = "$test_dir/sf20070119.005.A14.000.fits
  echo "~/palomar/scripts/offsets2.csh $image"
else
  set image = "$file"."$CHIP".*fits
  echo "~/palomar/scripts/offsets2.csh $image"
endif
#
set l = `~/palomar/scripts/offsets2.csh $image`
if ( $#l == 2 ) then
   echo $l
   if ( -e $TELESCOPE_OFFSETS_FILE ) then
      echo `date` $l >> $TELESCOPE_OFFSETS_LOGFILE
      rm $TELESCOPE_OFFSETS_FILE
   endif
   echo $l >! $TELESCOPE_OFFSETS_FILE
else
   echo "could not get offsets for image $image"
   echo $l
endif
#
#
