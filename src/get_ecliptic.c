#include <stdio.h>

main()
{
    double ra,dec,lon,lat;

    for(lon=0;lon<=360.0;lon=lon+1){
        ecliptic_to_equator(lon,0.0,&ra,&dec);
	printf("%7.3f %7.3f %7.3f\n",lon,ra/15.0,dec);
    }
    exit(0);
}
