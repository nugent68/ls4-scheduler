"""
Example Usage of the Astronomical Observation Scheduler

This file demonstrates how to use the Python scheduler module to:
1. Set up an observation session
2. Load observation sequences
3. Calculate astronomical times
4. Schedule and execute observations
5. Handle telescope and camera operations
"""

import sys
import logging
from datetime import datetime, timedelta
from pathlib import Path

# Add src to path if running from project root
sys.path.insert(0, 'src')

from scheduler import Scheduler, Config, Field, ShutterCode, SurveyCode
from scheduler_astro import (
    julian_date, lst, moon_position, moon_separation,
    twilight_times, altitude_azimuth, airmass
)

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)


def create_sample_sequence_file(filename: str = "sample_sequence.txt"):
    """Create a sample observation sequence file for testing"""
    
    sequence = """# Sample Observation Sequence File
# Format: RA(hours) Dec(deg) Shutter ExpTime(sec) Interval(sec) NumObs SurveyCode
# 
# Shutter codes:
#   Y = Sky exposure
#   N = Dark exposure  
#   F = Focus sequence
#   P = Pointing offset
#   E = Evening flat
#   M = Morning flat
#   L = Dome flat

# Set filter
FILTER rgzz

# Dark frames (30 second darks, 15 total)
0.000000  0.000000 N  30.00  600.0  15  0

# Focus sequence (7 focus positions)
0.000000  0.000000 F  30.00  60.0   7   0  0.05  25.30

# Evening flats
0.000000  0.000000 E  5.00   30.0   10  0

# Science fields - TNO survey
3.424352  14.300000 Y  60.00  1800.0 3  1  # Field 1
3.474352  14.300000 Y  60.00  1800.0 3  1  # Field 2 (paired)
5.234567  -5.432100 Y  60.00  1800.0 3  1  # Field 3
5.284567  -5.432100 Y  60.00  1800.0 3  1  # Field 4 (paired)

# Supernova survey fields (longer exposures)
12.345678  25.678900 Y  120.00  3600.0 3  2  # SNe field 1
12.395678  25.678900 Y  120.00  3600.0 3  2  # SNe field 2 (paired)

# Must-do target (high priority)
8.123456  10.987654 Y  90.00  2400.0 4  3  # High priority target

# Morning flats
0.000000  0.000000 M  5.00   30.0   10  0

# End of sequence
"""
    
    with open(filename, 'w') as f:
        f.write(sequence)
    
    logger.info(f"Created sample sequence file: {filename}")
    return filename


def demonstrate_astronomical_calculations():
    """Demonstrate astronomical calculation functions"""
    
    print("\n" + "="*60)
    print("ASTRONOMICAL CALCULATIONS DEMONSTRATION")
    print("="*60)
    
    # Set up observatory location (La Silla)
    longitude = 70.7380 / 15.0  # Convert to hours (west positive)
    latitude = -29.2572
    
    # Current date/time
    now = datetime.utcnow()
    jd = julian_date(now.year, now.month, now.day, now.hour, now.minute, now.second)
    
    print(f"\nObservatory: La Silla")
    print(f"Longitude: {longitude*15:.4f}° W")
    print(f"Latitude: {latitude:.4f}°")
    print(f"Current UTC: {now.strftime('%Y-%m-%d %H:%M:%S')}")
    print(f"Julian Date: {jd:.6f}")
    
    # Calculate LST
    lst_hours = lst(jd, longitude)
    print(f"Local Sidereal Time: {lst_hours:.4f} hours")
    
    # Calculate twilight times
    jd_noon = julian_date(now.year, now.month, now.day, 12.0)
    twilights = twilight_times(jd_noon, longitude, latitude)
    
    print("\nTwilight Times (JD):")
    for key, value in twilights.items():
        if value:
            print(f"  {key:20s}: {value:.6f}")
    
    # Moon information
    moon_ra, moon_dec, illumination = moon_position(jd)
    print(f"\nMoon Position:")
    print(f"  RA: {moon_ra:.4f} hours")
    print(f"  Dec: {moon_dec:.4f}°")
    print(f"  Illumination: {illumination:.1%}")
    
    # Sample target
    target_ra = 5.5  # hours
    target_dec = 23.5  # degrees (Pleiades)
    
    print(f"\nTarget: Pleiades")
    print(f"  RA: {target_ra:.4f} hours")
    print(f"  Dec: {target_dec:.4f}°")
    
    # Calculate altitude and azimuth
    alt, az = altitude_azimuth(target_ra, target_dec, lst_hours, latitude)
    print(f"  Altitude: {alt:.2f}°")
    print(f"  Azimuth: {az:.2f}°")
    
    if alt > 0:
        am = airmass(alt)
        print(f"  Airmass: {am:.3f}")
    else:
        print(f"  Target is below horizon")
    
    # Moon separation
    sep = moon_separation(target_ra, target_dec, moon_ra, moon_dec)
    print(f"  Moon separation: {sep:.2f}°")
    
    if sep < Config.MIN_MOON_SEPARATION:
        print(f"  WARNING: Too close to moon (limit: {Config.MIN_MOON_SEPARATION}°)")


