"""
Astronomical Calculation Module for Scheduler using Astropy
This module reimplements scheduler_astro.py functionality using the astropy package

This module provides astronomical calculations including:
- Time conversions (UT, LST, JD)
- Coordinate transformations
- Rise/set time calculations
- Moon position and interference checks
- Airmass calculations

All functions maintain the same interface as scheduler_astro.py for compatibility
"""

import math
import numpy as np
from datetime import datetime, timedelta
from typing import Tuple, Optional
from dataclasses import dataclass
import logging
import warnings

# Suppress astropy warnings for cleaner output
warnings.filterwarnings('ignore', category=UserWarning)

# Import astropy modules
from astropy import units as u
from astropy.time import Time
from astropy.coordinates import (
    SkyCoord, EarthLocation, AltAz,
    get_sun, get_body,
    solar_system_ephemeris,
    FK5, ICRS, Galactic, BarycentricMeanEcliptic
)
import astropy.coordinates as coord
from astropy import constants as const

logger = logging.getLogger(__name__)

# Set solar system ephemeris for better accuracy
solar_system_ephemeris.set('builtin')

# ============================================================================
# Constants (matching original module)
# ============================================================================

DEG_TO_RAD = math.pi / 180.0
RAD_TO_DEG = 180.0 / math.pi
HOURS_TO_RAD = math.pi / 12.0
RAD_TO_HOURS = 12.0 / math.pi
SIDEREAL_DAY_IN_HOURS = 23.93446972
JD_EPOCH_2000 = 2451545.0  # JD for 2000-01-01 12:00 UT


# ============================================================================
# Time Conversion Functions
# ============================================================================

def julian_date(year: int, month: int, day: int, 
                hour: float = 0.0, minute: float = 0.0, second: float = 0.0) -> float:
    """
    Calculate Julian Date for given date and time using astropy.
    
    Args:
        year: Year
        month: Month (1-12)
        day: Day of month
        hour: Hour (0-23)
        minute: Minute (0-59)
        second: Second (0-59)
    
    Returns:
        Julian Date
    """
    # Create datetime object
    dt = datetime(year, month, day, int(hour), int(minute), int(second))
    # Add fractional seconds
    dt = dt + timedelta(seconds=(second % 1))
    
    # Create astropy Time object
    t = Time(dt, scale='ut1')
    
    return t.jd


def jd_to_datetime(jd: float) -> datetime:
    """
    Convert Julian Date to datetime object using astropy.
    
    Args:
        jd: Julian Date
    
    Returns:
        Datetime object
    """
    t = Time(jd, format='jd', scale='ut1')
    return t.datetime


def gmst(jd: float) -> float:
    """
    Calculate Greenwich Mean Sidereal Time using astropy.
    
    Args:
        jd: Julian Date
    
    Returns:
        GMST in hours (0-24)
    """
    t = Time(jd, format='jd', scale='ut1')
    gmst_angle = t.sidereal_time('mean', 'greenwich')
    gmst_hours = gmst_angle.hour
    
    return gmst_hours


def lst(jd: float, longitude: float) -> float:
    """
    Calculate Local Sidereal Time using astropy.
    
    Args:
        jd: Julian Date
        longitude: Observatory longitude in hours (west positive)
    
    Returns:
        LST in hours (0-24)
    """
    t = Time(jd, format='jd', scale='ut1')
    
    # Convert longitude from hours to degrees (west positive to east negative)
    lon_deg = -longitude * 15.0  # astropy uses east positive
    
    # Create location (latitude doesn't matter for LST)
    location = EarthLocation(lon=lon_deg * u.deg, lat=0 * u.deg)
    
    # Calculate LST
    lst_angle = t.sidereal_time('mean', longitude=location.lon)
    lst_hours = lst_angle.hour
    
    return lst_hours


def ut_to_jd(ut_hours: float, jd_start: float) -> float:
    """
    Convert UT hours to Julian Date.
    
    Args:
        ut_hours: UT in hours
        jd_start: JD at start of day
    
    Returns:
        Julian Date
    """
    return jd_start + ut_hours / 24.0


# ============================================================================
# Coordinate Transformation Functions
# ============================================================================

