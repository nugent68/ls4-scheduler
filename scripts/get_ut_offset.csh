set l1 = `almanac | grep -e "Sunset"`
set sunset_h = $l1[6]
set sunset_m = $l1[7]
set l2 = `date  +"%H %M"`
set h = $l2[1]
set m = $l2[2]
echo "Sunset at $sunset_h $sunset_m"
echo "Currently $h $m"
set ut_offset = `echo "scale=6;$sunset_h - $h + (($sunset_m - $m)/60.0)" | bc`
echo "offset = $ut_offset"