def demonstrate_scheduler_operations():
    """Demonstrate scheduler operations"""
    
    print("\n" + "="*60)
    print("SCHEDULER OPERATIONS DEMONSTRATION")
    print("="*60)
    
    # Create scheduler instance
    scheduler = Scheduler()
    
    # Load site parameters
    scheduler.load_site_params("La Silla")
    
    # Create and load sample sequence
    sequence_file = create_sample_sequence_file()
    num_fields = scheduler.load_sequence(sequence_file)
    
    print(f"\nLoaded {num_fields} fields from sequence")
    
    # Display field summary
    print("\nField Summary:")
    print("-" * 50)
    
    field_types = {}
    for field in scheduler.fields:
        shutter_char, shutter_desc = get_shutter_string(field.shutter)
        key = shutter_desc
        if key not in field_types:
            field_types[key] = 0
        field_types[key] += 1
    
    for field_type, count in field_types.items():
        print(f"  {field_type:15s}: {count:3d} fields")
    
    # Display some field details
    print("\nSample Field Details:")
    print("-" * 50)
    
    for i, field in enumerate(scheduler.fields[:5]):
        shutter_char, shutter_desc = get_shutter_string(field.shutter)
        print(f"Field {i:3d}: RA={field.ra:7.4f}h Dec={field.dec:+8.4f}° "
              f"Type={shutter_desc:10s} Exp={field.expt*3600:6.1f}s "
              f"N={field.n_required:2d}")
    
    # Simulate field selection
    print("\nField Selection Simulation:")
    print("-" * 50)
    
    # This would normally use get_next_field() with actual JD and weather
    # For demonstration, we'll show the logic
    
    print("Selection priorities:")
    print("1. DO_NOW fields (darks, flats, focus)")
    print("2. Paired fields (if previous was first of pair)")
    print("3. MUST_DO fields with least time remaining")
    print("4. Regular fields with least time remaining")
    print("5. Fields that need interval shortening")


def get_shutter_string(shutter: ShutterCode):
    """Helper function to get shutter string representation"""
    shutter_map = {
        ShutterCode.BAD: ("?", "unknown"),
        ShutterCode.DARK: ("d", "dark"),
        ShutterCode.SKY: ("s", "sky"),
        ShutterCode.FOCUS: ("f", "focus"),
        ShutterCode.OFFSET: ("p", "offset"),
        ShutterCode.EVENING_FLAT: ("e", "pmskyflat"),
        ShutterCode.MORNING_FLAT: ("m", "amskyflat"),
        ShutterCode.DOME_FLAT: ("l", "domeskyflat"),
    }
    return shutter_map.get(shutter, ("?", "unknown"))


