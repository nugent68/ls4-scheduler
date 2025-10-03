#!/usr/bin/env python3
"""
Automated test script for scheduler_astro.py functions
Tests sun and moon rise/set times for La Silla Observatory with predefined dates
"""

import sys
import os
from datetime import datetime, timedelta

# Add src directory to path to import scheduler_astro
sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'src'))

from scheduler_astro import (
    julian_date,
    jd_to_datetime,
    lst,
    moon_position,
    rise_set_times,
    twilight_times,
    altitude_azimuth,
    airmass,
    moon_separation,
    gmst,
    galactic_coordinates,
    ecliptic_coordinates,
    atmospheric_refraction
)


# La Silla Observatory coordinates
LA_SILLA_LATITUDE = -29.2567  # degrees (south is negative)
LA_SILLA_LONGITUDE = 70.7377 / 15.0  # convert to hours (west is positive)
LA_SILLA_ALTITUDE = 2400  # meters


def get_chile_offset(month):
    """Get Chile time zone offset based on month (simplified)"""
    # Chile DST roughly from October to March (southern hemisphere)
    if month >= 10 or month <= 3:
        return -4  # CLST (summer time)
    else:
        return -3  # CLT (winter time)


def format_time_with_local(jd, tz_offset):
    """Format time with both UT and local time"""
    dt = jd_to_datetime(jd)
    ut_str = dt.strftime('%H:%M')
    
    local_dt = dt + timedelta(hours=tz_offset)
    local_str = local_dt.strftime('%H:%M')
    
    # Add day indicator if different
    if local_dt.day != dt.day:
        if local_dt.day > dt.day:
            local_str += "+1"
        else:
            local_str += "-1"
    
    return ut_str, local_str


def test_specific_date(year, month, day):
    """Test calculations for a specific date"""
    print(f"\n{'='*70}")
    print(f"Testing date: {year:04d}-{month:02d}-{day:02d}")
    print(f"{'='*70}")
    
    # Get timezone info
    tz_offset = get_chile_offset(month)
    tz_name = "CLST" if tz_offset == -4 else "CLT"
    
    # Calculate JD
    jd_noon = julian_date(year, month, day, 12, 0, 0)
    print(f"Julian Date (noon): {jd_noon:.4f}")
    print(f"Time Zone: {tz_name} (UTC{tz_offset:+d})")
    
    # Test reverse conversion
    dt = jd_to_datetime(jd_noon)
    print(f"JD to datetime: {dt.strftime('%Y-%m-%d %H:%M:%S')} UT")
    
    # Test GMST and LST
    gmst_hours = gmst(jd_noon)
    lst_hours = lst(jd_noon, LA_SILLA_LONGITUDE)
    print(f"GMST: {gmst_hours:.4f} hours")
    print(f"LST at La Silla: {lst_hours:.4f} hours")
    
    # Get twilight times
    twilight = twilight_times(jd_noon, LA_SILLA_LONGITUDE, LA_SILLA_LATITUDE)
    
    print(f"\nSun Rise/Set (UT / {tz_name}):")
    if 'sunrise' in twilight and 'sunset' in twilight:
        sunrise_ut, sunrise_local = format_time_with_local(twilight['sunrise'], tz_offset)
        sunset_ut, sunset_local = format_time_with_local(twilight['sunset'], tz_offset)
        print(f"  Sunrise:  {sunrise_ut} UT / {sunrise_local} {tz_name}")
        print(f"  Sunset:   {sunset_ut} UT / {sunset_local} {tz_name}")
    
    print(f"\nAstronomical Twilight (UT / {tz_name}):")
    if 'astronomical_dawn' in twilight and 'astronomical_dusk' in twilight:
        dawn_ut, dawn_local = format_time_with_local(twilight['astronomical_dawn'], tz_offset)
        dusk_ut, dusk_local = format_time_with_local(twilight['astronomical_dusk'], tz_offset)
        print(f"  Dawn:     {dawn_ut} UT / {dawn_local} {tz_name}")
        print(f"  Dusk:     {dusk_ut} UT / {dusk_local} {tz_name}")
        
        # Calculate dark hours
        dark_hours = (twilight['astronomical_dawn'] - twilight['astronomical_dusk']) * 24.0
        if dark_hours < 0:
            dark_hours += 24.0
        print(f"  Dark hours: {dark_hours:.2f}")
    
    # Moon calculations
    moon_ra, moon_dec, illumination = moon_position(jd_noon)
    print(f"\nMoon:")
    print(f"  RA:       {moon_ra:.2f} hours")
    print(f"  Dec:      {moon_dec:.2f} degrees")
    print(f"  Phase:    {illumination:.1%}")
    
    # Moon rise/set
    rise_jd, set_jd = rise_set_times(moon_ra, moon_dec, jd_noon,
                                     LA_SILLA_LONGITUDE, LA_SILLA_LATITUDE)
    if rise_jd and set_jd:
        moonrise_ut, moonrise_local = format_time_with_local(rise_jd, tz_offset)
        moonset_ut, moonset_local = format_time_with_local(set_jd, tz_offset)
        print(f"  Moonrise: {moonrise_ut} UT / {moonrise_local} {tz_name}")
        print(f"  Moonset:  {moonset_ut} UT / {moonset_local} {tz_name}")


