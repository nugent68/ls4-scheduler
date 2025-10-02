# Python Astronomical Observation Scheduler

## Overview

This is a Python implementation of an astronomical observation scheduler, converted from the original C code. The scheduler manages telescope observations, handling field scheduling, telescope control, camera operations, and data logging for automated astronomical surveys.

## Features

- **Observation Sequence Management**: Load and manage observation sequences from configuration files
- **Astronomical Calculations**: Calculate rise/set times, airmass, moon position, twilight times
- **Field Prioritization**: Intelligent selection of observation targets based on multiple criteria
- **Telescope Control**: Interface with telescope control systems
- **Camera Control**: Manage camera exposures and readouts
- **FITS Header Management**: Handle FITS metadata for observations
- **Observation Logging**: Comprehensive logging and record keeping

## Module Structure

```
src/
├── scheduler.py           # Main scheduler module with core classes
├── scheduler_astro.py     # Astronomical calculations
├── scheduler_camera.py    # Camera control interface (existing)
└── scheduler_telescope.py # Telescope control interface (existing)
```

## Installation

```bash
# Clone the repository
git clone <repository-url>
cd ls4-scheduler

# Install dependencies
pip install numpy

# Optional: Install for development
pip install -e .
```

## Quick Start

```python
from scheduler import Scheduler
from datetime import datetime

# Create scheduler instance
scheduler = Scheduler()

# Load site parameters
scheduler.load_site_params("La Silla")

# Load observation sequence
num_fields = scheduler.load_sequence("observations.txt")

# Run scheduler
observation_date = datetime.now()
scheduler.run(observation_date, "observations.txt")
```

## Core Classes

### `Scheduler`
Main scheduler class that orchestrates observations.

**Key Methods:**
- `load_sequence(filename)`: Load observation sequence from file
- `load_site_params(site_name)`: Load observatory site parameters
- `run(date, sequence_file)`: Execute observation scheduling
- `save_obs_record()`: Save observation state
- `load_obs_record()`: Resume from saved state

### `Field`
Represents an observation field/target.

**Attributes:**
- `ra`, `dec`: Coordinates (hours, degrees)
- `shutter`: Observation type (SKY, DARK, FOCUS, etc.)
- `expt`: Exposure time in hours
- `interval`: Time between observations
- `n_required`: Number of observations needed
- `n_done`: Completed observations
- `survey_code`: Survey type (TNO, SNE, MUSTDO, etc.)

### `Config`
Configuration constants and parameters.

**Key Settings:**
- `MAX_AIRMASS`: Maximum airmass limit (2.0)
- `MAX_HOURANGLE`: Maximum hour angle (4.3 hours)
- `MIN_MOON_SEPARATION`: Minimum moon separation (15°)
- `MIN_DEC`, `MAX_DEC`: Declination limits
- `NOMINAL_FOCUS_*`: Focus settings

## Observation Sequence File Format

```
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

# Dark frames
0.000000  0.000000 N  30.00  600.0  15  0

# Science fields
3.424352  14.300000 Y  60.00  1800.0 3  1
5.234567  -5.432100 Y  60.00  1800.0 3  1
```

## Astronomical Calculations

The `scheduler_astro` module provides comprehensive astronomical calculations:

### Time Functions
- `julian_date()`: Calculate Julian Date
- `lst()`: Local Sidereal Time
- `gmst()`: Greenwich Mean Sidereal Time
- `twilight_times()`: Calculate twilight boundaries

### Coordinate Functions
- `precess_coordinates()`: Precess between epochs
- `galactic_coordinates()`: Convert to galactic coordinates
- `ecliptic_coordinates()`: Convert to ecliptic coordinates
- `altitude_azimuth()`: Calculate alt/az from RA/Dec

### Observability Functions
- `rise_set_times()`: Calculate rise and set times
- `airmass()`: Calculate airmass for altitude
- `moon_position()`: Moon position and phase
- `moon_separation()`: Angular separation from moon