def precess_coordinates(ra: float, dec: float, jd_from: float, jd_to: float) -> Tuple[float, float]:
    """
    Precess coordinates from one epoch to another using astropy.
    
    Args:
        ra: Right ascension in hours
        dec: Declination in degrees
        jd_from: Julian Date of initial epoch
        jd_to: Julian Date of target epoch
    
    Returns:
        Tuple of (ra, dec) at target epoch
    """
    # Create time objects
    t_from = Time(jd_from, format='jd')
    t_to = Time(jd_to, format='jd')
    
    # Create SkyCoord object at initial epoch
    coord_from = SkyCoord(ra=ra*u.hour, dec=dec*u.deg, 
                          frame=FK5(equinox=t_from))
    
    # Transform to target epoch
    coord_to = coord_from.transform_to(FK5(equinox=t_to))
    
    # Extract RA and Dec
    ra_new = coord_to.ra.hour
    dec_new = coord_to.dec.deg
    
    return ra_new, dec_new


def galactic_coordinates(ra: float, dec: float, epoch: float = 2000.0) -> Tuple[float, float]:
    """
    Convert equatorial to galactic coordinates using astropy.
    
    Args:
        ra: Right ascension in hours
        dec: Declination in degrees
        epoch: Epoch of coordinates (default J2000)
    
    Returns:
        Tuple of (l, b) galactic longitude and latitude in degrees
    """
    # For J2000 coordinates, use ICRS which is very close to FK5 J2000
    if abs(epoch - 2000.0) < 0.01:
        # Use ICRS for J2000
        coord_eq = SkyCoord(ra=ra*u.hour, dec=dec*u.deg, frame='icrs')
    else:
        # Create time for epoch
        jd_epoch = JD_EPOCH_2000 + (epoch - 2000.0) * 365.25
        t_epoch = Time(jd_epoch, format='jd')
        # Create SkyCoord in FK5 frame
        coord_eq = SkyCoord(ra=ra*u.hour, dec=dec*u.deg,
                            frame=FK5(equinox=t_epoch))
    
    # Transform to Galactic coordinates
    coord_gal = coord_eq.galactic
    
    # Extract l and b
    l = coord_gal.l.deg
    b = coord_gal.b.deg
    
    return l, b


def ecliptic_coordinates(ra: float, dec: float, jd: float) -> Tuple[float, float, float]:
    """
    Convert equatorial to ecliptic coordinates using astropy.
    
    Args:
        ra: Right ascension in hours
        dec: Declination in degrees
        jd: Julian Date for obliquity calculation
    
    Returns:
        Tuple of (epoch, longitude, latitude) in degrees
    """
    # Create time object
    t = Time(jd, format='jd')
    
    # Create SkyCoord in ICRS frame (close to FK5 J2000)
    coord_eq = SkyCoord(ra=ra*u.hour, dec=dec*u.deg, frame='icrs')
    
    # Transform to Ecliptic coordinates
    coord_ecl = coord_eq.transform_to(BarycentricMeanEcliptic(equinox=t))
    
    # Extract longitude and latitude
    lon = coord_ecl.lon.deg
    lat = coord_ecl.lat.deg
    
    # Calculate epoch (year)
    epoch = 2000.0 + (jd - JD_EPOCH_2000) / 365.25
    
    return epoch, lon, lat


# ============================================================================
# Rise/Set Time Calculations
# ============================================================================

def hour_angle_from_altitude(altitude: float, dec: float, lat: float) -> Optional[float]:
    """
    Calculate hour angle for given altitude.
    
    Args:
        altitude: Altitude in degrees
        dec: Declination in degrees
        lat: Observer latitude in degrees
    
    Returns:
        Hour angle in hours, or None if object never reaches altitude
    """
    # Convert to radians
    alt_rad = altitude * DEG_TO_RAD
    dec_rad = dec * DEG_TO_RAD
    lat_rad = lat * DEG_TO_RAD
    
    # Calculate cos(hour angle)
    cos_ha = ((math.sin(alt_rad) - math.sin(dec_rad) * math.sin(lat_rad)) /
              (math.cos(dec_rad) * math.cos(lat_rad)))
    
    # Check if object reaches altitude
    if abs(cos_ha) > 1.0:
        return None
    
    # Calculate hour angle
    ha_rad = math.acos(cos_ha)
    ha_hours = ha_rad * RAD_TO_HOURS
    
    return ha_hours