def test_coordinate_transformations():
    """Test coordinate transformation functions"""
    print(f"\n{'='*70}")
    print("Testing Coordinate Transformations")
    print(f"{'='*70}")
    
    # Test object: Vega
    ra_vega = 18.6156  # hours
    dec_vega = 38.7836  # degrees
    
    print(f"\nVega (α Lyrae):")
    print(f"  RA:  {ra_vega:.4f} hours")
    print(f"  Dec: {dec_vega:.4f} degrees")
    
    # Galactic coordinates
    l, b = galactic_coordinates(ra_vega, dec_vega, 2000.0)
    print(f"  Galactic l: {l:.2f}°, b: {b:.2f}°")
    
    # Ecliptic coordinates
    jd = julian_date(2025, 10, 3, 12, 0, 0)
    epoch, lon, lat = ecliptic_coordinates(ra_vega, dec_vega, jd)
    print(f"  Ecliptic λ: {lon:.2f}°, β: {lat:.2f}° (epoch {epoch:.1f})")
    
    # Test altitude/azimuth at specific LST
    lst_test = 20.0  # hours
    alt, az = altitude_azimuth(ra_vega, dec_vega, lst_test, LA_SILLA_LATITUDE)
    print(f"\nAt LST = {lst_test:.1f}h:")
    print(f"  Altitude: {alt:.2f}°")
    print(f"  Azimuth:  {az:.2f}°")
    if alt > 0:
        am = airmass(alt, 'young')
        print(f"  Airmass:  {am:.3f}")
        refr = atmospheric_refraction(alt)
        print(f"  Refraction: {refr:.3f}°")


def test_extreme_cases():
    """Test extreme cases and edge conditions"""
    print(f"\n{'='*70}")
    print("Testing Extreme Cases")
    print(f"{'='*70}")
    
    # Test circumpolar object (never sets)
    ra_polar = 0.0  # hours
    dec_polar = -89.0  # degrees (near south celestial pole)
    
    jd = julian_date(2025, 10, 3, 12, 0, 0)
    rise_jd, set_jd = rise_set_times(ra_polar, dec_polar, jd,
                                     LA_SILLA_LONGITUDE, LA_SILLA_LATITUDE)
    
    print(f"\nCircumpolar object (RA={ra_polar}h, Dec={dec_polar}°):")
    if rise_jd == jd and set_jd == jd + 1.0:
        print("  Status: Always above horizon (circumpolar)")
    elif rise_jd is None:
        print("  Status: Never rises")
    else:
        print(f"  Rise: JD {rise_jd:.4f}, Set: JD {set_jd:.4f}")
    
    # Test object that never rises
    ra_never = 0.0  # hours  
    dec_never = 70.0  # degrees (far north)
    
    rise_jd, set_jd = rise_set_times(ra_never, dec_never, jd,
                                     LA_SILLA_LONGITUDE, LA_SILLA_LATITUDE)
    
    print(f"\nNever-rising object (RA={ra_never}h, Dec={dec_never}°):")
    if rise_jd is None and set_jd is None:
        print("  Status: Never rises above horizon")
    else:
        print(f"  Rise: JD {rise_jd:.4f}, Set: JD {set_jd:.4f}")
    
    # Test airmass at different altitudes
    print("\nAirmass vs Altitude:")
    for alt in [90, 60, 45, 30, 20, 10, 5, 1]:
        am_secant = airmass(alt, 'secant')
        am_young = airmass(alt, 'young')
        print(f"  Alt {alt:2d}°: Secant={am_secant:6.3f}, Young={am_young:6.3f}")


