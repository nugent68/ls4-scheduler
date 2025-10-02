/* almanac.c

   compile using "make almanac LDFLAGS=-lm"

   D. Rabinowitz 7-10-03

   This is the skycalc code by John Thorstensen, modified to
   ignore standard input, and to simply print out useful quantities
   for the current date.

   All the information is determined in subroutine "print_tonight".

   The observatory site is La Silla by default (see DEFAULT_OBSCODE).

   The alamanac information is printed for the system time at which 
   the program is run.

   The original output from the almanac output (command "a") is preserved.
   It is printed to standard output with "#" before each line.

   Additional information is printed with headings as in the following:

UT_SUNSET  3.112220
UT_EVENING  4.672969
UT_MIDNIGHT  7.000000
UT_MORNING 11.092281
UT_SUNRISE 12.652994
UT_MOONRISE  0.117441
UT_MOONSET 10.508142
LST_SUNSET 14.562915
LST_EVENING 16.127937
LST_MIDNIGHT 18.461339
LST_MORNING 22.564825
LST_SUNRISE  0.129811
LST_MOONRISE 11.559936
LST_MOONSET 21.979086

"EVENING" and "MORNING" refer to the time when the sun is at elevation
-18 deg. Hence, LST_EVENING is the start of the dark time, and LST_MORNING
is the end of the dark time. Twilight is the time from SUNSET to EVENING
and from MORNING to SUNRISE.

*/