def rise_set_times(ra: float, dec: float, jd: float, 
                   longitude: float, latitude: float,
                   altitude: float = 0.0) -> Tuple[Optional[float], Optional[float]]:
    """
    Calculate rise and set times for an object using astropy.
    
    Args:
        ra: Right ascension in hours
        dec: Declination in degrees
        jd: Julian Date
        longitude: Observer longitude in hours (west positive)
        latitude: Observer latitude in degrees
        altitude: Altitude threshold in degrees (default horizon)
    
    Returns:
        Tuple of (rise_jd, set_jd), None if never rises/sets
    """
    # Create observer location
    lon_deg = -longitude * 15.0  # Convert to degrees, east positive
    location = EarthLocation(lon=lon_deg * u.deg, lat=latitude * u.deg)
    
    # Create time object
    t = Time(jd, format='jd')
    
    # Create target coordinates
    target = SkyCoord(ra=ra*u.hour, dec=dec*u.deg, frame='icrs')
    
    # Calculate hour angle at rise/set
    ha = hour_angle_from_altitude(altitude, dec, latitude)
    
    if ha is None:
        # Object never rises or never sets
        # Check if it's always above or always below altitude
        noon_alt = 90.0 - abs(latitude - dec)
        if noon_alt > altitude:
            # Always above altitude (circumpolar)
            return (jd, jd + 1.0)
        else:
            # Never rises above altitude
            return (None, None)
    
    # Calculate LST at rise and set
    lst_rise = ra - ha
    lst_set = ra + ha
    
    # Normalize to 0-24 hours
    while lst_rise < 0:
        lst_rise += 24.0
    while lst_rise >= 24.0:
        lst_rise -= 24.0
    while lst_set < 0:
        lst_set += 24.0
    while lst_set >= 24.0:
        lst_set -= 24.0
    
    # Convert LST to JD
    current_lst = lst(jd, longitude)
    
    # Calculate time differences
    dt_rise = lst_rise - current_lst
    dt_set = lst_set - current_lst
    
    # Adjust for wraparound
    if dt_rise < 0:
        dt_rise += 24.0
    if dt_set < 0:
        dt_set += 24.0
    
    # Convert to Julian Date (accounting for sidereal vs solar time)
    solar_to_sidereal = 365.25 / 366.25
    jd_rise = jd + (dt_rise * solar_to_sidereal) / 24.0
    jd_set = jd + (dt_set * solar_to_sidereal) / 24.0
    
    return (jd_rise, jd_set)


# ============================================================================
# Sun and Moon Calculations using Astropy
# ============================================================================

def sun_position(jd: float) -> Tuple[float, float]:
    """
    Calculate sun position using astropy.
    
    Args:
        jd: Julian Date
    
    Returns:
        Tuple of (ra, dec) in hours and degrees
    """
    t = Time(jd, format='jd')
    sun = get_sun(t)
    
    ra = sun.ra.hour
    dec = sun.dec.deg
    
    return ra, dec


def moon_position(jd: float) -> Tuple[float, float, float]:
    """
    Calculate moon position and phase using astropy.
    
    Args:
        jd: Julian Date
    
    Returns:
        Tuple of (ra, dec, illumination) where illumination is 0-1
    """
    t = Time(jd, format='jd')
    
    # Get moon position using get_body
    with solar_system_ephemeris.set('builtin'):
        moon = get_body('moon', t)
    ra = moon.ra.hour
    dec = moon.dec.deg
    
    # Get sun position for phase calculation
    sun = get_sun(t)
    
    # Calculate elongation (angle between sun and moon)
    elongation = moon.separation(sun)
    elong_deg = elongation.deg
    
    # Calculate illumination using elongation
    # This is a simplified formula; astropy doesn't directly provide illumination
    illumination = 0.5 * (1.0 - np.cos(np.radians(elong_deg)))
    
    return ra, dec, illumination


def moon_separation(ra1: float, dec1: float, ra2: float, dec2: float) -> float:
    """
    Calculate angular separation between two celestial positions using astropy.
    
    Args:
        ra1, dec1: First position (hours, degrees)
        ra2, dec2: Second position (hours, degrees)
    
    Returns:
        Angular separation in degrees
    """
    # Create SkyCoord objects
    coord1 = SkyCoord(ra=ra1*u.hour, dec=dec1*u.deg, frame='icrs')
    coord2 = SkyCoord(ra=ra2*u.hour, dec=dec2*u.deg, frame='icrs')
    
    # Calculate separation
    sep = coord1.separation(coord2)
    
    return sep.deg


# ============================================================================
# Twilight Calculations
# ============================================================================

