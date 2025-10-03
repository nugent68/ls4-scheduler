#!/usr/bin/env python3
"""
Test script for scheduler_astro.py functions
Tests sun and moon rise/set times for La Silla Observatory
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
    moon_separation
)


# La Silla Observatory coordinates
LA_SILLA_LATITUDE = -29.2567  # degrees (south is negative)
LA_SILLA_LONGITUDE = 70.7377 / 15.0  # convert to hours (west is positive)
LA_SILLA_ALTITUDE = 2400  # meters (not used in basic calculations)

# Time zone offset for Chile
# Chile uses CLT (UTC-3) in winter and CLST (UTC-4) in summer
# For simplicity, we'll use UTC-3 as standard, but this should be adjusted based on date
def get_chile_offset(month):
    """Get Chile time zone offset based on month (simplified)"""
    # Chile DST roughly from October to March (southern hemisphere)
    if month >= 10 or month <= 3:
        return -4  # CLST (summer time)
    else:
        return -3  # CLT (winter time)


def format_jd_to_time(jd, date_jd, timezone_offset=0):
    """
    Convert JD to readable time format in both UT and local time
    Returns tuple of (ut_string, local_string)
    """
    dt = jd_to_datetime(jd)
    
    # UT time
    ut_str = dt.strftime("%H:%M")
    
    # Local time
    local_dt = dt + timedelta(hours=timezone_offset)
    # Handle day boundary
    if local_dt.day != dt.day:
        local_str = local_dt.strftime("%H:%M (next day)") if local_dt.day > dt.day else local_dt.strftime("%H:%M (prev day)")
    else:
        local_str = local_dt.strftime("%H:%M")
    
    return ut_str, local_str


def format_time_display(label, jd, date_jd, timezone_offset):
    """Format time display with both UT and local time"""
    ut_str, local_str = format_jd_to_time(jd, date_jd, timezone_offset)
    return f"{label:22s} {ut_str} UT / {local_str} CLT"


def calculate_sun_times(year, month, day):
    """Calculate sun rise/set and twilight times"""
    print(f"\n{'='*70}")
    print(f"SUN CALCULATIONS FOR {year:04d}-{month:02d}-{day:02d}")
    print(f"{'='*70}")
    
    # Get timezone offset
    tz_offset = get_chile_offset(month)
    tz_name = "CLST" if tz_offset == -4 else "CLT"
    print(f"Time Zone: {tz_name} (UTC{tz_offset:+d})")
    
    # Calculate JD for noon of the given date
    jd_noon = julian_date(year, month, day, 12, 0, 0)
    jd_start = julian_date(year, month, day, 0, 0, 0)
    
    print(f"Julian Date (noon): {jd_noon:.4f}")
    
    # Get twilight times (includes sunrise/sunset)
    twilight = twilight_times(jd_noon, LA_SILLA_LONGITUDE, LA_SILLA_LATITUDE)
    
    print(f"\nTwilight Times:")
    print("-" * 50)
    print(f"{'Event':<22s} {'UT Time':<12s} {'Local Time (' + tz_name + ')'}")
    print("-" * 50)
    
    # Morning events (in chronological order)
    if 'astronomical_dawn' in twilight:
        print(format_time_display("Astronomical Dawn:", twilight['astronomical_dawn'], jd_start, tz_offset))
    if 'nautical_dawn' in twilight:
        print(format_time_display("Nautical Dawn:", twilight['nautical_dawn'], jd_start, tz_offset))
    if 'civil_dawn' in twilight:
        print(format_time_display("Civil Dawn:", twilight['civil_dawn'], jd_start, tz_offset))
    if 'sunrise' in twilight:
        print(format_time_display("Sunrise:", twilight['sunrise'], jd_start, tz_offset))
    
    print()
    
    # Evening events (in chronological order)
    if 'sunset' in twilight:
        print(format_time_display("Sunset:", twilight['sunset'], jd_start, tz_offset))
    if 'civil_dusk' in twilight:
        print(format_time_display("Civil Dusk:", twilight['civil_dusk'], jd_start, tz_offset))
    if 'nautical_dusk' in twilight:
        print(format_time_display("Nautical Dusk:", twilight['nautical_dusk'], jd_start, tz_offset))
    if 'astronomical_dusk' in twilight:
        print(format_time_display("Astronomical Dusk:", twilight['astronomical_dusk'], jd_start, tz_offset))
    
    # Calculate observing night duration
    if 'astronomical_dusk' in twilight and 'astronomical_dawn' in twilight:
        night_duration = (twilight['astronomical_dawn'] - twilight['astronomical_dusk']) * 24.0
        if night_duration < 0:
            night_duration += 24.0
        print(f"\nDark Sky Duration: {night_duration:.2f} hours")
        
        # Show best observing window in both times
        ut_dusk, local_dusk = format_jd_to_time(twilight['astronomical_dusk'], jd_start, tz_offset)
        ut_dawn, local_dawn = format_jd_to_time(twilight['astronomical_dawn'], jd_start, tz_offset)
        print(f"Best Observing Window: {local_dusk} - {local_dawn} {tz_name}")
        print(f"                       ({ut_dusk} - {ut_dawn} UT)")
    
    return twilight, tz_offset, tz_name


def calculate_moon_times(year, month, day, tz_offset, tz_name):
    """Calculate moon rise/set times and phase"""
    print(f"\n{'='*70}")
    print(f"MOON CALCULATIONS FOR {year:04d}-{month:02d}-{day:02d}")
    print(f"{'='*70}")
    
    # Calculate JD for noon of the given date
    jd_noon = julian_date(year, month, day, 12, 0, 0)
    jd_start = julian_date(year, month, day, 0, 0, 0)
    
    # Get moon position and phase
    moon_ra, moon_dec, illumination = moon_position(jd_noon)
    
    print(f"Moon Position at noon UT:")
    print(f"  RA:           {moon_ra:.2f} hours")
    print(f"  Dec:          {moon_dec:.2f} degrees")
    print(f"  Illumination: {illumination:.1%}")
    
    # Determine moon phase name
    if illumination < 0.05:
        phase_name = "New Moon"
    elif illumination < 0.23:
        phase_name = "Waxing Crescent"
    elif illumination < 0.27:
        phase_name = "First Quarter"
    elif illumination < 0.48:
        phase_name = "Waxing Gibbous"
    elif illumination < 0.52:
        phase_name = "Full Moon"
    elif illumination < 0.73:
        phase_name = "Waning Gibbous"
    elif illumination < 0.77:
        phase_name = "Last Quarter"
    elif illumination < 0.95:
        phase_name = "Waning Crescent"
    else:
        phase_name = "New Moon"
    
    print(f"  Phase:        {phase_name}")
    
    # Calculate moon rise/set times
    rise_jd, set_jd = rise_set_times(moon_ra, moon_dec, jd_noon, 
                                     LA_SILLA_LONGITUDE, LA_SILLA_LATITUDE)
    
    print(f"\nMoon Rise/Set Times:")
    print("-" * 50)
    print(f"{'Event':<22s} {'UT Time':<12s} {'Local Time (' + tz_name + ')'}")
    print("-" * 50)
    
    if rise_jd and set_jd:
        # Check if moonrise is before moonset (moon rises during this day)
        if rise_jd < set_jd:
            print(format_time_display("Moonrise:", rise_jd, jd_start, tz_offset))
            print(format_time_display("Moonset:", set_jd, jd_start, tz_offset))
        else:
            print(format_time_display("Moonset:", set_jd, jd_start, tz_offset))
            print(format_time_display("Moonrise:", rise_jd, jd_start, tz_offset))
        
        # Calculate moon visibility duration
        if set_jd > rise_jd:
            moon_up_hours = (set_jd - rise_jd) * 24.0
        else:
            moon_up_hours = 24.0 - (rise_jd - set_jd) * 24.0
        print(f"\nMoon above horizon: {moon_up_hours:.2f} hours")
    elif rise_jd is not None:
        print("Moon is circumpolar (always above horizon)")
    else:
        print("Moon never rises above horizon")
    
    # Calculate moon altitude at midnight (both UT and local)
    jd_midnight_ut = julian_date(year, month, day, 0, 0, 0)
    jd_midnight_local = jd_midnight_ut - tz_offset / 24.0
    
    # UT midnight
    lst_midnight_ut = lst(jd_midnight_ut, LA_SILLA_LONGITUDE)
    alt_ut, az_ut = altitude_azimuth(moon_ra, moon_dec, lst_midnight_ut, LA_SILLA_LATITUDE)
    
    # Local midnight
    lst_midnight_local = lst(jd_midnight_local, LA_SILLA_LONGITUDE)
    alt_local, az_local = altitude_azimuth(moon_ra, moon_dec, lst_midnight_local, LA_SILLA_LATITUDE)
    
    print(f"\nMoon at Midnight:")
    print(f"  UT Midnight (00:00 UT):")
    print(f"    Altitude:     {alt_ut:.1f}°")
    print(f"    Azimuth:      {az_ut:.1f}°")
    if alt_ut > 0:
        am = airmass(alt_ut)
        print(f"    Airmass:      {am:.3f}")
    
    print(f"  Local Midnight (00:00 {tz_name}):")
    print(f"    Altitude:     {alt_local:.1f}°")
    print(f"    Azimuth:      {az_local:.1f}°")
    if alt_local > 0:
        am = airmass(alt_local)
        print(f"    Airmass:      {am:.3f}")
    
    return moon_ra, moon_dec, illumination


def calculate_observing_conditions(year, month, day, twilight, moon_info, tz_offset, tz_name):
    """Analyze observing conditions for the night"""
    print(f"\n{'='*70}")
    print(f"OBSERVING CONDITIONS SUMMARY")
    print(f"{'='*70}")
    
    moon_ra, moon_dec, illumination = moon_info
    
    # Determine overall conditions
    if illumination < 0.25:
        moon_condition = "Excellent (Dark)"
    elif illumination < 0.50:
        moon_condition = "Good (Quarter Moon)"
    elif illumination < 0.75:
        moon_condition = "Fair (Gibbous Moon)"
    else:
        moon_condition = "Poor (Near Full Moon)"
    
    print(f"Moon Interference: {moon_condition}")
    
    # Best observing window
    if 'astronomical_dusk' in twilight and 'astronomical_dawn' in twilight:
        dusk_time = twilight['astronomical_dusk']
        dawn_time = twilight['astronomical_dawn']
        
        # Convert to both time formats
        jd_start = julian_date(year, month, day, 0, 0, 0)
        ut_dusk, local_dusk = format_jd_to_time(dusk_time, jd_start, tz_offset)
        ut_dawn, local_dawn = format_jd_to_time(dawn_time, jd_start, tz_offset)
        
        print(f"\nBest Observing Window:")
        print(f"  Start: {local_dusk} {tz_name} ({ut_dusk} UT) - Astronomical Dusk")
        print(f"  End:   {local_dawn} {tz_name} ({ut_dawn} UT) - Astronomical Dawn")
        
        # Calculate LST range
        lst_dusk = lst(dusk_time, LA_SILLA_LONGITUDE)
        lst_dawn = lst(dawn_time, LA_SILLA_LONGITUDE)
        
        print(f"\nLocal Sidereal Time Range:")
        print(f"  At dusk:  {lst_dusk:.2f} hours")
        print(f"  At dawn:  {lst_dawn:.2f} hours")
        
        # Objects best placed
        print(f"\nObjects at Meridian:")
        print(f"  At dusk:  RA ~ {lst_dusk:.1f}h")
        print(f"  At midnight: RA ~ {(lst_dusk + lst_dawn) / 2:.1f}h")
        print(f"  At dawn:  RA ~ {lst_dawn:.1f}h")


def main():
    """Main function to run tests"""
    print("\n" + "="*70)
    print(" La Silla Observatory Ephemeris Calculator")
    print(" Location: 29°15'S, 70°44'W, Altitude: 2400m")
    print("="*70)
    
    # Get date input from user
    print("\nEnter date for calculations:")
    try:
        month = int(input("Month (1-12): "))
        day = int(input("Day (1-31): "))
        year = int(input("Year (e.g., 2024): "))
        
        # Validate input
        if month < 1 or month > 12:
            raise ValueError("Month must be between 1 and 12")
        if day < 1 or day > 31:
            raise ValueError("Day must be between 1 and 31")
        if year < 1900 or year > 2100:
            raise ValueError("Year must be between 1900 and 2100")
        
    except ValueError as e:
        print(f"Invalid input: {e}")
        print("Using today's date instead...")
        today = datetime.now()
        year, month, day = today.year, today.month, today.day
    
    # Calculate sun times and get timezone info
    twilight, tz_offset, tz_name = calculate_sun_times(year, month, day)
    
    # Calculate moon times
    moon_info = calculate_moon_times(year, month, day, tz_offset, tz_name)
    
    # Analyze observing conditions
    calculate_observing_conditions(year, month, day, twilight, moon_info, tz_offset, tz_name)
    
    print("\n" + "="*70)
    print("Calculations complete!")
    print("="*70 + "\n")


if __name__ == "__main__":
    main()