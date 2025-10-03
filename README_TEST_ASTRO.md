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
  - Moon position (RA, Dec)
  - Moon phase and illumination percentage
  - Moonrise and moonset times
  - Moon altitude at both UT and local midnight
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
- Seasonal variations (solstices and equinoxes)
- Atmospheric refraction corrections
- Practical observing planning with object visibility

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
SUN CALCULATIONS FOR 2025-10-03
======================================================================
Time Zone: CLST (UTC-4)

Twilight Times:
--------------------------------------------------
Event                  UT Time      Local Time (CLST)
--------------------------------------------------
Astronomical Dawn:     08:53 UT / 04:53 CLST
Sunrise:               10:13 UT / 06:13 CLST
Sunset:                22:41 UT / 18:41 CLST
Astronomical Dusk:     00:01 UT / 20:01 CLST

Dark Sky Duration: 8.87 hours
Best Observing Window: 20:01 - 04:53 CLST
                       (00:01 - 08:53 UT)

MOON CALCULATIONS FOR 2025-10-03
======================================================================
Moon Position at noon UT:
  Illumination: 82.1%
  Phase:        Waning Crescent

Moon Rise/Set Times:
--------------------------------------------------
Moonrise:              18:55 UT / 14:55 CLST
Moonset:               08:05 UT / 04:05 CLST

Moon at Midnight:
  UT Midnight (00:00 UT):
    Altitude:     63.9°
    Airmass:      1.113
  Local Midnight (00:00 CLST):
    Altitude:     54.0°
    Airmass:      1.236
```

### Automated Script Output Example
```
Testing Coordinate Transformations
======================================================================
Vega (α Lyrae):
  RA:  18.6156 hours
  Dec: 38.7836 degrees
  Galactic l: 178.43°, b: 19.27°

Sun Rise/Set (UT / CLST):
  Sunrise:  10:13 UT / 06:13 CLST
  Sunset:   22:41 UT / 18:41 CLST

Night Duration at Key Dates:
------------------------------------------------------------
Date                      Dark Hours   Dusk (Local)    Dawn (Local)
------------------------------------------------------------
Spring Equinox             9.21 hours   20:09 CLST    05:22 CLST
Winter Solstice           10.87 hours   20:13 CLT     07:06 CLT

Object Visibility at Midnight (CLST):
--------------------------------------------------
M42 (Orion Nebula)   Alt:  10.3°, Az:  90.4°, AM: 5.40
LMC Center           Alt:  31.3°, Az: 156.6°, AM: 1.92
```

## Time Display Notes

1. **All times are shown in both Universal Time (UT) and Chilean local time**
2. The scripts automatically detect whether Chilean Standard Time (CLT) or Chilean Summer Time (CLST) applies
3. When a time crosses midnight, it's marked with "(next day)" or "(prev day)" for clarity
4. The automated script uses "-1" or "+1" notation for day boundaries in compact displays

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