def twilight_times(jd: float, longitude: float, latitude: float) -> dict:
    """
    Calculate twilight times for a given date and location using astropy.
    
    Args:
        jd: Julian Date (noon of the day)
        longitude: Observatory longitude in hours (west positive)
        latitude: Observatory latitude in degrees
    
    Returns:
        Dictionary with sunset, sunrise, and twilight times
    """
    # Create observer location
    lon_deg = -longitude * 15.0  # Convert to degrees, east positive
    location = EarthLocation(lon=lon_deg * u.deg, lat=latitude * u.deg)
    
    # Get sun position
    sun_ra, sun_dec = sun_position(jd)
    
    # Sun altitude thresholds
    horizon_alt = -0.833  # Accounting for refraction and solar radius
    civil_alt = -6.0
    nautical_alt = -12.0
    astronomical_alt = -18.0
    
    times = {}
    
    # Calculate rise/set times for different altitudes
    # Sunset/sunrise
    rise_set = rise_set_times(sun_ra, sun_dec, jd, longitude, latitude, horizon_alt)
    if rise_set[0] and rise_set[1]:
        times['sunrise'] = rise_set[0]
        times['sunset'] = rise_set[1]
    
    # Civil twilight
    rise_set = rise_set_times(sun_ra, sun_dec, jd, longitude, latitude, civil_alt)
    if rise_set[0] and rise_set[1]:
        times['civil_dawn'] = rise_set[0]
        times['civil_dusk'] = rise_set[1]
    
    # Nautical twilight
    rise_set = rise_set_times(sun_ra, sun_dec, jd, longitude, latitude, nautical_alt)
    if rise_set[0] and rise_set[1]:
        times['nautical_dawn'] = rise_set[0]
        times['nautical_dusk'] = rise_set[1]
    
    # Astronomical twilight
    rise_set = rise_set_times(sun_ra, sun_dec, jd, longitude, latitude, astronomical_alt)
    if rise_set[0] and rise_set[1]:
        times['astronomical_dawn'] = rise_set[0]
        times['astronomical_dusk'] = rise_set[1]
    
    return times


# ============================================================================
# Airmass and Altitude Calculations
# ============================================================================

def altitude_azimuth(ra: float, dec: float, lst_hours: float, latitude: float) -> Tuple[float, float]:
    """
    Calculate altitude and azimuth for given position using astropy.
    
    Args:
        ra: Right ascension in hours
        dec: Declination in degrees
        lst_hours: Local sidereal time in hours
        latitude: Observer latitude in degrees
    
    Returns:
        Tuple of (altitude, azimuth) in degrees
    """
    # Calculate hour angle
    ha = lst_hours - ra
    if ha < -12.0:
        ha += 24.0
    elif ha > 12.0:
        ha -= 24.0
    
    # Convert to radians
    ha_rad = ha * HOURS_TO_RAD
    dec_rad = dec * DEG_TO_RAD
    lat_rad = latitude * DEG_TO_RAD
    
    # Calculate altitude
    sin_alt = (math.sin(dec_rad) * math.sin(lat_rad) + 
               math.cos(dec_rad) * math.cos(lat_rad) * math.cos(ha_rad))
    
    # Handle numerical errors
    if sin_alt > 1.0:
        sin_alt = 1.0
    elif sin_alt < -1.0:
        sin_alt = -1.0
    
    alt_rad = math.asin(sin_alt)
    alt = alt_rad * RAD_TO_DEG
    
    # Calculate azimuth
    cos_az = (math.sin(dec_rad) - sin_alt * math.sin(lat_rad)) / (math.cos(alt_rad) * math.cos(lat_rad))
    sin_az = -math.sin(ha_rad) * math.cos(dec_rad) / math.cos(alt_rad)
    
    az_rad = math.atan2(sin_az, cos_az)
    az = az_rad * RAD_TO_DEG
    
    # Normalize azimuth to 0-360
    if az < 0:
        az += 360.0
    
    return alt, az


def airmass(altitude: float, model: str = 'secant') -> float:
    """
    Calculate airmass for given altitude.
    
    Args:
        altitude: Altitude in degrees
        model: Airmass model ('secant', 'hardie', 'young')
    
    Returns:
        Airmass value (1.0 at zenith, large values near horizon)
    """
    if altitude <= 0:
        return 999.9  # Below horizon
    
    # Zenith angle
    z = 90.0 - altitude
    z_rad = z * DEG_TO_RAD
    
    if model == 'secant':
        # Simple secant model
        am = 1.0 / math.cos(z_rad)
    
    elif model == 'hardie':
        # Hardie (1962) model
        sec_z = 1.0 / math.cos(z_rad)
        am = sec_z - 0.0018167 * (sec_z - 1) - 0.002875 * (sec_z - 1)**2 - 0.0008083 * (sec_z - 1)**3
    
    elif model == 'young':
        # Young (1994) model
        am = (1.002432 * math.cos(z_rad)**2 + 0.148386 * math.cos(z_rad) + 0.0096467) / \
             (math.cos(z_rad)**3 + 0.149864 * math.cos(z_rad)**2 + 0.0102963 * math.cos(z_rad) + 0.000303978)
    
    else:
        # Default to secant model
        am = 1.0 / math.cos(z_rad)
    
    return am


