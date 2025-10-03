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
import tempfile
import os

# Add src to path if running from project root
sys.path.insert(0, 'src')

from scheduler import (
    Scheduler, Config, Field, NightTimes, SiteParams,
    FieldStatus, ShutterCode, SurveyCode, SelectionCode
)
from scheduler_astro import (
    julian_date, lst, moon_position, moon_separation,
    twilight_times, altitude_azimuth, airmass,
    galactic_coordinates, ecliptic_coordinates
)
from scheduler_camera import CameraStatus, FitsHeader as CameraFitsHeader
from scheduler_telescope import TelescopeStatus

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
#   P/O = Pointing offset
#   E = Evening flat
#   M = Morning flat
#   L = Dome flat
#
# Survey codes:
#   0 = None
#   1 = TNO (Trans-Neptunian Objects)
#   2 = SNE (Supernovae)
#   3 = MUSTDO (High priority)
#   4 = LIGO (Gravitational wave follow-up)

# Set filter
FILTER rgzz

# Dark frames (30 second darks, 15 total)
0.000000  0.000000 N  30.00  600.0  15  0

# Focus sequence (7 focus positions)
# Format includes focus increment and default focus for F type
0.000000  0.000000 F  30.00  60.0   7   0  0.05  25.30

# Pointing offset calibration
0.000000  0.000000 P  30.00  60.0   1   0

# Evening flats
0.000000  0.000000 E  5.00   30.0   10  0

# Science fields - TNO survey (paired fields for motion detection)
3.424352  14.300000 Y  60.00  1800.0 3  1  # Field 1
3.474352  14.300000 Y  60.00  1800.0 3  1  # Field 2 (paired with Field 1)
5.234567  -5.432100 Y  60.00  1800.0 3  1  # Field 3
5.284567  -5.432100 Y  60.00  1800.0 3  1  # Field 4 (paired with Field 3)

# Supernova survey fields (longer exposures, wider spacing)
12.345678  25.678900 Y  120.00  3600.0 3  2  # SNe field 1
12.395678  25.678900 Y  120.00  3600.0 3  2  # SNe field 2 (paired)

# Must-do target (high priority, will be observed even if late)
8.123456  10.987654 Y  90.00  2400.0 4  3  # High priority target

# LIGO follow-up field
16.789012  -15.432100 Y  180.00  1200.0 5  4  # LIGO event follow-up

# Morning flats
0.000000  0.000000 M  5.00   30.0   10  0

# Dome flats (can be done anytime)
0.000000  0.000000 L  10.00  30.0   5   0

