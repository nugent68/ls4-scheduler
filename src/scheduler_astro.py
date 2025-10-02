"""
Astronomical Calculation Module for Scheduler

This module provides astronomical calculations including:
- Time conversions (UT, LST, JD)
- Coordinate transformations
- Rise/set time calculations
- Moon position and interference checks
- Airmass calculations
"""

import math
import numpy as np
from datetime import datetime, timedelta
from typing import Tuple, Optional
from dataclasses import dataclass
import logging

logger = logging.getLogger(__name__)


# ============================================================================
# Constants
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
    Calculate Julian Date for given date and time.
    
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
    # Convert to decimal hours
    decimal_hour = hour + minute / 60.0 + second / 3600.0
    
    # Adjust for January/February
    if month <= 2:
        year -= 1
        month += 12
    
    # Calculate Julian Date
    a = int(year / 100)
    b = 2 - a + int(a / 4)
    
    jd = (int(365.25 * (year + 4716)) + 
          int(30.6001 * (month + 1)) + 
          day + b - 1524.5)
    jd += decimal_hour / 24.0
    
    return jd


def jd_to_datetime(jd: float) -> datetime:
    """
    Convert Julian Date to datetime object.
    
    Args:
        jd: Julian Date
    
    Returns:
        Datetime object
    """
    # Algorithm from Meeus
    jd_frac = jd + 0.5
    z = int(jd_frac)
    f = jd_frac - z
    
    if z < 2299161:
        a = z
    else:
        alpha = int((z - 1867216.25) / 36524.25)
        a = z + 1 + alpha - int(alpha / 4)
    
    b = a + 1524
    c = int((b - 122.1) / 365.25)
    d = int(365.25 * c)
    e = int((b - d) / 30.6001)
    
    day = b - d - int(30.6001 * e) + f
    month = e - 1 if e < 14 else e - 13
    year = c - 4716 if month > 2 else c - 4715
    
    day_int = int(day)
    day_frac = day - day_int
    hour = int(day_frac * 24)
    minute = int((day_frac * 24 - hour) * 60)
    second = int(((day_frac * 24 - hour) * 60 - minute) * 60)
    
    return datetime(year, month, day_int, hour, minute, second)


def gmst(jd: float) -> float:
    """
    Calculate Greenwich Mean Sidereal Time.
    
    Args:
        jd: Julian Date
    
    Returns:
        GMST in hours (0-24)
    """
    # Time since J2000.0 in Julian centuries
    t = (jd - JD_EPOCH_2000) / 36525.0
    
    # GMST at 0h UT
    gmst0 = (6.697374558 + 
             2400.051336 * t + 
             0.000025862 * t * t)
    
    # Add the time since 0h UT
    ut_hours = (jd - int(jd - 0.5) - 0.5) * 24.0
    gmst_hours = gmst0 + ut_hours * 1.00273790935
    
    # Normalize to 0-24 hours
    while gmst_hours >= 24.0:
        gmst_hours -= 24.0
    while gmst_hours < 0.0:
        gmst_hours += 24.0
    
    return gmst_hours