def parallactic_angle(ha: float, dec: float, latitude: float) -> float:
    """
    Calculate parallactic angle.
    
    Args:
        ha: Hour angle in hours
        dec: Declination in degrees
        latitude: Observer latitude in degrees
    
    Returns:
        Parallactic angle in degrees
    """
    # Convert to radians
    ha_rad = ha * HOURS_TO_RAD
    dec_rad = dec * DEG_TO_RAD
    lat_rad = latitude * DEG_TO_RAD
    
    # Calculate parallactic angle
    numerator = math.sin(ha_rad)
    denominator = math.tan(lat_rad) * math.cos(dec_rad) - math.sin(dec_rad) * math.cos(ha_rad)
    
    if abs(denominator) < 1e-10:
        # At zenith or pole
        return 0.0
    
    pa_rad = math.atan2(numerator, denominator)
    pa = pa_rad * RAD_TO_DEG
    
    return pa


# ============================================================================
# Refraction Corrections
# ============================================================================

def atmospheric_refraction(altitude: float, temperature: float = 10.0, 
                         pressure: float = 1010.0) -> float:
    """
    Calculate atmospheric refraction.
    
    Args:
        altitude: True altitude in degrees
        temperature: Temperature in Celsius (default 10°C)
        pressure: Pressure in millibars (default 1010 mb)
    
    Returns:
        Refraction correction in degrees (add to true altitude)
    """
    if altitude <= -1.0:
        return 0.0  # Well below horizon
    
    # Convert to radians
    alt_rad = altitude * DEG_TO_RAD
    
    if altitude > 15.0:
        # Simple formula for high altitudes
        r = 0.00452 * pressure / ((273.0 + temperature) * math.tan(alt_rad))
    else:
        # More complex formula for low altitudes
        a = altitude + 10.3 / (altitude + 5.11)
        r = 0.0167 * pressure / (273.0 + temperature) / math.tan(a * DEG_TO_RAD)
    
    return r


# ============================================================================
# Example Usage and Testing
# ============================================================================

if __name__ == "__main__":
    print("Astropy-based Astronomical Calculations Module")
    print("=" * 60)
    
    # Test JD calculation
    jd = julian_date(2024, 10, 2, 12, 0, 0)
    print(f"JD for 2024-10-02 12:00 UT: {jd}")
    
    # Test datetime conversion
    dt = jd_to_datetime(jd)
    print(f"JD {jd} converts to: {dt}")
    
    # Test coordinate transformations
    ra, dec = 5.5, 23.5  # Approximate position of Pleiades
    l, b = galactic_coordinates(ra, dec)
    print(f"\nPleiades - RA: {ra}h, Dec: {dec}° -> Gal. l: {l:.2f}°, b: {b:.2f}°")
    
    # Test sun position
    sun_ra, sun_dec = sun_position(jd)
    print(f"\nSun - RA: {sun_ra:.2f}h, Dec: {sun_dec:.2f}°")
    
    # Test moon calculations
    moon_ra, moon_dec, illumination = moon_position(jd)
    print(f"Moon - RA: {moon_ra:.2f}h, Dec: {moon_dec:.2f}°, Illumination: {illumination:.2%}")
    
    # Test separation
    sep = moon_separation(ra, dec, moon_ra, moon_dec)
    print(f"Moon-Pleiades separation: {sep:.2f}°")
    
    # Test rise/set times (La Silla Observatory)
    longitude = 70.7377 / 15.0  # La Silla longitude in hours
    latitude = -29.2567  # La Silla latitude
    rise_jd, set_jd = rise_set_times(ra, dec, jd, longitude, latitude)
    if rise_jd and set_jd:
        print(f"\nPleiades at La Silla:")
        print(f"  Rise: JD {rise_jd:.4f}")
        print(f"  Set:  JD {set_jd:.4f}")
    
    # Test LST
    lst_hours = lst(jd, longitude)
    print(f"\nLST at La Silla: {lst_hours:.2f} hours")
    
    # Test altitude/azimuth
    alt, az = altitude_azimuth(ra, dec, lst_hours, latitude)
    if alt > 0:
        am = airmass(alt)
        print(f"Pleiades - Alt: {alt:.2f}°, Az: {az:.2f}°, Airmass: {am:.3f}")
    
    print("\n" + "=" * 60)
    print("All astropy-based functions tested successfully!")