# End of sequence
"""
    
    with open(filename, 'w') as f:
        f.write(sequence)
    
    logger.info(f"Created sample sequence file: {filename}")
    return filename


def demonstrate_scheduler_initialization():
    """Demonstrate scheduler initialization and configuration"""
    
    print("\n" + "="*60)
    print("SCHEDULER INITIALIZATION")
    print("="*60)
    
    # Create scheduler with custom configuration
    config = Config()
    config.FAKE_RUN = True  # Enable simulation mode
    config.DEBUG = False
    config.MAX_AIRMASS = 2.0
    config.MIN_MOON_SEPARATION = 15.0
    
    scheduler = Scheduler(config)
    
    print(f"\nScheduler Configuration:")
    print(f"  Simulation mode: {config.FAKE_RUN}")
    print(f"  Max airmass: {config.MAX_AIRMASS}")
    print(f"  Min moon separation: {config.MIN_MOON_SEPARATION}°")
    print(f"  Max hour angle: {config.MAX_HOURANGLE} hours")
    print(f"  Declination range: {config.MIN_DEC}° to {config.MAX_DEC}°")
    
    # Load site parameters
    scheduler.load_site_params("DEFAULT")
    
    print(f"\nSite Parameters:")
    print(f"  Site: {scheduler.site.site_name}")
    print(f"  Longitude: {scheduler.site.longitude*15:.4f}° W")
    print(f"  Latitude: {scheduler.site.latitude:.4f}°")
    print(f"  Elevation: {scheduler.site.elevation:.0f} m")
    print(f"  Time zone: {scheduler.site.zone_name} (UTC{-scheduler.site.std_timezone:+.0f})")
    
    return scheduler


def demonstrate_sequence_loading(scheduler: Scheduler):
    """Demonstrate loading and analyzing observation sequences"""
    
    print("\n" + "="*60)
    print("SEQUENCE LOADING AND ANALYSIS")
    print("="*60)
    
    # Create and load sample sequence
    sequence_file = create_sample_sequence_file()
    num_fields = scheduler.load_sequence(sequence_file)
    
    print(f"\nLoaded {num_fields} fields from sequence file")
    print(f"Filter set to: {scheduler.filter_name}")
    
    # Analyze field types
    field_summary = {}
    for field in scheduler.fields:
        shutter_name = field.shutter.name
        survey_name = field.survey_code.name
        key = f"{shutter_name}/{survey_name}"
        
        if key not in field_summary:
            field_summary[key] = {
                'count': 0,
                'total_exposures': 0,
                'total_time': 0
            }
        
        field_summary[key]['count'] += 1
        field_summary[key]['total_exposures'] += field.n_required
        field_summary[key]['total_time'] += field.n_required * field.expt
    
    print("\nField Summary by Type:")
    print("-" * 50)
    print(f"{'Type':<25} {'Fields':>6} {'Exps':>6} {'Time(min)':>10}")
    print("-" * 50)
    
    for key, stats in sorted(field_summary.items()):
        print(f"{key:<25} {stats['count']:>6} {stats['total_exposures']:>6} "
              f"{stats['total_time']*60:>10.1f}")
    
    # Show sample fields
    print("\nSample Field Details:")
    print("-" * 70)
    
    for i in range(min(5, len(scheduler.fields))):
        field = scheduler.fields[i]
        print(f"Field {i:3d}: RA={field.ra:7.4f}h Dec={field.dec:+8.4f}° "
              f"Type={field.shutter.name:12s} "
              f"Exp={field.expt*3600:6.1f}s×{field.n_required:2d} "
              f"Survey={field.survey_code.name}")
    
    # Clean up
    os.remove(sequence_file)
    
    return num_fields


def demonstrate_night_planning(scheduler: Scheduler):
    """Demonstrate night timing calculations and planning"""
    
    print("\n" + "="*60)
    print("NIGHT PLANNING")
    print("="*60)
    
    # Set observation date
    obs_date = datetime(2024, 3, 15)  # Mid-March
    scheduler._init_night_times(obs_date)
    
    night = scheduler.night_times
    
    print(f"\nObservation Date: {obs_date.strftime('%Y-%m-%d')}")
    print(f"\nTwilight Times (UT):")
    print(f"  Sunset:         {night.ut_sunset:6.3f} ({night.jd_sunset:.6f} JD)")
    print(f"  12° twilight:   {night.ut_evening12:6.3f} ({night.jd_evening12:.6f} JD)")
    print(f"  18° twilight:   {night.ut_evening18:6.3f} ({night.jd_evening18:.6f} JD)")
    print(f"  Obs start:      {night.ut_start:6.3f} ({night.jd_start:.6f} JD)")
    print(f"  Obs end:        {night.ut_end:6.3f} ({night.jd_end:.6f} JD)")
    print(f"  18° dawn:       {night.ut_morning18:6.3f} ({night.jd_morning18:.6f} JD)")
    print(f"  12° dawn:       {night.ut_morning12:6.3f} ({night.jd_morning12:.6f} JD)")
    print(f"  Sunrise:        {night.ut_sunrise:6.3f} ({night.jd_sunrise:.6f} JD)")
    
    # Calculate observing window
    obs_hours = (night.jd_end - night.jd_start) * 24
    print(f"\nObserving Window: {obs_hours:.2f} hours")
    
    print(f"\nLST Range:")
    print(f"  Start: {night.lst_start:.3f} hours")
    print(f"  End:   {night.lst_end:.3f} hours")
    
    print(f"\nMoon Information:")
    print(f"  Position: RA={night.ra_moon:.3f}h, Dec={night.dec_moon:.2f}°")
    print(f"  Illumination: {night.percent_moon:.1%}")
    
    return night


def demonstrate_field_initialization(scheduler: Scheduler):
    """Demonstrate field initialization and observability calculations"""
    
    print("\n" + "="*60)
    print("FIELD INITIALIZATION AND OBSERVABILITY")
    print("="*60)
    
    # Initialize fields for the night
    jd = scheduler.night_times.jd_start
    num_observable = scheduler._init_fields(jd)
    
    print(f"\nField Observability Analysis:")
    print(f"  Total fields: {scheduler.num_fields}")
    print(f"  Observable tonight: {num_observable}")
    
    # Analyze observability by type
    observable_by_type = {}
    for field in scheduler.fields:
        key = f"{field.shutter.name}/{field.survey_code.name}"
        if key not in observable_by_type:
            observable_by_type[key] = {'total': 0, 'observable': 0}
        observable_by_type[key]['total'] += 1
        if field.doable:
            observable_by_type[key]['observable'] += 1
    
    print("\nObservability by Type:")
    print("-" * 50)
    print(f"{'Type':<25} {'Observable':>10} {'Total':>10}")
    print("-" * 50)
    
    for key, stats in sorted(observable_by_type.items()):
        print(f"{key:<25} {stats['observable']:>10} / {stats['total']:<10}")
    
    # Show some observable fields
    print("\nSample Observable Fields:")
    print("-" * 80)
    
    count = 0
    for field in scheduler.fields:
        if field.doable and field.shutter == ShutterCode.SKY and count < 5:
            print(f"Field {field.field_number:3d}: "
                  f"RA={field.ra:7.4f}h Dec={field.dec:+8.4f}° "
                  f"Rise={field.jd_rise:.6f} Set={field.jd_set:.6f} "
                  f"TimeUp={field.time_up:.2f}h")
            
            # Show coordinate information
            print(f"  Galactic: l={field.gal_long:.2f}°, b={field.gal_lat:.2f}°")
            print(f"  Ecliptic: λ={field.ecl_long:.2f}°, β={field.ecl_lat:.2f}°")
            count += 1
    
    return num_observable


def demonstrate_field_selection(scheduler: Scheduler):
    """Demonstrate field selection algorithm"""
    
    print("\n" + "="*60)
    print("FIELD SELECTION ALGORITHM")
    print("="*60)
    
    print("\nField Selection Priority Order:")
    print("1. DO_NOW fields (calibrations: darks, flats, focus, offset)")
    print("2. Paired fields (if previous was first of a pair)")
    print("3. MUST_DO fields with least time remaining")
    print("4. Regular fields with least time remaining")
    print("5. Late fields that can have intervals shortened")
    
    # Simulate field selection at different times
    print("\nSimulated Field Selection Through the Night:")
    print("-" * 60)
    
    # Start of night
    jd = scheduler.night_times.jd_start
    scheduler.i_prev = -1  # No previous field
    
    for hour in range(5):  # First 5 hours
        jd_test = jd + hour / 24.0
        ut_test = (jd_test - int(jd_test - 0.5) - 0.5) * 24.0
        
        # Update field statuses
        for field in scheduler.fields:
            scheduler._update_field_status(field, jd_test)
        
        # Get next field
        i = scheduler._get_next_field(jd_test)
        
        if i >= 0:
            field = scheduler.fields[i]
            print(f"Hour {hour}: UT {ut_test:5.2f} - "
                  f"Field {i:3d} ({field.shutter.name:12s}) "
                  f"Status={field.status.name}")
        else:
            print(f"Hour {hour}: UT {ut_test:5.2f} - No fields ready")


def demonstrate_observation_simulation(scheduler: Scheduler):
    """Demonstrate observation simulation"""
    
    print("\n" + "="*60)
    print("OBSERVATION SIMULATION")
    print("="*60)
    
    # Set up for simulation
    scheduler.config.FAKE_RUN = True
    jd = scheduler.night_times.jd_start
    
    print(f"\nSimulating first hour of observations...")
    print("-" * 60)
    
    observations_made = 0
    jd_end = jd + 1.0 / 24.0  # One hour later
    
    while jd < jd_end and observations_made < 10:
        # Get next field
        i = scheduler._get_next_field(jd)
        
        if i >= 0:
            field = scheduler.fields[i]
            ut = (jd - int(jd - 0.5) - 0.5) * 24.0
            
            print(f"\nUT {ut:6.3f}: Observing field {i}")
            print(f"  Type: {field.shutter.name}")
            print(f"  Position: RA={field.ra:.4f}h, Dec={field.dec:.4f}°")
            print(f"  Exposure: {field.expt*3600:.1f}s")
            print(f"  Progress: {field.n_done}/{field.n_required}")
            
            # Simulate observation
            field.n_done += 1
            field.jd_next = jd + field.interval
            
            # Advance time
            jd += field.expt + Config.EXPOSURE_OVERHEAD
            observations_made += 1
            
            # Update previous field
            scheduler.i_prev = i
        else:
            # No field ready, advance time
            jd += Config.LOOP_WAIT_SEC / 86400.0
    
    print(f"\nSimulation complete: {observations_made} observations in first hour")


def demonstrate_focus_and_offset_handling(scheduler: Scheduler):
    """Demonstrate focus and offset sequence handling"""
    
    print("\n" + "="*60)
    print("FOCUS AND OFFSET CALIBRATION")
    print("="*60)
    
    # Focus sequence parameters
    print("\nFocus Sequence Configuration:")
    print(f"  Default focus: {scheduler.focus_default:.2f} mm")
    print(f"  Focus increment: {scheduler.focus_increment:.3f} mm")
    print(f"  Focus range: {Config.MIN_FOCUS:.2f} - {Config.MAX_FOCUS:.2f} mm")
    
    # Find focus field
    focus_field = None
    for field in scheduler.fields:
        if field.shutter == ShutterCode.FOCUS:
            focus_field = field
            break
    
    if focus_field:
        n_focus = focus_field.n_required
        n_half = n_focus // 2
        focus_start = scheduler.focus_default - n_half * scheduler.focus_increment
        
        print(f"\nFocus Positions for {n_focus}-point sequence:")
        for i in range(n_focus):
            focus = focus_start + i * scheduler.focus_increment
            offset = focus - scheduler.focus_default
            marker = " <- default" if abs(offset) < 0.001 else ""
            print(f"  Position {i+1}: {focus:.3f} mm (offset: {offset:+.3f} mm){marker}")
    
    # Offset calibration
    print("\nOffset Calibration:")
    print("  Used to determine telescope pointing offsets")
    print("  Compares actual star positions with expected positions")
    print("  Updates RA and Dec offsets for accurate pointing")
    
    offset_field = None
    for field in scheduler.fields:
        if field.shutter == ShutterCode.OFFSET:
            offset_field = field
            break
    
    if offset_field:
        print(f"  Offset exposure: {offset_field.expt*3600:.1f}s")
        print(f"  Number of offset fields: {offset_field.n_required}")


def demonstrate_observation_record(scheduler: Scheduler):
    """Demonstrate observation record saving and loading"""
    
    print("\n" + "="*60)
    print("OBSERVATION RECORD MANAGEMENT")
    print("="*60)
    
    # Create temporary file for demonstration
    with tempfile.NamedTemporaryFile(suffix='.bin', delete=False) as tmp:
        record_file = tmp.name
    
    try:
        # Simulate some observations
        scheduler.fields[0].n_done = 2
        scheduler.fields[1].n_done = 1
        scheduler.fields[2].n_done = 3  # Complete
        scheduler.fields[2].n_required = 3
        
        # Save record
        scheduler.save_obs_record(record_file)
        print(f"Saved observation record to: {record_file}")
        
        # Create new scheduler and load record
        new_scheduler = Scheduler()
        num_loaded = new_scheduler.load_obs_record(record_file)
        
        print(f"\nLoaded {num_loaded} fields from record")
        
        # Show status
        n_fresh = sum(1 for f in new_scheduler.fields if f.n_done == 0)
        n_started = sum(1 for f in new_scheduler.fields if 0 < f.n_done < f.n_required)
        n_completed = sum(1 for f in new_scheduler.fields if f.n_done == f.n_required)
        
        print(f"Field Status:")
        print(f"  Fresh (not started): {n_fresh}")
        print(f"  In progress: {n_started}")
        print(f"  Completed: {n_completed}")
        
    finally:
        # Clean up
        if os.path.exists(record_file):
            os.remove(record_file)


def main():
    """Main demonstration function"""
    
    print("\n" + "="*80)
    print(" " * 15 + "ASTRONOMICAL OBSERVATION SCHEDULER")
    print(" " * 20 + "Python Module Demonstration")
    print("="*80)
    
    print("\nThis demonstration shows the key features of the Python scheduler")
    print("module, which has been converted from the original C implementation.")
    print("The scheduler manages telescope observations, handling field scheduling,")
    print("telescope control, camera operations, and observation logging.")
    
    # Run demonstrations
    scheduler = demonstrate_scheduler_initialization()
    demonstrate_sequence_loading(scheduler)
    demonstrate_night_planning(scheduler)
    demonstrate_field_initialization(scheduler)
    demonstrate_field_selection(scheduler)
    demonstrate_observation_simulation(scheduler)
    demonstrate_focus_and_offset_handling(scheduler)
    demonstrate_observation_record(scheduler)
    
    print("\n" + "="*80)
    print(" " * 25 + "DEMONSTRATION COMPLETE")
    print("="*80)
    
    print("\nKey Features Demonstrated:")
    print("✓ Scheduler initialization and configuration")
    print("✓ Site parameter management")
    print("✓ Observation sequence loading and parsing")
    print("✓ Night timing calculations (twilight, LST, moon)")
    print("✓ Field observability analysis")
    print("✓ Field selection algorithm with priorities")
    print("✓ Observation simulation")
    print("✓ Focus and offset calibration sequences")
    print("✓ Observation record persistence")
    
    print("\nThe scheduler module provides a complete framework for:")
    print("• Automated telescope observation management")
    print("• Intelligent field scheduling based on constraints")
    print("• Integration with telescope and camera hardware")
    print("• Comprehensive observation logging and tracking")
    print("• Recovery from interruptions via observation records")
    
    print("\nFor production use, disable FAKE_RUN mode to interface with")
    print("actual telescope and camera hardware systems.")


if __name__ == "__main__":
    main()