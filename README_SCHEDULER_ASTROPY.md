# scheduler_astropy.py - Astropy-based Astronomical Calculations

## Overview

`scheduler_astropy.py` is a reimplementation of the original `scheduler_astro.py` module using the [Astropy](https://www.astropy.org/) package. This provides more accurate and robust astronomical calculations while maintaining the same function interfaces for compatibility.

## Installation

Before using this module, you need to install Astropy:

```bash
pip install astropy
```

## Key Improvements Over Original Implementation

1. **More Accurate Ephemerides**: Uses professional-grade solar system ephemerides from Astropy
2. **Better Coordinate Transformations**: Leverages Astropy's comprehensive coordinate systems
3. **Improved Time Handling**: Uses Astropy's Time class for precise time conversions
4. **Professional Standards**: Implements IAU-standard algorithms for astronomical calculations

## Function Compatibility

All functions maintain the same interface as the original `scheduler_astro.py`:

### Time Conversion Functions
- `julian_date(year, month, day, hour, minute, second)` - Convert to Julian Date
- `jd_to_datetime(jd)` - Convert JD to datetime
- `gmst(jd)` - Greenwich Mean Sidereal Time
- `lst(jd, longitude)` - Local Sidereal Time
- `ut_to_jd(ut_hours, jd_start)` - Convert UT hours to JD

### Coordinate Transformations
- `precess_coordinates(ra, dec, jd_from, jd_to)` - Precess between epochs
- `galactic_coordinates(ra, dec, epoch)` - Convert to galactic coordinates
- `ecliptic_coordinates(ra, dec, jd)` - Convert to ecliptic coordinates

### Rise/Set Calculations
- `hour_angle_from_altitude(altitude, dec, lat)` - Hour angle for given altitude
- `rise_set_times(ra, dec, jd, longitude, latitude, altitude)` - Calculate rise/set times

### Sun and Moon Functions
- `sun_position(jd)` - Calculate sun RA and Dec (NEW - not in original)
- `moon_position(jd)` - Calculate moon position and phase
- `moon_separation(ra1, dec1, ra2, dec2)` - Angular separation

### Twilight Calculations
- `twilight_times(jd, longitude, latitude)` - All twilight times

### Altitude and Airmass
- `altitude_azimuth(ra, dec, lst_hours, latitude)` - Alt/Az from RA/Dec
- `airmass(altitude, model)` - Calculate airmass (secant, hardie, young)
- `parallactic_angle(ha, dec, latitude)` - Parallactic angle
- `atmospheric_refraction(altitude, temperature, pressure)` - Refraction correction

## Expected Differences from Original

When comparing results with the original `scheduler_astro.py`, you may notice small differences:

### Acceptable Differences (Normal)
- **Time conversions**: < 0.001 days (~1.4 minutes)
- **GMST/LST**: < 0.05 hours (~3 minutes)
- **Rise/Set times**: < 0.02 days (~30 minutes)
- **Moon position**: < 1 degree
- **Precession**: < 0.01 hours in RA, < 0.01 degrees in Dec

### Reasons for Differences
1. **Different Algorithms**: Astropy uses IAU-standard algorithms
2. **Better Ephemerides**: More accurate solar system positions
3. **Time Scales**: Astropy properly handles UT1 vs UTC
4. **Numerical Precision**: Different numerical methods

## Usage Examples

### Basic Time Calculations
```python
from scheduler_astropy import julian_date, lst

# Calculate JD for a specific date
jd = julian_date(2025, 10, 3, 12, 0, 0)
print(f"Julian Date: {jd}")

# Calculate LST at La Silla
longitude = 70.7377 / 15.0  # hours
lst_hours = lst(jd, longitude)
print(f"LST: {lst_hours:.2f} hours")
```

### Moon Position and Phase
```python
from scheduler_astropy import moon_position

# Get moon position and phase
ra, dec, illumination = moon_position(jd)
print(f"Moon RA: {ra:.2f}h, Dec: {dec:.2f}°")
print(f"Illumination: {illumination:.1%}")
```

### Rise/Set Times
```python
from scheduler_astropy import rise_set_times

# Calculate rise/set for an object
ra, dec = 5.5, -5.0  # Orion
latitude = -29.2567  # La Silla
rise_jd, set_jd = rise_set_times(ra, dec, jd, longitude, latitude)
```

### Coordinate Transformations
```python
from scheduler_astropy import galactic_coordinates, precess_coordinates

# Convert to galactic
l, b = galactic_coordinates(ra, dec, 2000.0)
print(f"Galactic: l={l:.2f}°, b={b:.2f}°")

# Precess from J2000 to J2025
jd_2000 = julian_date(2000, 1, 1, 12, 0, 0)
jd_2025 = julian_date(2025, 1, 1, 12, 0, 0)
ra_new, dec_new = precess_coordinates(ra, dec, jd_2000, jd_2025)
```

## Performance Considerations

The Astropy-based implementation may be slightly slower than the original due to:
- More complex algorithms
- Additional error checking
- Higher precision calculations

For most applications, the performance difference is negligible and the improved accuracy is worth it.

## Testing

A comprehensive test suite is provided in `test_scheduler_astropy.py` that compares outputs with the original implementation:

```bash
python test_scheduler_astropy.py
```

## Migration Guide

To migrate from `scheduler_astro.py` to `scheduler_astropy.py`:

1. **Install Astropy**: `pip install astropy`
2. **Update imports**:
   ```python
   # Old
   import scheduler_astro as astro
   
   # New
   import scheduler_astropy as astro
   ```
3. **No code changes needed**: All function signatures are identical
4. **Test thoroughly**: Small numerical differences are expected

## Known Limitations

1. **Moon Phase**: Uses simplified elongation-based calculation (same as original)
2. **Atmospheric Refraction**: Uses same simplified model as original
3. **Rise/Set Times**: Basic calculation without iterative refinement

## Future Improvements

Potential enhancements that could be added:
- More accurate moon phase using lunar libration
- Iterative rise/set time calculations
- Planet positions
- Satellite tracking
- Occultation predictions

## Dependencies

- Python 3.6+
- Astropy 4.0+ (automatically installs numpy, pyerfa, etc.)
- NumPy (installed with Astropy)

## Author

Reimplemented for the LS4 Scheduler project using Astropy for improved accuracy and professional-grade astronomical calculations.

## References

- [Astropy Documentation](https://docs.astropy.org/)
- [IAU SOFA Standards](https://www.iausofa.org/)
- Original `scheduler_astro.py` implementation