def lst(jd: float, longitude: float) -> float:
    """
    Calculate Local Sidereal Time.
    
    Args:
        jd: Julian Date
        longitude: Observatory longitude in hours (west positive)
    
    Returns:
        LST in hours (0-24)
    """
    gmst_hours = gmst(jd)
    lst_hours = gmst_hours - longitude
    
    # Normalize to 0-24 hours
    while lst_hours >= 24.0:
        lst_hours -= 24.0
    while lst_hours < 0.0:
        lst_hours += 24.0
    
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
    Precess coordinates from one epoch to another.
    
    Args:
        ra: Right ascension in hours
        dec: Declination in degrees
        jd_from: Julian Date of initial epoch
        jd_to: Julian Date of target epoch
    
    Returns:
        Tuple of (ra, dec) at target epoch
    """
    # Convert to radians
    ra_rad = ra * HOURS_TO_RAD
    dec_rad = dec * DEG_TO_RAD
    
    # Time difference in Julian centuries
    t0 = (jd_from - JD_EPOCH_2000) / 36525.0
    t = (jd_to - jd_from) / 36525.0
    
    # Precession angles (from Meeus)
    zeta = (2306.2181 + 1.39656 * t0) * t + 0.30188 * t * t
    z = (2306.2181 + 1.39656 * t0) * t + 1.09468 * t * t
    theta = (2004.3109 - 0.85330 * t0) * t - 0.42665 * t * t
    
    # Convert to radians
    zeta *= DEG_TO_RAD / 3600.0
    z *= DEG_TO_RAD / 3600.0
    theta *= DEG_TO_RAD / 3600.0
    
    # Calculate precession matrix elements
    cos_dec = math.cos(dec_rad)
    sin_dec = math.sin(dec_rad)
    cos_ra = math.cos(ra_rad)
    sin_ra = math.sin(ra_rad)
    
    # Apply precession
    a = cos_dec * sin_ra
    b = math.cos(theta) * cos_dec * cos_ra - math.sin(theta) * sin_dec
    c = math.sin(theta) * cos_dec * cos_ra + math.cos(theta) * sin_dec
    
    ra_new = math.atan2(a, b) + zeta
    dec_new = math.asin(c)
    
    # Convert back to hours and degrees
    ra_new = ra_new * RAD_TO_HOURS
    dec_new = dec_new * RAD_TO_DEG
    
    # Normalize RA to 0-24 hours
    while ra_new < 0:
        ra_new += 24.0
    while ra_new >= 24.0:
        ra_new -= 24.0
    
    return ra_new, dec_new


def galactic_coordinates(ra: float, dec: float, epoch: float = 2000.0) -> Tuple[float, float]:
    """
    Convert equatorial to galactic coordinates.
    
    Args:
        ra: Right ascension in hours
        dec: Declination in degrees
        epoch: Epoch of coordinates (default J2000)
    
    Returns:
        Tuple of (l, b) galactic longitude and latitude in degrees
    """
    # Convert to J2000 if necessary
    if abs(epoch - 2000.0) > 0.01:
        jd_epoch = JD_EPOCH_2000 + (epoch - 2000.0) * 365.25
        ra, dec = precess_coordinates(ra, dec, jd_epoch, JD_EPOCH_2000)
    
    # Convert to radians
    ra_rad = ra * HOURS_TO_RAD
    dec_rad = dec * DEG_TO_RAD
    
    # Galactic pole and center (J2000)
    ra_gp = 12.8605 * HOURS_TO_RAD  # RA of galactic pole
    dec_gp = 27.1282 * DEG_TO_RAD   # Dec of galactic pole
    l_ncp = 122.932 * DEG_TO_RAD    # Galactic longitude of north celestial pole
    
    # Calculate galactic coordinates
    cos_b = (math.cos(dec_rad) * math.cos(dec_gp) * math.cos(ra_rad - ra_gp) +
             math.sin(dec_rad) * math.sin(dec_gp))
    b = math.asin(cos_b)
    
    sin_l = math.cos(dec_rad) * math.sin(ra_rad - ra_gp) / math.cos(b)
    cos_l = (math.sin(dec_rad) - math.sin(b) * math.sin(dec_gp)) / (math.cos(b) * math.cos(dec_gp))
    
    l = math.atan2(sin_l, cos_l) + l_ncp
    
    # Convert to degrees and normalize
    l = l * RAD_TO_DEG
    b = math.asin(math.sin(dec_rad) * math.sin(dec_gp) + 
                  math.cos(dec_rad) * math.cos(dec_gp) * math.cos(ra_rad - ra_gp)) * RAD_TO_DEG
    
    while l < 0:
        l += 360.0
    while l >= 360.0:
        l -= 360.0
    
    return l, b


def ecliptic_coordinates(ra: float, dec: float, jd: float) -> Tuple[float, float, float]:
    """
    Convert equatorial to ecliptic coordinates.
    
    Args:
        ra: Right ascension in hours
        dec: Declination in degrees
        jd: Julian Date for obliquity calculation
    
    Returns:
        Tuple of (epoch, longitude, latitude) in degrees
    """
    # Calculate obliquity of ecliptic
    t = (jd - JD_EPOCH_2000) / 36525.0
    eps = 23.439291 - 0.0130042 * t  # Mean obliquity
    eps_rad = eps * DEG_TO_RAD
    
    # Convert to radians
    ra_rad = ra * HOURS_TO_RAD
    dec_rad = dec * DEG_TO_RAD
    
    # Calculate ecliptic coordinates
    sin_lon = (math.sin(ra_rad) * math.cos(eps_rad) + 
               math.tan(dec_rad) * math.sin(eps_rad))
    cos_lon = math.cos(ra_rad)
    lon = math.atan2(sin_lon, cos_lon)
    
    lat = math.asin(math.sin(dec_rad) * math.cos(eps_rad) - 
                    math.cos(dec_rad) * math.sin(eps_rad) * math.sin(ra_rad))
    
    # Convert to degrees
    lon = lon * RAD_TO_DEG
    lat = lat * RAD_TO_DEG
    
    # Normalize longitude to 0-360
    while lon < 0:
        lon += 360.0
    while lon >= 360.0:
        lon -= 360.0
    
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
    Calculate rise and set times for an object.
    
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
# Moon Calculations
# ============================================================================

def moon_position(jd: float) -> Tuple[float, float, float]:
    """
    Calculate approximate moon position and phase.
    
    Args:
        jd: Julian Date
    
    Returns:
        Tuple of (ra, dec, illumination) where illumination is 0-1
    """
    # Time since J2000.0 in Julian centuries
    t = (jd - JD_EPOCH_2000) / 36525.0
    
    # Mean elements of lunar orbit
    l0 = 218.316 + 13.176396 * (jd - JD_EPOCH_2000)  # Mean longitude
    m = 134.963 + 13.064993 * (jd - JD_EPOCH_2000)   # Mean anomaly
    f = 93.272 + 13.229350 * (jd - JD_EPOCH_2000)    # Mean distance from ascending node
    
    # Convert to radians
    l0_rad = l0 * DEG_TO_RAD
    m_rad = m * DEG_TO_RAD
    f_rad = f * DEG_TO_RAD
    
    # Corrections
    l = l0 + 6.289 * math.sin(m_rad)
    b = 5.128 * math.sin(f_rad)
    
    # Convert to radians
    l_rad = l * DEG_TO_RAD
    b_rad = b * DEG_TO_RAD
    
    # Obliquity of ecliptic
    eps = 23.439291 - 0.0130042 * t
    eps_rad = eps * DEG_TO_RAD
    
    # Convert to equatorial coordinates
    ra_rad = math.atan2(math.sin(l_rad) * math.cos(eps_rad) - math.tan(b_rad) * math.sin(eps_rad),
                        math.cos(l_rad))
    dec_rad = math.asin(math.sin(b_rad) * math.cos(eps_rad) + 
                        math.cos(b_rad) * math.sin(eps_rad) * math.sin(l_rad))
    
    # Convert to hours and degrees
    ra = ra_rad * RAD_TO_HOURS
    dec = dec_rad * RAD_TO_DEG
    
    # Normalize RA
    while ra < 0:
        ra += 24.0
    while ra >= 24.0:
        ra -= 24.0
    
    # Calculate phase (simplified)
    # Sun's mean longitude
    sun_l = 280.460 + 0.9856474 * (jd - JD_EPOCH_2000)
    sun_l_rad = sun_l * DEG_TO_RAD
    
    # Elongation
    elong = l_rad - sun_l_rad
    
    # Illumination (simplified)
    illumination = 0.5 * (1.0 - math.cos(elong))
    
    return ra, dec, illumination


def moon_separation(ra1: float, dec1: float, ra2: float, dec2: float) -> float:
    """
    Calculate angular separation between two celestial positions.
    
    Args:
        ra1, dec1: First position (hours, degrees)
        ra2, dec2: Second position (hours, degrees)
    
    Returns:
        Angular separation in degrees
    """
    # Convert to radians
    ra1_rad = ra1 * HOURS_TO_RAD
    dec1_rad = dec1 * DEG_TO_RAD
    ra2_rad = ra2 * HOURS_TO_RAD
    dec2_rad = dec2 * DEG_TO_RAD
    
    # Calculate separation using spherical law of cosines
    cos_sep = (math.sin(dec1_rad) * math.sin(dec2_rad) +
               math.cos(dec1_rad) * math.cos(dec2_rad) * math.cos(ra1_rad - ra2_rad))
    
    # Handle numerical errors
    if cos_sep > 1.0:
        cos_sep = 1.0
    elif cos_sep < -1.0:
        cos_sep = -1.0
    
    sep_rad = math.acos(cos_sep)
    sep_deg = sep_rad * RAD_TO_DEG
    
    return sep_deg


# ============================================================================
# Twilight Calculations
# ============================================================================

def twilight_times(jd: float, longitude: float, latitude: float) -> dict:
    """
    Calculate twilight times for a given date and location.
    
    Args:
        jd: Julian Date (noon of the day)
        longitude: Observatory longitude in hours (west positive)
        latitude: Observatory latitude in degrees
    
    Returns:
        Dictionary with sunset, sunrise, and twilight times
    """
    # Sun altitude thresholds
    horizon_alt = -0.833  # Accounting for refraction and solar radius
    civil_alt = -6.0
    nautical_alt = -12.0
    astronomical_alt = -18.0
    
    # Calculate approximate sun position (simplified)
    # This would normally use more accurate solar position algorithm
    n = jd - JD_EPOCH_2000
    L = (280.460 + 0.9856474 * n) % 360.0  # Mean longitude
    g = math.radians((357.528 + 0.9856003 * n) % 360.0)  # Mean anomaly
    
    # Equation of center
    lambda_sun = L + 1.915 * math.sin(g) + 0.020 * math.sin(2 * g)
    
    # Obliquity
    eps = 23.439 - 0.0000004 * n
    
    # Sun's RA and Dec
    lambda_rad = math.radians(lambda_sun)
    eps_rad = math.radians(eps)
    
    ra_sun = math.atan2(math.cos(eps_rad) * math.sin(lambda_rad), math.cos(lambda_rad))
    dec_sun = math.asin(math.sin(eps_rad) * math.sin(lambda_rad))
    
    ra_sun_hours = ra_sun * RAD_TO_HOURS
    dec_sun_deg = dec_sun * RAD_TO_DEG
    
    # Calculate rise/set times for different altitudes
    times = {}
    
    # Sunset/sunrise
    rise_set = rise_set_times(ra_sun_hours, dec_sun_deg, jd, longitude, latitude, horizon_alt)
    if rise_set[0] and rise_set[1]:
        times['sunrise'] = rise_set[0]
        times['sunset'] = rise_set[1]
    
    # Civil twilight
    rise_set = rise_set_times(ra_sun_hours, dec_sun_deg, jd, longitude, latitude, civil_alt)
    if rise_set[0] and rise_set[1]:
        times['civil_dawn'] = rise_set[0]
        times['civil_dusk'] = rise_set[1]
    
    # Nautical twilight
    rise_set = rise_set_times(ra_sun_hours, dec_sun_deg, jd, longitude, latitude, nautical_alt)
    if rise_set[0] and rise_set[1]:
        times['nautical_dawn'] = rise_set[0]
        times['nautical_dusk'] = rise_set[1]
    
    # Astronomical twilight
    rise_set = rise_set_times(ra_sun_hours, dec_sun_deg, jd, longitude, latitude, astronomical_alt)
    if rise_set[0] and rise_set[1]:
        times['astronomical_dawn'] = rise_set[0]
        times['astronomical_dusk'] = rise_set[1]
    
    return times


# ============================================================================
# Airmass and Altitude Calculations
# ============================================================================

def altitude_azimuth(ra: float, dec: float, lst_hours: float, latitude: float) -> Tuple[float, float]:
    """
    Calculate altitude and azimuth for given position.
    
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
    # Test astronomical calculations
    
    # Test JD calculation
    jd = julian_date(2024, 10, 2, 12, 0, 0)
    print(f"JD for 2024-10-02 12:00 UT: {jd}")
    
    # Test coordinate transformations
    ra, dec = 5.5, 23.5  # Approximate position of Pleiades
    l, b = galactic_coordinates(ra, dec)
    print(f"Pleiades - RA: {ra}h, Dec: {dec}° -> Gal. l: {l:.2f}°, b: {b:.2f}°")
    
    # Test rise/set times
    longitude = 70.0 / 15.0  # Boston longitude in hours
    latitude = 42.36  # Boston latitude
    rise_jd, set_jd = rise_set_times(ra, dec, jd, longitude, latitude)
    if rise_jd and set_jd:
        print(f"Rise: JD {rise_jd:.4f}, Set: JD {set_jd:.4f}")
    
    # Test moon calculations
    moon_ra, moon_dec, illumination = moon_position(jd)
    print(f"Moon - RA: {moon_ra:.2f}h, Dec: {moon_dec:.2f}°, Illumination: {illumination:.2%}")
    
    # Test separation
    sep = moon_separation(ra, dec, moon_ra, moon_dec)
    print(f"Moon-Pleiades separation: {sep:.2f}°")
    
    # Test airmass
    lst_hours = lst(jd, longitude)
    alt, az = altitude_azimuth(ra, dec, lst_hours, latitude)
    if alt > 0:
        am = airmass(alt)
        print(f"Altitude: {alt:.2f}°, Azimuth: {az:.2f}°, Airmass: {am:.3f}")