# Astronomical Calculation Test Scripts

This repository contains two Python scripts for testing the astronomical calculation functions in `scheduler_astro.py` for the La Silla Observatory in Chile.

## Scripts

### 1. `test_scheduler_astro.py` - Interactive Test Script

An interactive script that prompts the user for a date and calculates sun and moon ephemerides for that date.

**Features:**
- Interactive date input (month, day, year format)
- **Dual time display: Universal Time (UT) and Chilean Local Time (CLT/CLST)**
- Automatic detection of Chilean daylight saving time:
  - CLST (UTC-4) during summer months (October-March)
  - CLT (UTC-3) during winter months (April-September)
- Complete sun calculations including:
  - Sunrise and sunset times
  - All twilight times (civil, nautical, astronomical)
  - Dark sky duration
- Complete moon calculations including:
  - **Moon position at LOCAL MIDNIGHT (more relevant for nighttime observations)**
  - Moon RA, Dec, and Local Sidereal Time at midnight
  - Moon altitude, azimuth, and airmass at local midnight
  - Moon phase and illumination percentage
  - Moonrise and moonset times
  - Moon visibility during dark hours
- Observing conditions summary
- Local Sidereal Time calculations
- Best objects to observe (by RA)

**Usage:**
```bash
python test_scheduler_astro.py
```

When prompted, enter:
- Month (1-12)
- Day (1-31)
- Year (e.g., 2025)

### 2. `test_scheduler_astro_auto.py` - Automated Test Script

An automated test script that runs comprehensive tests on all functions without user input.

**Features:**
- **Dual time display for all rise/set calculations**
- **Moon position calculated at local midnight for all tests**
- Automatic timezone detection (CLT/CLST)
- Tests for specific dates (including today's date)
- Julian Date conversions
- GMST and LST calculations
- Coordinate transformations:
  - Equatorial to Galactic
  - Equatorial to Ecliptic
  - Altitude/Azimuth calculations
- Edge case testing:
  - Circumpolar objects
  - Never-rising objects
  - Airmass calculations at various altitudes
- Seasonal variations (solstices and equinoxes) with moon phase
- Atmospheric refraction corrections
- Practical observing planning with object visibility
- **Moon tracking through the night** - shows moon position every 2 hours

**Usage:**
```bash
python test_scheduler_astro_auto.py
```

## Observatory Location

Both scripts are configured for **La Silla Observatory**:
- **Latitude:** 29°15'24" S (-29.2567°)
- **Longitude:** 70°44'16" W (4.716 hours west)
- **Altitude:** 2400 meters
- **Time Zone:** 
  - CLT (Chile Standard Time, UTC-3) April-September
  - CLST (Chile Summer Time, UTC-4) October-March

## Functions Tested

The scripts test the following functions from `scheduler_astro.py`:

### Time Functions
- `julian_date()` - Convert calendar date to Julian Date
- `jd_to_datetime()` - Convert Julian Date to datetime
- `gmst()` - Greenwich Mean Sidereal Time
- `lst()` - Local Sidereal Time

### Coordinate Transformations
- `galactic_coordinates()` - Convert equatorial to galactic
- `ecliptic_coordinates()` - Convert equatorial to ecliptic
- `altitude_azimuth()` - Convert RA/Dec to Alt/Az

### Rise/Set Calculations
- `rise_set_times()` - Calculate rise and set times for any object
- `twilight_times()` - Calculate all twilight times
- `hour_angle_from_altitude()` - Hour angle for given altitude

### Moon Calculations
- `moon_position()` - Calculate moon RA, Dec, and phase
- `moon_separation()` - Angular separation between objects

### Observing Calculations
- `airmass()` - Calculate airmass at given altitude
- `atmospheric_refraction()` - Refraction correction

## Output Format

### Interactive Script Output Example
```
======================================================================
MOON CALCULATIONS FOR 2025-10-03
======================================================================
Moon Position at Local Midnight (00:00 CLST = 04:00 UT 2025-10-03):
  RA:           22.25 hours
  Dec:          -11.75 degrees
  Illumination: 87.3%
  Phase:        Waning Crescent
  LST:          0.17 hours
  Altitude:     58.0°
  Azimuth:      297.1°
  Airmass:      1.179

Moon Rise/Set Times:
--------------------------------------------------
Moonrise:              19:38 UT / 15:38 CLST
Moonset:               08:29 UT / 04:29 CLST

OBSERVING CONDITIONS SUMMARY
======================================================================
Moon Interference: Poor (Near Full Moon)
Moon visibility during dark hours: Yes
```

### Automated Script Output Example
```
Moon at Local Midnight (00:00 CLST):
  RA:       22.25 hours
  Dec:      -11.75 degrees
  Phase:    87.3%
  Altitude: 58.0°
  Azimuth:  297.1°
  Airmass:  1.179

Moon Tracking Through the Night
------------------------------------------------------------
Local Time   UT Time    Altitude   Azimuth    Airmass
------------------------------------------------------------
20:00 CLST   00:00 UT     58.1°      65.1°     1.18
22:00 CLST   02:00 UT     73.0°       1.1°     1.05
00:00 CLST   04:00 UT     58.0°     297.1°     1.18
02:00 CLST   06:00 UT     33.4°     275.8°     1.82
04:00 CLST   08:00 UT      7.8°     262.0°     7.37
06:00 CLST   10:00 UT   Below horizon
```

## Key Changes in Moon Position Display

The scripts now display **moon position at local midnight** instead of noon UT because:
1. **More relevant for observations** - Astronomers need to know the moon's position during nighttime hours
2. **Better planning** - Shows actual moon interference during observing hours
3. **Practical altitude/azimuth** - Displays where the moon will be in the sky at midnight
4. **Airmass calculation** - Provides airmass value for midnight, useful for exposure planning
5. **LST at midnight** - Shows the Local Sidereal Time, helping identify which objects are at meridian

## Time Display Notes

1. **All times are shown in both Universal Time (UT) and Chilean local time**
2. The scripts automatically detect whether Chilean Standard Time (CLT) or Chilean Summer Time (CLST) applies
3. When a time crosses midnight, it's marked with "(next day)" or "(prev day)" for clarity
4. The automated script uses "-1" or "+1" notation for day boundaries in compact displays
5. Moon position is specifically calculated for 00:00 local time (which is 04:00 UT for CLST or 03:00 UT for CLT)

## Requirements

- Python 3.6+
- NumPy
- Standard library modules: `math`, `datetime`, `sys`, `os`
- Access to `src/scheduler_astro.py`

## Customization

To use for a different observatory, modify these constants in both scripts:
```python
LA_SILLA_LATITUDE = -29.2567  # degrees (negative for south)
LA_SILLA_LONGITUDE = 70.7377 / 15.0  # hours (positive for west)
LA_SILLA_ALTITUDE = 2400  # meters

# Also update the timezone function for your location
def get_chile_offset(month):
    # Adjust for your local timezone rules
    if month >= 10 or month <= 3:
        return -4  # Summer time offset
    else:
        return -3  # Winter time offset
```

## Sample Test Commands

For automated testing of a specific date:
```bash
printf "10\n3\n2025\n" | python test_scheduler_astro.py
```

For current date testing:
```bash
python test_scheduler_astro_auto.py
```

## Author

Test scripts created for the LS4 Scheduler project to validate astronomical calculations for telescope scheduling at La Silla Observatory.