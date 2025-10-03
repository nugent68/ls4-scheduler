#!/usr/bin/env python3
"""
Test script to compare scheduler_astro.py and scheduler_astropy.py outputs
Verifies that the astropy-based implementation produces compatible results
"""

import sys
import os
from datetime import datetime

# Add src directory to path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'src'))

# Import both modules
import scheduler_astro as astro_orig
import scheduler_astropy as astro_py

# La Silla Observatory coordinates
LA_SILLA_LATITUDE = -29.2567  # degrees (south is negative)
LA_SILLA_LONGITUDE = 70.7377 / 15.0  # convert to hours (west is positive)


def compare_values(name, val1, val2, tolerance=0.01, unit=""):
    """Compare two values and report differences"""
    if val1 is None and val2 is None:
        print(f"  {name:30s}: Both None - OK")
        return True
    elif val1 is None or val2 is None:
        print(f"  {name:30s}: MISMATCH - Original: {val1}, Astropy: {val2}")
        return False
    
    diff = abs(val1 - val2)
    if diff <= tolerance:
        print(f"  {name:30s}: {val1:.4f} vs {val2:.4f} {unit} (diff: {diff:.6f}) - OK")
        return True
    else:
        print(f"  {name:30s}: {val1:.4f} vs {val2:.4f} {unit} (diff: {diff:.6f}) - MISMATCH")
        return False


def test_time_functions():
    """Test time conversion functions"""
    print("\n" + "="*70)
    print("TESTING TIME CONVERSION FUNCTIONS")
    print("="*70)
    
    # Test Julian Date calculation
    year, month, day = 2025, 10, 3
    hour, minute, second = 12, 30, 45.5
    
    jd_orig = astro_orig.julian_date(year, month, day, hour, minute, second)
    jd_py = astro_py.julian_date(year, month, day, hour, minute, second)
    
    print(f"\nJulian Date for {year}-{month:02d}-{day:02d} {hour:02d}:{minute:02d}:{second:.1f}")
    compare_values("Julian Date", jd_orig, jd_py, tolerance=0.00001)
    
    # Test JD to datetime conversion
    dt_orig = astro_orig.jd_to_datetime(jd_orig)
    dt_py = astro_py.jd_to_datetime(jd_py)
    
    print(f"\nJD to Datetime conversion:")
    print(f"  Original: {dt_orig}")
    print(f"  Astropy:  {dt_py}")
    
    # Test GMST
    gmst_orig = astro_orig.gmst(jd_orig)
    gmst_py = astro_py.gmst(jd_py)
    compare_values("GMST", gmst_orig, gmst_py, tolerance=0.001, unit="hours")
    
    # Test LST
    lst_orig = astro_orig.lst(jd_orig, LA_SILLA_LONGITUDE)
    lst_py = astro_py.lst(jd_py, LA_SILLA_LONGITUDE)
    compare_values("LST at La Silla", lst_orig, lst_py, tolerance=0.001, unit="hours")


def test_coordinate_transformations():
    """Test coordinate transformation functions"""
    print("\n" + "="*70)
    print("TESTING COORDINATE TRANSFORMATIONS")
    print("="*70)
    
    # Test object: Vega
    ra_vega = 18.6156  # hours
    dec_vega = 38.7836  # degrees
    
    print(f"\nTest object: Vega (RA={ra_vega}h, Dec={dec_vega}°)")
    
    # Test Galactic coordinates
    l_orig, b_orig = astro_orig.galactic_coordinates(ra_vega, dec_vega, 2000.0)
    l_py, b_py = astro_py.galactic_coordinates(ra_vega, dec_vega, 2000.0)
    
    print("\nGalactic Coordinates:")
    compare_values("Galactic l", l_orig, l_py, tolerance=0.1, unit="deg")
    compare_values("Galactic b", b_orig, b_py, tolerance=0.1, unit="deg")
    
    # Test Ecliptic coordinates
    jd = astro_orig.julian_date(2025, 10, 3, 12, 0, 0)
    epoch_orig, lon_orig, lat_orig = astro_orig.ecliptic_coordinates(ra_vega, dec_vega, jd)
    epoch_py, lon_py, lat_py = astro_py.ecliptic_coordinates(ra_vega, dec_vega, jd)
    
    print("\nEcliptic Coordinates:")
    compare_values("Ecliptic longitude", lon_orig, lon_py, tolerance=0.5, unit="deg")
    compare_values("Ecliptic latitude", lat_orig, lat_py, tolerance=0.5, unit="deg")
    
    # Test Precession
    jd_from = astro_orig.julian_date(2000, 1, 1, 12, 0, 0)
    jd_to = astro_orig.julian_date(2025, 1, 1, 12, 0, 0)
    
    ra_prec_orig, dec_prec_orig = astro_orig.precess_coordinates(ra_vega, dec_vega, jd_from, jd_to)
    ra_prec_py, dec_prec_py = astro_py.precess_coordinates(ra_vega, dec_vega, jd_from, jd_to)
    
    print("\nPrecession (J2000 to J2025):")
    compare_values("Precessed RA", ra_prec_orig, ra_prec_py, tolerance=0.001, unit="hours")
    compare_values("Precessed Dec", dec_prec_orig, dec_prec_py, tolerance=0.01, unit="deg")


