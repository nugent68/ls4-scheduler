#!/bin/tcsh  -f

# Make daily report file

# launch watchdog program to kill read_camera and write_fits if they are
# still running five minutes later. This prevents this script from hanging
# if the camera power is out.
#
#daily_checkout_watchdog &


set RECIPIENTS = david.rabinowitz@yale.edu
set TODAY = `getdate -e`
set LOGFILE = ${TODAY}_system_check.log
#
# filter log is a file updated at 3 PM local time by a cronjob 
# running on asok. After the dome opens, the filter may
# be queried by a sending a filter command to questctl
#
#set FILTER_LOG =  "/neat/obsdata/logs/filter.log"
#set FILTER_LOG =  $FILTERLOG

date > $LOGFILE
#echo " " >> $LOGFILE

# Get filter
#echo "getting filter" >> $LOGFILE
#tail -1 $FILTER_LOG >> $LOGFILE
#

# Check Disk Space

set DISK = /dev/nvme0n1p1
echo "Checking disk space:" >> $LOGFILE
set SPACE = `df $DISK | awk '{}END{print $4/1000000}'`
echo "   There are $SPACE Gb available on $DISK" >> $LOGFILE



# Check Pressure

echo "Checking pressure:" >> $LOGFILE
#

# Check Temperatures


# Reset Camera

#echo "Resetting camera:" >> $LOGFILE
#echo "RESET" | camctl $OBSHOST | grep -v socket >> $LOGFILE

# Take a Stare
#echo "Taking a staring dark:" >> $LOGFILE

mail -s "$TODAY system check" $RECIPIENTS < $LOGFILE
mv $LOGFILE $HOME/logs/