def test_multiple_dates():
    """Test calculations for multiple dates throughout the year"""
    print(f"\n{'='*70}")
    print("Testing Multiple Dates Throughout 2025")
    print(f"{'='*70}")
    
    # Test solstices and equinoxes
    test_dates = [
        (2025, 3, 20, "Spring Equinox"),
        (2025, 6, 21, "Winter Solstice (Southern)"),
        (2025, 9, 23, "Fall Equinox"),
        (2025, 12, 21, "Summer Solstice (Southern)")
    ]
    
    print("\nNight Duration and Times at Key Dates:")
    print("-" * 60)
    print(f"{'Date':<25s} {'Dark Hours':<12s} {'Dusk (Local)':<15s} {'Dawn (Local)'}")
    print("-" * 60)
    
    for year, month, day, description in test_dates:
        jd_noon = julian_date(year, month, day, 12, 0, 0)
        twilight = twilight_times(jd_noon, LA_SILLA_LONGITUDE, LA_SILLA_LATITUDE)
        
        # Get timezone offset
        tz_offset = get_chile_offset(month)
        tz_name = "CLST" if tz_offset == -4 else "CLT"
        
        if 'astronomical_dawn' in twilight and 'astronomical_dusk' in twilight:
            dark_hours = (twilight['astronomical_dawn'] - twilight['astronomical_dusk']) * 24.0
            if dark_hours < 0:
                dark_hours += 24.0
            
            # Get local times
            dusk_ut, dusk_local = format_time_with_local(twilight['astronomical_dusk'], tz_offset)
            dawn_ut, dawn_local = format_time_with_local(twilight['astronomical_dawn'], tz_offset)
            
            print(f"  {description:25s} {dark_hours:5.2f} hours   {dusk_local:6s} {tz_name}    {dawn_local:6s} {tz_name}")


def test_observing_planning():
    """Test practical observing planning scenarios"""
    print(f"\n{'='*70}")
    print("Testing Observing Planning Scenarios")
    print(f"{'='*70}")
    
    # Test for tonight (or a specific date)
    year, month, day = 2025, 10, 3
    jd_noon = julian_date(year, month, day, 12, 0, 0)
    
    # Get timezone
    tz_offset = get_chile_offset(month)
    tz_name = "CLST" if tz_offset == -4 else "CLT"
    
    print(f"\nObserving Plan for {year:04d}-{month:02d}-{day:02d}:")
    print("=" * 50)
    
    # Get twilight times
    twilight = twilight_times(jd_noon, LA_SILLA_LONGITUDE, LA_SILLA_LATITUDE)
    
    if 'astronomical_dusk' in twilight and 'astronomical_dawn' in twilight:
        # Calculate observing hours
        jd_dusk = twilight['astronomical_dusk']
        jd_dawn = twilight['astronomical_dawn']
        
        # Display times
        dusk_ut, dusk_local = format_time_with_local(jd_dusk, tz_offset)
        dawn_ut, dawn_local = format_time_with_local(jd_dawn, tz_offset)
        
        print(f"Observing Window:")
        print(f"  Start: {dusk_local} {tz_name} ({dusk_ut} UT)")
        print(f"  End:   {dawn_local} {tz_name} ({dawn_ut} UT)")
        
        # Calculate LST range
        lst_start = lst(jd_dusk, LA_SILLA_LONGITUDE)
        lst_end = lst(jd_dawn, LA_SILLA_LONGITUDE)
        
        print(f"\nLST Range: {lst_start:.2f}h - {lst_end:.2f}h")
        
        # Test visibility of some sample objects
        test_objects = [
            ("M42 (Orion Nebula)", 5.583, -5.383),
            ("M31 (Andromeda)", 0.712, 41.269),
            ("Omega Centauri", 13.446, -47.479),
            ("LMC Center", 5.392, -69.756),
            ("SMC Center", 0.877, -72.829)
        ]
        
        print(f"\nObject Visibility at Midnight ({tz_name}):")
        print("-" * 50)
        
        # Calculate local midnight JD
        jd_midnight_local = julian_date(year, month, day, 0, 0, 0) + 1 - tz_offset / 24.0
        lst_midnight = lst(jd_midnight_local, LA_SILLA_LONGITUDE)
        
        for name, ra, dec in test_objects:
            alt, az = altitude_azimuth(ra, dec, lst_midnight, LA_SILLA_LATITUDE)
            if alt > 0:
                am = airmass(alt, 'young')
                status = f"Alt: {alt:5.1f}°, Az: {az:5.1f}°, AM: {am:4.2f}"
            else:
                status = "Below horizon"
            print(f"  {name:20s} {status}")


def main():
    """Main test function"""
    print("\n" + "="*70)
    print(" Automated Tests for scheduler_astro.py")
    print(" La Silla Observatory: 29°15'S, 70°44'W")
    print("="*70)
    
    # Test today's date
    today = datetime.now()
    test_specific_date(today.year, today.month, today.day)
    
    # Test a specific date
    test_specific_date(2025, 10, 3)
    
    # Test coordinate transformations
    test_coordinate_transformations()
    
    # Test extreme cases
    test_extreme_cases()
    
    # Test multiple dates
    test_multiple_dates()
    
    # Test observing planning
    test_observing_planning()
    
    print("\n" + "="*70)
    print(" All tests completed successfully!")
    print("="*70 + "\n")


if __name__ == "__main__":
    main()