def test_sun_moon_calculations():
    """Test sun and moon position calculations"""
    print("\n" + "="*70)
    print("TESTING SUN AND MOON CALCULATIONS")
    print("="*70)
    
    jd = astro_orig.julian_date(2025, 10, 3, 12, 0, 0)
    
    # Test sun position (only in astropy version)
    print("\nSun Position (astropy only):")
    sun_ra, sun_dec = astro_py.sun_position(jd)
    print(f"  Sun RA:  {sun_ra:.4f} hours")
    print(f"  Sun Dec: {sun_dec:.4f} degrees")
    
    # Test moon position
    moon_ra_orig, moon_dec_orig, illum_orig = astro_orig.moon_position(jd)
    moon_ra_py, moon_dec_py, illum_py = astro_py.moon_position(jd)
    
    print("\nMoon Position:")
    compare_values("Moon RA", moon_ra_orig, moon_ra_py, tolerance=0.5, unit="hours")
    compare_values("Moon Dec", moon_dec_orig, moon_dec_py, tolerance=2.0, unit="deg")
    compare_values("Moon Illumination", illum_orig, illum_py, tolerance=0.1)
    
    # Test separation
    ra1, dec1 = 5.5, 23.5  # Pleiades
    sep_orig = astro_orig.moon_separation(ra1, dec1, moon_ra_orig, moon_dec_orig)
    sep_py = astro_py.moon_separation(ra1, dec1, moon_ra_py, moon_dec_py)
    
    print("\nMoon-Pleiades Separation:")
    compare_values("Angular separation", sep_orig, sep_py, tolerance=2.0, unit="deg")


def test_rise_set_times():
    """Test rise and set time calculations"""
    print("\n" + "="*70)
    print("TESTING RISE/SET TIME CALCULATIONS")
    print("="*70)
    
    jd = astro_orig.julian_date(2025, 10, 3, 12, 0, 0)
    
    # Test for Orion (should rise and set)
    ra_orion = 5.5  # hours
    dec_orion = -5.0  # degrees
    
    print(f"\nOrion (RA={ra_orion}h, Dec={dec_orion}°) at La Silla:")
    
    rise_orig, set_orig = astro_orig.rise_set_times(ra_orion, dec_orion, jd, 
                                                     LA_SILLA_LONGITUDE, LA_SILLA_LATITUDE)
    rise_py, set_py = astro_py.rise_set_times(ra_orion, dec_orion, jd,
                                              LA_SILLA_LONGITUDE, LA_SILLA_LATITUDE)
    
    compare_values("Rise time (JD)", rise_orig, rise_py, tolerance=0.01)
    compare_values("Set time (JD)", set_orig, set_py, tolerance=0.01)
    
    # Test circumpolar object
    ra_circ = 0.0
    dec_circ = -89.0
    
    print(f"\nCircumpolar object (RA={ra_circ}h, Dec={dec_circ}°) at La Silla:")
    
    rise_orig, set_orig = astro_orig.rise_set_times(ra_circ, dec_circ, jd,
                                                     LA_SILLA_LONGITUDE, LA_SILLA_LATITUDE)
    rise_py, set_py = astro_py.rise_set_times(ra_circ, dec_circ, jd,
                                              LA_SILLA_LONGITUDE, LA_SILLA_LATITUDE)
    
    if rise_orig == jd and set_orig == jd + 1.0:
        print("  Original: Circumpolar (always above horizon)")
    if rise_py == jd and set_py == jd + 1.0:
        print("  Astropy:  Circumpolar (always above horizon)")


