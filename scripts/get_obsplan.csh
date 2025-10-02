#!/bin/tcsh
#
# get_obsplan.csh
#
# use wget to retrieve obsplan from questpt webpage
#
#
if ( $#argv == 1 ) then
  set d = $argv[1]
else
   set d = `get_ut_date`
endif

cd /home/observer/obsplans
if (  ! -e $d ) then
   mkdir $d
endif
cd $d
#
set url = "http://hepwww.physics.yale.edu/quest/quest_data/questpt/$d/$d.obsplan"
wget $url