/* SKY CALCULATOR PROGRAM

	John Thorstensen, Dartmouth College.

   This program computes many quantities frequently needed by
   the observational astronomer.  It is written as a completely
   self-contained program in standard c, so it should be
   very transportable; the only issue I know of that really affects
   portability is the adequacy of the double-precision floating
   point accuracy on the machine.  Experience shows that c compilers
   on various systems have idiosyncracies, though, so be sure
   to check carefully.

   This is intended as an observatory utility program; I assume the
   user is familiar with astronomical coordinates and nomenclature.
   While the code should be very transportable, I also
   assume it will be installed by a conscientious person who
   will run critical tests before it is released at a new site.
   Experience shows that some c compilers generate unforseen errors
   when the code is ported, so the output should be checked meticulously
   against data from other sites.

   The first part (the almanac) lists the phenomena for a single night (sunset,
   twilight, moonrise, mooset, etc.) in civil clock time.
   The rise-set and twilight times given are good
   to a minute or two; the moon ephemeris used for rise/set is good to
   +- 0.3 degrees or so; it's from the Astronomical Almanac's
   low precision formulae, (with topocentric corrections included).
   The resulting moon rise/set times are generally good to better than
   two minutes.  The moon coordinates for midnight and in the 'calculator
   mode' are from a more accurate routine and are generally better than
   1 arcmin.  The elevation of an observatory above its effective
   horizon can be specified; if it is non-zero, rise/set times are
   corrected approximately for depression of the horizon.

   After displaying the phenomena for one night, the program goes
   into a 'calculator mode', in which one can -

	- enter RA, dec, proper motion, epoch, date, time,
	     new site parameters, etc. ...

	- compute and display circumstances of observation for the
	   current parameters, including precessed coordinates,
	   airmass, interference from moon or twilight, parallactic
	   angle, etc; the program also gives calendar date in
	   both UT and local, Julian date, and barycentric corrections.

	- compute and display a table of airmasses (etc) at
	   hourly intervals through the night.  This is very useful
	   at the telescope.  Also, if one has a modest number of
	   objects, it may be convenient (using system utilities)
	   to redirect the output and print a hard copy of these
	   tables for ready reference.

	- compute and display galactic and ecliptic coordinates.

	- compute and display rough (of order 0.1 degree, but often
	  much better) positions of the major planets.

	- display the almanac for the current night.

    The program is self-contained.  It was developed on a VMS system,
   but should adapt easily to any system with a c compiler.  It has
   been ported to, and tested on, several popular workstations.

	** BUT CAUTION ... **
   Because many of the routines take a double-precision floating point
   Julian Date as their time argument, one must be sure that the machine
   and compiler carry sufficient mantissa to reach the desired accuracy.
   On VAX/VMS, the time resolution is of order 0.01 second.  This has also
   proven true on Sun and IBM workstations.

LEGALITIES:

   I make no guarantee as to the accuracy, reliability, or
   appropriateness of this program, though I have found it to be
   reasonably accurate and quite useful to the working astronomer.

   The program is COPYRIGHT 1993 BY JOHN THORSTENSEN.
   Permission is hereby granted for non-profit scientific or educational use.
   For-profit use (e. g., by astrologers!) must be through negotiated
   license.  The author requests that observatories and astronomy
   departments which install this as a utility notify the author
   by paper mail, just so I know how widely it is used.

   Credits:
    * The julian date and sidereal time routines were
    originally coded in PL/I by  Steve Maker of Dartmouth College.
    They were based on routines in the old American Ephemeris.
    * The conversion from julian date to calendar date is adapted
    from Numerical Recipes in c, by Press et al. (Cambridge University
    Press). I highly recommend this excellent, very useful book.


    APOLOGIES/DISCLAIMER:
    I am aware that the code here does not always conform to
    the best programming practices.  Not every possible error condition
    is anticipated, and no guarantee is given that this is bug-free.
    Nonetheless, most of this code has been shaken down at several
    hundred sites for several years, and I have never received any
    actual bug reports.  Many users have found this program
    to be useful.

    CHANGES SINCE THE ORIGINAL DISTRIBUTION ....

	The program as listed here is for the most part similar to that
	posted on the IRAF bulletin board in 1990.  Some changes
	include:

	01 In the original code, many functions returned structures, which
	   some c implementations do not like.  These have been eliminated.

	02 The original main() was extremely cumbersome; much of it has
	   been broken into smaller (but still large) functions.

	03 The hourly airmass includes a column for the altitude of the
	   sun, which is printed if it is greater than -18 degrees.

	04 The planets are included (see above).  As part of this, the
	   circumstances calculator issues a warning when one is within
	   three degrees of a major planet.  This warning is now also
	   included in the hourly-airmass table.

	05 The changeover from standard to daylight time has been rationalized.
	   Input times between 2 and 3 AM on the night when DST starts (which
	   are skipped over and  hence don't exist) are now disallowed; input
	   times between 1 and 2 AM on the night when DST ends (which are
	   ambiguous) are interpreted as standard times.  Warnings are printed
	   in both the almanac and calculator mode when one is near to the
	   changeover.

	06 a much more accurate moon calculation has been added; it's used
	   when the moon's coordinates are given explicitly, but not for
	   the rise/set times, which iterate and for which a lower precision
	   is adequate.

	07 It's possible now to set the observatory elevation; in a second
	   revision there are now two separate elevation parameters specified.
	   The elevation above the horizon used only in rise/set calculations
	   and adjusts rise/set times assuming the parameter is the elevation
	   above flat surroundings (e. g., an ocean).  The true elevation above
	   sea level is used (together with an ellipsoidal earth figure) in
	   determining the observatory's geocentric coordinates for use in
	   the topocentric correction of the moon's position and in the
	   calculation of the diurnal rotation part of the barycentric velocity
	   correction.  These refinements are quite small.

	08 The moon's altitude above the horizon is now printed in the
	   hourly airmass calculation; in the header line, its illuminated
	   fraction and angular separation from the object are included,
	   as computed for local midnight.

	09 The helio/barycentric corrections have been revised and improved.
	   The same routines used for planetary positions are used to
	   compute the offset from heliocentric to solar-system
	   barycentric positions and velocities.  The earth's position
	   (and the sun's position as well) have been improved somewhat
	   as well.

	10 The printed day and date are always based on the same truncation
	   of the julian date argument, so they should now always agree
	   arbitrarily close to midnight.

	11 A new convention has been adopted by which the default is that the
	   date specified is the evening date for the whole night.  This way,
	   calculating an almanac for the night of July 3/4 and then specifying
	   a time after midnight gives the circumstances for the *morning of
	   July 4*.  Typing 'n' toggles between this interpretation and a
	   literal interpretation of the date.

	12 The planetary proximity warning is now included in the hourly airmass
	   table.

	13 A routine has been added which calculates how far the moon is from
	   the nearest cardinal phase (to coin a phrase) and prints a
	   description.  This information is now included in both the almanac
	   and the calculator mode.

	14 The output formats have been changed slightly; it's hoped this
	   will enhance comprehensibility.

	15 A substantial revision affecting the user interface took place
	   in September of 1993.  A command 'a' has been added to the
	   'calculator' menu, which simply prints the almanac (rise, set,
	   and so on) for the current night.  I'd always found that it was
	   easy to get disoriented using the '=' command -- too much
	   information about the moment, not enough about the time
	   context.  Making the almanac info *conveniently* available
	   in the calculator mode helps your get oriented.

	   When the 'a' almanac is printed, space is saved over the
	   almanac printed on entry, because there does not need
	   to be a banner introducing the calculator mode.  Therefore some
	   extra information is included with the 'a' almanac; this includes
	   the length of the night from sunset to sunrise, the number of
	   hours the sun is below -18 degrees altitude, and the number of hours
	   moon is down after twilight.  In addition, moonrise and moonset
	   are printed in the order in which they occur, and the occasional
	   non-convergence of the rise/set algorithms at high latitude are
	   signalled more forcefully to the user.

	16 I found this 'a' command to be convenient in practice, and never
	   liked the previous structure of having to 'quit' the calculator
	   mode to see almanac information for a different night.  D'Anne
	   Thompson of NOAO also pointed out how hokey this was, especially the
	   use of a negative date to exit. So, I simply removed the outer
	   'almanac' loop and added a 'Q' to the main menu for 'quit'.  The
	   use of upper case -- for this one command only --  should guard
	   against accidental exit.

	17 The menu has been revised to be a little more readable.

	18 More error checking was added in Nov. 1993, especially for numbers.
	   If the user gives an illegal entry (such as a number which isn't
	   legal), the rest of the command line is thrown away (to avoid
	   having scanf simply chomp through it) and the user is prompted
	   as to what to do next.  This seems to have stopped all situations
	   in which execution could run away.  Also, typing repeated carriage
	   returns with nothing on the line -- which a frustrated novice
	   user may do because of the lack of any prompts -- causes a
	   little notice to be printed to draw attention to the help and menu
	   texts.

	19 I found in practice that, although the input parameters and
	   conditions are deducible *in principle* from such things as the
	   'a' and '=' output, it takes too much digging to find them.  So
	   I instituted an 'l' command to 'look' at the current parameter
	   values.  To make room for this I put the 'Cautions and legalities'
	   into the 'w' (inner workings) help text.  This looks as though
	   it will be be very helpful to the user.

	20 The more accurate moon calculation is used for moonrise and
	   moonset; the execution time penalty appears to be trivial.
	   Low precision moon is still used for the summary moon information
	   printed along with the hourly airmass table.

	21 A calculation of the expected portion of the night-sky
	   brightness due to moonlight has been added.  This is based on
	   Krisciunas and Schaefer's analytic fits (PASP, 1991).  Obviously,
	   it's only an estimate which will vary considerably depending on
	   atmospheric conditions.

	22 A very crude estimator of the zenith sky brightness in twilight
	   has been added.

	23 A topocentric correction has been added for the sun, in anticipation
	   of adding eclipse prediction.

	24 The code now checks for eclipses of the sun and moon, by making
	   very direct use of the predicted positions.  If an eclipse is
	   predicted, a notice is printed in print_circumstances; also, a
	   disclaimer is printed for the lunar sky brightness if a lunar
	   eclipse is predicted to be under way.

	25 In the driver of the main calculator loop, a provision has been
	   added for getting characters out of a buffer rather than reading
	   them directly off the command line.  This allows one to type any
	   valid command character (except Q for quit) directly after a number
	   in an argument without generating a complaint from the program
	   (see note 18).  This had been an annoying flaw.

	26 In 1993 December/1994 January, the code was transplanted
	   to a PC and compiled under Borland Turbo C++, with strict
	   ANSI rules.  The code was cut into 9 parts -- 8 subroutine
	   files, the main program, and an incude file containing
	   global variables and function prototypes.

	27 An "extra goodies" feature has been added -- at present it
	   computes geocentric times of a repeating phenomenon as
	   viewed from a site.  This can be used for relatively little-used
           commands to save space on the main menu.

	28 The sun and moon are now included in the "major planets"
	   printout.  This allows one to find their celestial positions
	   even when they are not visible from the selected site.

	29 A MAJOR new feature was added in February 1994, which computes
           the observability of an object at new and full moon over a
           range of dates.  The galactic/ecliptic coordinate converter
           was moved to the extra goodies menu to make room for this.

	30 Inclusion of a season-long timescale means that it's not
           always necessary to specify a date on entry to the program.
           Accordingly, the program immediately starts up in what used
           to be called "calculator" mode -- only the site is prompted
           for.  It is thought that the site will be relevant to nearly
           all users.

	31 Because the user is not led by the hand as much as before, the
           startup messages were completely revised to direct new users
           toward a short `guided tour' designed to show the program's
	   command structure and capabilities very quickly.  Tests on
	   volunteers showed that users instinctively go for anything
	   called the `menu', despite the fact that that's a slow way to
	   learn, so all mention of the menu option is removed from the
	   startup sequence; they'll catch on soon enough.

	32 Code has been added to automatically set the time and
           date to the present on startup.  A menu option 'T' has been
           added to set the time and date to the present plus a settable
           offset.  This should be very useful while observing.

	33 Because Sun machines apparently do not understand ANSI-standard
           function declarations, the code has been revised back to K&R
           style.  It's also been put back in a monolithic block for
           simplicity in distribution.

	34 The startup has been simplified still further, in that the
           coordinates are set automatically to the zenith on startup.
	   An 'xZ' menu item sets to the zenith for the currently specified
           time and date (not necessarily the real time and date.)

	35 Another MAJOR new capability was added in early 1994 --
           the ability to read in a list of objects and set the current
	   coordinates to an object on the list.  The list can be sorted
           in a number of ways using information about the site, date
           and time.

	35 Calculator-like commands were added to the extra goodies menu
           to do precessions and to convert Julian dates to calendar
           dates.  An option to set the date and time to correspond to
           a given julian date was also added.

	36 Another substantial new capability was added Aug 94 -- one can
           toggle open a log file (always named "skyclg") and keep
           a record of the output.  This is done simply by replacing
           most occurrences of "printf" with "oprintf", which mimics
           printf but writes to a log file as well if it is open.
	   This appears to slow down execution somewhat.

	37 12-degree twilight has been added to the almanac.  While the
	   awkward "goto" statements have been retained, the statement
           labels have been revised to make them a little clearer.
*/

