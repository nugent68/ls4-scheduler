#!/bin/csh
#
grep -e " sky " log.obs > temp
set n = `cat temp | wc -l`
set i = 1
while ( $i <= $n )
  set l = `head -n $i temp | tail -n 1`
  set name = $l[9]
  set year = `echo $name | cut -c 1-4`
  set month = `echo $name | cut -c 5-6`
  set day = `echo $name | cut -c 7-8`
  set expt = $l[5]
  set ut = $l[7]
  set ha = $l[6]
  set ut = `echo "scale=6; $ut - 0.5" | bc`
  set ut0 = `echo $ut | cut -c 1-7`
  set ut = `echo "scale=6; 24.0 * ($ut - $ut0 ) " | bc`
  set ra = $l[1]
  set dec = $l[2]
  set index = $l[4]
  set l = `~/scheduler/bin/get_airmass $ra $dec $ut $year $month $day`
  set am = $l[2]
  set l1 = `grep -e "$name" qa.log`
  if ( $#l1 >= 0 ) then
    echo $ra $dec $ha $am $index $expt $ut $l1
  endif
  @ i = $i + 1
end
#