def test_twilight_times():
    """Test twilight calculations"""
    print("\n" + "="*70)
    print("TESTING TWILIGHT CALCULATIONS")
    print("="*70)
    
    jd = astro_orig.julian_date(2025, 10, 3, 12, 0, 0)
    
    twilight_orig = astro_orig.twilight_times(jd, LA_SILLA_LONGITUDE, LA_SILLA_LATITUDE)
    twilight_py = astro_py.twilight_times(jd, LA_SILLA_LONGITUDE, LA_SILLA_LATITUDE)
    
    print("\nTwilight Times at La Silla:")
    
    twilight_types = [
        ('sunrise', 'Sunrise'),
        ('sunset', 'Sunset'),
        ('civil_dawn', 'Civil Dawn'),
        ('civil_dusk', 'Civil Dusk'),
        ('nautical_dawn', 'Nautical Dawn'),
        ('nautical_dusk', 'Nautical Dusk'),
        ('astronomical_dawn', 'Astronomical Dawn'),
        ('astronomical_dusk', 'Astronomical Dusk')
    ]
    
    for key, name in twilight_types:
        if key in twilight_orig and key in twilight_py:
            compare_values(name, twilight_orig[key], twilight_py[key], tolerance=0.02)


def test_altitude_azimuth():
    """Test altitude and azimuth calculations"""
    print("\n" + "="*70)
    print("TESTING ALTITUDE/AZIMUTH CALCULATIONS")
    print("="*70)
    
    ra = 5.5  # hours
    dec = 23.5  # degrees
    lst_hours = 6.0  # hours
    
    print(f"\nObject at RA={ra}h, Dec={dec}°, LST={lst_hours}h:")
    
    alt_orig, az_orig = astro_orig.altitude_azimuth(ra, dec, lst_hours, LA_SILLA_LATITUDE)
    alt_py, az_py = astro_py.altitude_azimuth(ra, dec, lst_hours, LA_SILLA_LATITUDE)
    
    compare_values("Altitude", alt_orig, alt_py, tolerance=0.01, unit="deg")
    compare_values("Azimuth", az_orig, az_py, tolerance=0.01, unit="deg")
    
    # Test airmass
    if alt_orig > 0:
        am_orig = astro_orig.airmass(alt_orig, 'young')
        am_py = astro_py.airmass(alt_py, 'young')
        compare_values("Airmass (Young)", am_orig, am_py, tolerance=0.001)


def test_refraction():
    """Test atmospheric refraction calculations"""
    print("\n" + "="*70)
    print("TESTING ATMOSPHERIC REFRACTION")
    print("="*70)
    
    test_altitudes = [1, 5, 10, 30, 45, 60, 90]
    
    print("\nRefraction at various altitudes:")
    for alt in test_altitudes:
        refr_orig = astro_orig.atmospheric_refraction(alt)
        refr_py = astro_py.atmospheric_refraction(alt)
        compare_values(f"Refraction at {alt}°", refr_orig, refr_py, tolerance=0.0001, unit="deg")


def main():
    """Main test function"""
    print("\n" + "="*70)
    print(" COMPARISON TEST: scheduler_astro.py vs scheduler_astropy.py")
    print(" Testing at La Silla Observatory: 29°15'S, 70°44'W")
    print("="*70)
    
    # Check if astropy is installed
    try:
        import astropy
        print(f"\nAstropy version: {astropy.__version__}")
    except ImportError:
        print("\nERROR: Astropy is not installed!")
        print("Please install it with: pip install astropy")
        return
    
    # Run all tests
    test_time_functions()
    test_coordinate_transformations()
    test_sun_moon_calculations()
    test_rise_set_times()
    test_twilight_times()
    test_altitude_azimuth()
    test_refraction()
    
    print("\n" + "="*70)
    print(" All comparison tests completed!")
    print(" Note: Small differences are expected due to different algorithms")
    print(" Tolerances are set to acceptable levels for practical use")
    print("="*70 + "\n")


if __name__ == "__main__":
    main()