## Field Selection Algorithm

The scheduler uses a sophisticated priority system:

1. **DO_NOW Fields**: Immediate priority (darks, flats, focus)
2. **Paired Fields**: Second field of a pair if first was just observed
3. **MUST_DO Fields**: High priority targets with time constraints
4. **Ready Fields**: Fields ready for observation, selected by:
   - Least time remaining
   - Fewest observations left
5. **Late Fields**: Fields needing interval adjustment

## Enumerations

### `FieldStatus`
- `TOO_LATE`: Not enough time to complete
- `NOT_DOABLE`: Cannot be observed
- `READY`: Ready for observation
- `DO_NOW`: Immediate priority

### `ShutterCode`
- `DARK`: Dark frame (shutter closed)
- `SKY`: Sky exposure (shutter open)
- `FOCUS`: Focus sequence
- `OFFSET`: Pointing offset calibration
- `EVENING_FLAT`: Evening twilight flat
- `MORNING_FLAT`: Morning twilight flat
- `DOME_FLAT`: Dome flat

### `SurveyCode`
- `NONE`: No specific survey
- `TNO`: Trans-Neptunian Object survey
- `SNE`: Supernova survey
- `MUSTDO`: High priority observation
- `LIGO`: LIGO follow-up

## Example Usage

See `scheduler_example.py` for comprehensive examples:

```python
# Run the example demonstrations
python scheduler_example.py
```

This will demonstrate:
- Astronomical calculations
- Scheduler operations
- Observation planning
- Focus sequences

## Logging

The module uses Python's standard logging framework:

```python
import logging

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
```

## File I/O

### Binary Records
- Observation state saved using Python pickle
- Allows resuming interrupted sessions

### Text Files
- **History file**: Survey completion status
- **Log file**: Detailed observation log
- **Sequence file**: Completed observations

## Integration with Existing Systems

The Python scheduler is designed to integrate with existing telescope and camera control systems:

- Uses the same camera interface (`scheduler_camera.py`)
- Uses the same telescope interface (`scheduler_telescope.py`)
- Compatible with existing FITS header keywords
- Maintains same file formats for sequences

## Differences from C Implementation

### Improvements
- Object-oriented design with dataclasses
- Better error handling with exceptions
- Native datetime support
- Comprehensive logging framework
- Type hints for better IDE support
- Pickle-based state persistence

### Compatibility
- Same sequence file format
- Same coordinate systems
- Same selection algorithm
- Same airmass calculations
- Same FITS keywords

## Testing

Run the example script to verify functionality:

```bash
python scheduler_example.py
```

For unit testing (if test suite is added):

```bash
python -m pytest tests/
```

## Performance Considerations

- Python implementation may be slower for large field lists
- Numpy arrays can be used for vectorized calculations
- Consider using numba or cython for performance-critical sections

## Future Enhancements

- [ ] Add unit test suite
- [ ] Implement web-based monitoring interface
- [ ] Add database backend for observation records
- [ ] Integrate with cloud-based queue systems
- [ ] Add machine learning for weather prediction
- [ ] Implement adaptive scheduling algorithms

## Troubleshooting

### Common Issues

1. **Module Import Errors**
   - Ensure src/ is in Python path
   - Install required dependencies

2. **Sequence File Errors**
   - Check file format matches specification
   - Verify coordinate ranges (RA: 0-24h, Dec: -90 to +90°)

3. **Site Parameters**
   - Verify longitude sign (west positive)
   - Check timezone settings

## Contributing

When contributing to the Python scheduler:

1. Follow PEP 8 style guidelines
2. Add type hints to new functions
3. Include docstrings for all public methods
4. Update tests for new features
5. Update this documentation

## License

[Include appropriate license information]

## Authors

- Original C implementation: DLR (2007)
- Python conversion: [Current maintainer]

## References

- Meeus, J. "Astronomical Algorithms"
- USNO Circular 179
- IAU SOFA Library

## Contact

[Contact information for support]