#include <stdio.h>
#include <math.h>
#include <ctype.h>
#include <stdarg.h>
#include <string.h>
#include "sky_utils.h"

int main(int argc, char *argv[])

{
	struct date_time date;
	double jdb, jde;  /* jd of begin and end of dst */
	double objra=0., objdec=0., objepoch=1950.;
	short enter_ut = 0; /* are times to be entered as UT? */
	short night_date = 0; /* interperet current date as evening or true? */

	/* all the site-specific quantities are here:
		longit     = W longitude in decimal hours
		lat        = N latitude in decimal degrees
		stdz       = standard time zone offset, hours
		use_dst    = 1 for USA DST, 2 for Spanish, negative for south,
				 0 to use standard time year round
		zone_name  = name of time zone, e. g. Eastern
		zabr       = single-character abbreviation of time zone
		site_name  = name of site.
	*/


	char site_name[45];  /* initialized later with strcpy for portability */
	char zabr = 'M';
	char zone_name[25]; /* this too */
	short use_dst = 0;
	double longit = 7.44111;
	double elevsea = 1925.;  /* for MDM, strictly */
	double elev = 500.; /* well, sorta -- height above horizon */
	double horiz = 0.7174;
	double lat = 31.9533;
	double stdz = 7.;


        strcpy(site_name,"DEFAULT");

	load_site(&longit,&lat,&stdz,&use_dst,zone_name,&zabr,
			&elevsea,&elev,&horiz,site_name);

#if SYS_CLOCK_OK == 1

        if(get_sys_date(&date,use_dst,enter_ut,night_date,stdz,0.) != 0) {
	  date.y = 2000;  /* have to have a default date.*/
	  date.mo = 1;
	  date.d = 1;
	  date.h = 0.;
	  date.mn = 0.;
	  date.s = 0.;
	  oprntf("#SYSTEM CLOCK didn't read. Time & date set arbitrarily to\n");
	  print_all(date_to_jd(date));
	  oprntf("\n");
        }

        else set_zenith(date, use_dst, enter_ut, night_date, stdz, lat,
	  longit, objepoch, &objra, &objdec);

#else
       	  date.y = 2000;  /* have to have a default date.*/
	  date.mo = 1;
	  date.d = 1;
	  date.h = 0.;
	  date.mn = 0.;
	  date.s = 0.;
	  oprntf("#SYSTEM CLOCK options turned off, so \n ");
	  oprntf("#time and date set arbitrarily to:\n");
	  print_all(date_to_jd(date));
	  oprntf("\n#\n");
#endif


	print_tonight(date,lat,longit,elevsea,elev,horiz,site_name,stdz,
			   zone_name,zabr,use_dst,&jdb,&jde,2,NULL,1);

	exit(0);
}