def demonstrate_observation_planning():
    """Demonstrate observation planning features"""
    
    print("\n" + "="*60)
    print("OBSERVATION PLANNING DEMONSTRATION")
    print("="*60)
    
    # Create a sample field
    field = Field()
    field.ra = 5.5  # Pleiades
    field.dec = 23.5
    field.shutter = ShutterCode.SKY
    field.expt = 60.0 / 3600.0  # 60 seconds in hours
    field.interval = 1800.0 / 3600.0  # 30 minutes between observations
    field.n_required = 3
    field.survey_code = SurveyCode.TNO
    
    print(f"Sample Field Planning:")
    print(f"  Position: RA={field.ra:.4f}h, Dec={field.dec:.4f}°")
    print(f"  Exposures: {field.n_required} × {field.expt*3600:.0f}s")
    print(f"  Interval: {field.interval*60:.0f} minutes")
    
    # Calculate observability window
    longitude = 70.7380 / 15.0
    latitude = -29.2572
    
    # Get current JD
    now = datetime.utcnow()
    jd = julian_date(now.year, now.month, now.day, now.hour)
    
    # Calculate when field is observable (simplified)
    lst_hours = lst(jd, longitude)
    ha = lst_hours - field.ra
    if ha < -12:
        ha += 24
    elif ha > 12:
        ha -= 24
    
    alt, az = altitude_azimuth(field.ra, field.dec, lst_hours, latitude)
    
    print(f"\nCurrent Observability:")
    print(f"  Hour Angle: {ha:.2f} hours")
    print(f"  Altitude: {alt:.2f}°")
    print(f"  Azimuth: {az:.2f}°")
    
    if alt > 0:
        am = airmass(alt)
        print(f"  Airmass: {am:.3f}")
        
        if am < Config.MAX_AIRMASS:
            print(f"  Status: OBSERVABLE (airmass < {Config.MAX_AIRMASS})")
        else:
            print(f"  Status: NOT OBSERVABLE (airmass > {Config.MAX_AIRMASS})")
    else:
        print(f"  Status: BELOW HORIZON")
    
    # Calculate total time needed
    total_time = (field.n_required - 1) * field.interval + field.n_required * field.expt
    print(f"\nTime Requirements:")
    print(f"  Total time needed: {total_time*60:.1f} minutes")
    print(f"  Minimum window: {total_time*60:.1f} minutes above airmass limit")


def demonstrate_focus_sequence():
    """Demonstrate focus sequence planning"""
    
    print("\n" + "="*60)
    print("FOCUS SEQUENCE DEMONSTRATION")
    print("="*60)
    
    # Focus parameters
    focus_default = 25.30  # mm
    focus_increment = 0.05  # mm
    n_focus = 7
    
    print(f"Focus Sequence Parameters:")
    print(f"  Default focus: {focus_default:.2f} mm")
    print(f"  Increment: {focus_increment:.3f} mm")
    print(f"  Number of positions: {n_focus}")
    
    # Calculate focus positions
    n_half = n_focus // 2
    focus_start = focus_default - n_half * focus_increment
    
    print(f"\nFocus Positions:")
    for i in range(n_focus):
        focus = focus_start + i * focus_increment
        offset = focus - focus_default
        print(f"  Position {i+1}: {focus:.3f} mm (offset: {offset:+.3f} mm)")
    
    print(f"\nFocus range: {focus_start:.3f} to {focus_start + (n_focus-1)*focus_increment:.3f} mm")


def main():
    """Main demonstration function"""
    
    print("\n" + "="*60)
    print("ASTRONOMICAL OBSERVATION SCHEDULER - PYTHON MODULE")
    print("="*60)
    print("\nThis demonstration shows the key features of the Python")
    print("scheduler module converted from the original C code.")
    
    # Run demonstrations
    demonstrate_astronomical_calculations()
    demonstrate_scheduler_operations()
    demonstrate_observation_planning()
    demonstrate_focus_sequence()
    
    print("\n" + "="*60)
    print("DEMONSTRATION COMPLETE")
    print("="*60)
    print("\nThe Python scheduler module provides:")
    print("• Observation sequence management")
    print("• Astronomical calculations (coordinates, times, airmass)")
    print("• Field prioritization and selection")
    print("• Telescope and camera control interfaces")
    print("• FITS header management")
    print("• Observation logging and record keeping")
    print("\nRefer to the module documentation for detailed API reference.")


if __name__ == "__main__":
    main()