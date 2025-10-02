"""
Astronomical Observation Scheduler Module

This module provides a Python implementation of an astronomical observation scheduler,
converted from the original C code. It manages telescope observations, handling
field scheduling, telescope control, camera operations, and data logging.

Author: Converted from C scheduler by DLR (2007)
Python Version: 3.7+
"""

import os
import sys
import time
import math
import json
import signal
import logging
import pickle
from datetime import datetime, timedelta
from typing import Dict, List, Tuple, Optional, Union, Any
from dataclasses import dataclass, field
from enum import Enum, IntEnum
import numpy as np

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)


# ============================================================================
# Constants and Configuration
# ============================================================================

class Config:
    """Configuration constants for the scheduler"""
    
    # Simulation and debugging
    FAKE_RUN = False
    DEBUG = False
    UT_OFFSET = 0.00
    
    # Telescope settings
    POINTING_CORRECTIONS_ON = False
    TRACKING_CORRECTIONS_ON = False
    USE_TELESCOPE_OFFSETS = False
    DEEP_DITHER_ON = False
    
    # Time constants
    SIDEREAL_DAY_IN_HOURS = 23.93446972
    LST_SEARCH_INCREMENT = 0.00166  # 1 minute in hours
    FAKE_RUN_TIME_STEP = 0.0167     # 1 minute in hours
    
    # Observation constraints
    MAX_AIRMASS = 2.0
    MAX_HOURANGLE = 4.3
    MIN_DEC = -89.0  # degrees
    MAX_DEC = 30.0   # degrees
    MIN_MOON_SEPARATION = 15.0  # degrees
    
    # Exposure settings
    MAX_EXPT = 1000.0  # seconds
    MIN_INTERVAL = 0.0  # hours
    MAX_INTERVAL = 12.0  # hours (43200 seconds)
    LONG_EXPTIME = 1.0  # hours (3600 seconds)
    MAX_BAD_READOUTS = 3
    CLEAR_INTERVAL = 0.1  # hours since last exposure to start clear
    
    # Focus settings
    MIN_FOCUS = 24.0  # mm
    MAX_FOCUS = 28.0  # mm
    MIN_FOCUS_INCREMENT = 0.025  # mm
    MAX_FOCUS_INCREMENT = 0.10  # mm
    MAX_FOCUS_CHANGE = 0.3  # mm
    NOMINAL_FOCUS_START = 25.30  # mm
    NOMINAL_FOCUS_INCREMENT = 0.05  # mm
    NOMINAL_FOCUS_DEFAULT = 25.30  # mm
    NUM_FOCUS_ITERATIONS = 2
    MAX_FOCUS_DEVIATION = 0.05  # mm
    
    # Timing settings
    USE_12DEG_START = True
    STARTUP_TIME = 0.0  # hours after twilight
    MIN_EXECUTION_TIME = 0.029  # hours
    FOCUS_OVERHEAD = 0.00555  # hours (20 seconds)
    SKYFLAT_WAIT_TIME = 0.5 / 24  # days
    DARK_WAIT_TIME = 0.0 / 24  # days
    
    # Dithering settings
    FLAT_DITHER_STEP = 0.002778  # 10 arcsec in degrees
    DEEPSEARCH_DITHER_STEP = 0.001389  # 5 arcsec in degrees
    RA_STEP0 = 0.05  # hours difference in RA between paired fields
    
    # File settings
    HISTORY_FILE = "survey.hist"
    SELECTED_FIELDS_FILE = "fields.completed"
    LOG_OBS_FILE = "log.obs"
    OBS_RECORD_FILE = "scheduler.bin"
    FILENAME_LENGTH = 16
    STR_BUF_LEN = 1024
    
    # Limits
    MAX_OBS_PER_FIELD = 100
    MAX_FIELDS = 500
    MAX_FITS_WORDS = 100
    
    # Mathematical constants
    DEG_TO_RAD = math.pi / 180.0
    RAD_TO_DEG = 180.0 / math.pi
    
    # Camera settings
    NUM_CAMERA_CLEARS = 3
    EXPOSURE_OVERHEAD = 0.005  # hours
    
    # Filter names
    FILTER_NAMES = ["rgzz", "none", "fake", "clear"]


# ============================================================================
# Enumerations
# ============================================================================

class FieldStatus(IntEnum):
    """Field observation status codes"""
    TOO_LATE = -1
    NOT_DOABLE = 0
    READY = 1
    DO_NOW = 2


class ShutterCode(IntEnum):
    """Shutter operation codes"""
    BAD = -1
    DARK = 0
    SKY = 1
    FOCUS = 2
    OFFSET = 3
    EVENING_FLAT = 4
    MORNING_FLAT = 5
    DOME_FLAT = 6
    LIGO = 7


class SurveyCode(IntEnum):
    """Survey type codes"""
    NONE = 0
    TNO = 1
    SNE = 2
    MUSTDO = 3
    LIGO = 4


class SelectionCode(Enum):
    """Field selection priority codes"""
    NOT_SELECTED = 0
    FIRST_DO_NOW_FLAT = 1
    FIRST_DO_NOW_DARK = 2
    FIRST_DO_NOW = 3
    FIRST_READY_PAIR = 4
    FIRST_LATE_PAIR = 5
    FIRST_NOT_READY_LATE_PAIR = 6
    FIRST_NOT_READY_NOT_LATE_PAIR = 7
    LEAST_TIME_LATE_MUST_DO = 8
    LEAST_TIME_READY_MUST_DO = 9
    LEAST_TIME_READY = 10
    MOST_TIME_READY_LATE = 11


class CameraState(IntEnum):
    """Camera controller state codes"""
    NOSTATUS = 0
    UNKNOWN = 1
    IDLE = 2
    EXPOSING = 3
    READOUT_PENDING = 4
    READING = 5
    FETCHING = 6
    FLUSHING = 7
    ERASING = 8
    PURGING = 9
    AUTOCLEAR = 10
    AUTOFLUSH = 11
    POWERON = 12
    POWEROFF = 13
    POWERBAD = 14
    FETCH_PENDING = 15
    ERROR = 16
    ACTIVE = 17
    ERRORED = 18


# ============================================================================
# Data Classes
# ============================================================================

@dataclass
class SiteParams:
    """Site-specific parameters"""
    site_name: str = "DEFAULT"
    longitude: float = 0.0  # West longitude in decimal hours
    latitude: float = 0.0   # North latitude in decimal degrees
    elevation_sea: float = 0.0  # Elevation above sea level
    elevation: float = 0.0  # Height above horizon
    horizon: float = 0.0
    std_timezone: float = 0.0  # Standard time zone offset in hours
    zone_name: str = ""
    zone_abbr: str = ""
    use_dst: int = 0  # 1 for USA DST, 2 for Spanish, negative for south
    jd_dst_begin: float = 0.0
    jd_dst_end: float = 0.0


@dataclass
class NightTimes:
    """Night timing information"""
    jd_sunset: float = 0.0
    jd_sunrise: float = 0.0
    jd_evening12: float = 0.0  # 12-degree twilight
    jd_morning12: float = 0.0
    jd_evening18: float = 0.0  # 18-degree twilight
    jd_morning18: float = 0.0
    jd_start: float = 0.0  # Start of observations
    jd_end: float = 0.0    # End of observations
    
    ut_sunset: float = 0.0
    ut_sunrise: float = 0.0
    ut_evening12: float = 0.0
    ut_morning12: float = 0.0
    ut_evening18: float = 0.0
    ut_morning18: float = 0.0
    ut_start: float = 0.0
    ut_end: float = 0.0
    
    lst_start: float = 0.0
    lst_end: float = 0.0
    lst_evening12: float = 0.0
    lst_morning12: float = 0.0
    lst_evening18: float = 0.0
    lst_morning18: float = 0.0
    
    ra_moon: float = 0.0
    dec_moon: float = 0.0
    percent_moon: float = 0.0


@dataclass
class WeatherInfo:
    """Weather information"""
    temperature: float = 0.0
    humidity: float = 0.0
    wind_speed: float = 0.0
    wind_direction: float = 0.0
    dew_point: float = 0.0
    dome_status: int = -1  # -1 unknown, 0 closed, 1 open


@dataclass
class TelescopeStatus:
    """Telescope status information"""
    lst: float = 0.0  # Local sidereal time in hours
    filter_string: str = ""
    focus: float = Config.NOMINAL_FOCUS_DEFAULT  # Focus setting in mm
    dome_status: int = 0  # 0 closed, 1 open
    ut: float = 0.0  # Universal time in hours
    ra: float = 0.0  # RA pointing in hours
    dec: float = 0.0  # Dec pointing in degrees
    ra_offset: float = 0.0  # RA correction in degrees
    dec_offset: float = 0.0  # Dec correction in degrees
    weather: WeatherInfo = field(default_factory=WeatherInfo)
    update_time: float = 0.0


@dataclass
class CameraStatus:
    """Camera status information"""
    ready: bool = False
    error: bool = False
    error_code: int = 0
    state: str = ""
    comment: str = ""
    date: str = ""
    read_time: float = 0.0
    state_values: Dict[str, bool] = field(default_factory=dict)


@dataclass
class FitsWord:
    """FITS header keyword-value pair"""
    keyword: str
    value: str


@dataclass
class FitsHeader:
    """FITS header information"""
    num_words: int = 0
    words: List[FitsWord] = field(default_factory=list)
    
    def add_word(self, keyword: str, value: str):
        """Add or update a FITS header keyword"""
        for word in self.words:
            if word.keyword == keyword:
                word.value = value
                return
        self.words.append(FitsWord(keyword, value))
        self.num_words = len(self.words)
    
    def get_value(self, keyword: str) -> Optional[str]:
        """Get value for a FITS header keyword"""
        for word in self.words:
            if word.keyword == keyword:
                return word.value
        return None


@dataclass
class Field:
    """Observation field information"""
    # Status and identification
    status: FieldStatus = FieldStatus.NOT_DOABLE
    doable: bool = False
    selection_code: SelectionCode = SelectionCode.NOT_SELECTED
    field_number: int = 0
    line_number: int = 0
    script_line: str = ""
    
    # Coordinates
    ra: float = 0.0  # hours
    dec: float = 0.0  # degrees
    gal_long: float = 0.0  # degrees
    gal_lat: float = 0.0  # degrees
    ecl_long: float = 0.0  # degrees
    ecl_lat: float = 0.0  # degrees
    epoch: float = 0.0
    
    # Observation parameters
    shutter: ShutterCode = ShutterCode.SKY
    expt: float = 0.0  # exposure time in hours
    interval: float = 0.0  # interval between observations in hours
    n_required: int = 0  # number of observations required
    n_done: int = 0  # number of observations completed
    survey_code: SurveyCode = SurveyCode.NONE
    
    # Timing
    ut_rise: float = 0.0
    ut_set: float = 0.0
    jd_rise: float = 0.0
    jd_set: float = 0.0
    jd_next: float = 0.0
    time_up: float = 0.0
    time_required: float = 0.0
    time_left: float = 0.0
    
    # Observation history
    ut: List[float] = field(default_factory=list)
    jd: List[float] = field(default_factory=list)
    lst: List[float] = field(default_factory=list)
    ha: List[float] = field(default_factory=list)
    am: List[float] = field(default_factory=list)
    actual_expt: List[float] = field(default_factory=list)
    filenames: List[str] = field(default_factory=list)
    
    def __post_init__(self):
        """Initialize observation history arrays"""
        max_obs = Config.MAX_OBS_PER_FIELD
        if not self.ut:
            self.ut = [0.0] * max_obs
        if not self.jd:
            self.jd = [0.0] * max_obs
        if not self.lst:
            self.lst = [0.0] * max_obs
        if not self.ha:
            self.ha = [0.0] * max_obs
        if not self.am:
            self.am = [0.0] * max_obs
        if not self.actual_expt:
            self.actual_expt = [0.0] * max_obs
        if not self.filenames:
            self.filenames = [""] * max_obs


# ============================================================================
# Utility Functions
# ============================================================================

def clock_difference(h1: float, h2: float) -> float:
    """
    Calculate the difference between two clock values (0-24 hour range).
    
    Args:
        h1: First hour value
        h2: Second hour value
    
    Returns:
        Difference h2 - h1, constrained to [-12, 12] hours
    """
    dt = h2 - h1
    if dt > 12.0:
        dt -= 24.0
    if dt < -12.0:
        dt += 24.0
    return dt


def get_ha(ra: float, lst: float) -> float:
    """
    Calculate hour angle from RA and LST.
    
    Args:
        ra: Right ascension in hours
        lst: Local sidereal time in hours
    
    Returns:
        Hour angle in hours, range [-12, 12]
    """
    ha = lst - ra
    if ha <= -12.0:
        ha += 24.0
    elif ha >= 12.0:
        ha -= 24.0
    return ha


def get_airmass(ha: float, dec: float, lat: float) -> float:
    """
    Calculate airmass for given hour angle and declination.
    
    Args:
        ha: Hour angle in hours
        dec: Declination in degrees
        lat: Observatory latitude in degrees
    
    Returns:
        Airmass (1.0 at zenith, >1000 if below horizon)
    """
    # Convert to radians
    ha_rad = ha * 15.0 * Config.DEG_TO_RAD
    dec_rad = dec * Config.DEG_TO_RAD
    lat_rad = lat * Config.DEG_TO_RAD
    
    # Calculate altitude
    sin_alt = (math.sin(dec_rad) * math.sin(lat_rad) + 
               math.cos(dec_rad) * math.cos(lat_rad) * math.cos(ha_rad))
    
    if sin_alt <= 0:
        return 1000.0  # Below horizon
    
    return 1.0 / sin_alt


def julian_date(year: int, month: int, day: int, hour: float = 0.0) -> float:
    """
    Calculate Julian Date for given date and time.
    
    Args:
        year: Year
        month: Month (1-12)
        day: Day of month
        hour: Hour of day (decimal)
    
    Returns:
        Julian Date
    """
    if month <= 2:
        year -= 1
        month += 12
    
    a = int(year / 100)
    b = 2 - a + int(a / 4)
    
    jd = int(365.25 * (year + 4716)) + int(30.6001 * (month + 1)) + day + b - 1524.5
    jd += hour / 24.0
    
    return jd


def get_shutter_string(shutter: ShutterCode) -> Tuple[str, str]:
    """
    Get string representations for shutter code.
    
    Args:
        shutter: Shutter code enum value
    
    Returns:
        Tuple of (single_char, description) strings
    """
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


def get_shutter_code(shutter_str: str) -> ShutterCode:
    """
    Convert shutter string to code.
    
    Args:
        shutter_str: Single character shutter string
    
    Returns:
        Corresponding ShutterCode enum value
    """
    shutter_map = {
        "Y": ShutterCode.SKY,
        "y": ShutterCode.SKY,
        "N": ShutterCode.DARK, 
        "n": ShutterCode.DARK,
        "F": ShutterCode.FOCUS,
        "f": ShutterCode.FOCUS,
        "P": ShutterCode.OFFSET,
        "p": ShutterCode.OFFSET,
        "E": ShutterCode.EVENING_FLAT,
        "e": ShutterCode.EVENING_FLAT,
        "M": ShutterCode.MORNING_FLAT,
        "m": ShutterCode.MORNING_FLAT,
        "L": ShutterCode.DOME_FLAT,
        "l": ShutterCode.DOME_FLAT,
    }
    return shutter_map.get(shutter_str, ShutterCode.BAD)


def get_dither(iteration: int, step_size: float) -> Tuple[float, float]:
    """
    Calculate dither offset for flat field observations.
    
    Args:
        iteration: Iteration number (0-based)
        step_size: Dither step size in degrees
    
    Returns:
        Tuple of (ra_dither, dec_dither) in degrees
    """
    if iteration == 0:
        return (0.0, 0.0)
    
    # Determine square size based on iteration
    if iteration <= 8:
        square_size = 3
        i0 = 1
    elif iteration <= 24:
        square_size = 5
        i0 = 9
    elif iteration <= 48:
        square_size = 7
        i0 = 25
    elif iteration <= 80:
        square_size = 9
        i0 = 49
    elif iteration <= 120:
        square_size = 11
        i0 = 81
    else:
        logger.warning(f"Too many dither iterations: {iteration}")
        return (0.0, 0.0)
    
    i = iteration - i0
    side = i // (square_size - 1)
    step_a = square_size // 2
    step_b = i - side * (square_size - 1)
    
    if side == 0:
        ra_dither = step_a
        dec_dither = step_b - step_a
    elif side == 1:
        ra_dither = step_b - step_a + 1
        dec_dither = step_a
    elif side == 2:
        ra_dither = -step_a
        dec_dither = step_b - step_a + 1
    else:
        ra_dither = step_b - step_a
        dec_dither = -step_a
    
    return (ra_dither * step_size, dec_dither * step_size)


# ============================================================================
# Main Scheduler Class
# ============================================================================

class Scheduler:
    """Main astronomical observation scheduler"""
    
    def __init__(self, config: Optional[Config] = None):
        """
        Initialize the scheduler.
        
        Args:
            config: Configuration object (uses defaults if None)
        """
        self.config = config or Config()
        self.fields: List[Field] = []
        self.num_fields = 0
        self.site = SiteParams()
        self.night_times = NightTimes()
        self.telescope_status = TelescopeStatus()
        self.camera_status = CameraStatus()
        self.fits_header = FitsHeader()
        
        # Control flags
        self.pause_flag = False
        self.stop_flag = True
        self.stow_flag = True
        self.focus_done = False
        self.offset_done = False
        self.done = False
        
        # Focus parameters
        self.focus_start = Config.NOMINAL_FOCUS_START
        self.focus_increment = Config.NOMINAL_FOCUS_INCREMENT
        self.focus_default = Config.NOMINAL_FOCUS_DEFAULT
        
        # File handles
        self.hist_file = None
        self.sequence_file = None
        self.log_file = None
        self.obs_record_file = None
        
        # Previous observation tracking
        self.ut_prev = -1000.0
        self.i_prev = -1
        
        # Filter name
        self.filter_name = ""
        
        # Initialize signal handlers
        self._install_signal_handlers()
        
        # Initialize FITS header
        self._init_fits_header()
        
        logger.info("Scheduler initialized")
    
    def _install_signal_handlers(self):
        """Install signal handlers for graceful shutdown and control"""
        signal.signal(signal.SIGTERM, self._sigterm_handler)
        signal.signal(signal.SIGUSR1, self._sigusr1_handler)
        signal.signal(signal.SIGUSR2, self._sigusr2_handler)
    
    def _sigterm_handler(self, signum, frame):
        """Handle termination signal"""
        logger.info("Received SIGTERM, shutting down gracefully")
        self.done = True
        self.cleanup()
        sys.exit(0)
    
    def _sigusr1_handler(self, signum, frame):
        """Handle pause signal"""
        logger.info("Received SIGUSR1, pausing observations")
        self.pause_flag = True
    
    def _sigusr2_handler(self, signum, frame):
        """Handle resume signal"""
        logger.info("Received SIGUSR2, resuming observations")
        self.pause_flag = False
    
    def _init_fits_header(self):
        """Initialize FITS header with default keywords"""
        default_keywords = [
            ("TELESCOP", "LS4"),
            ("INSTRUME", "LS4-CAM"),
            ("OBSERVER", "SCHEDULER"),
            ("OBJECT", "SURVEY"),
            ("EXPTIME", "0.0"),
            ("DATE-OBS", ""),
            ("UT", "0.0"),
            ("LST", "0.0"),
            ("HA", "0.0"),
            ("AIRMASS", "0.0"),
            ("FILTER", ""),
            ("FOCUS", str(self.focus_default)),
            ("RA", "0.0"),
            ("DEC", "0.0"),
            ("EPOCH", "2000.0"),
            ("EQUINOX", "2000.0"),
        ]
        
        for keyword, value in default_keywords:
            self.fits_header.add_word(keyword, value)
    
    def load_site_params(self, site_name: str = "DEFAULT"):
        """
        Load site parameters for the observatory.
        
        Args:
            site_name: Name of the observatory site
        """
        # This would normally load from a configuration file
        # For now, using default values (La Silla observatory)
        self.site.site_name = site_name
        self.site.longitude = 4.714  # West longitude in hours
        self.site.latitude = -29.257  # degrees
        self.site.elevation = 2347.0  # meters
        self.site.elevation_sea = 2347.0
        self.site.horizon = 0.0
        self.site.std_timezone = 4.0  # Chile Standard Time
        self.site.zone_name = "CLT"
        self.site.zone_abbr = "C"
        self.site.use_dst = 0
        
        logger.info(f"Loaded site parameters for {site_name}")
    
    def load_sequence(self, filename: str) -> int:
        """
        Load observation sequence from file.
        
        Args:
            filename: Path to sequence file
        
        Returns:
            Number of fields loaded
        """
        self.fields = []
        self.num_fields = 0
        
        try:
            with open(filename, 'r') as f:
                for line_num, line in enumerate(f, 1):
                    line = line.strip()
                    
                    # Skip comments and empty lines
                    if not line or line.startswith('#'):
                        continue
                    
                    # Check for filter command
                    if line.upper().startswith('FILTER'):
                        parts = line.split()
                        if len(parts) >= 2:
                            self.filter_name = parts[1]
                            logger.info(f"Filter set to: {self.filter_name}")
                        continue
                    
                    # Parse field line
                    parts = line.split()
                    if len(parts) < 7:
                        logger.warning(f"Skipping invalid line {line_num}: {line}")
                        continue
                    
                    try:
                        field_obj = Field()
                        field_obj.field_number = self.num_fields
                        field_obj.line_number = line_num
                        field_obj.script_line = line
                        
                        field_obj.ra = float(parts[0])
                        field_obj.dec = float(parts[1])
                        field_obj.shutter = get_shutter_code(parts[2])
                        field_obj.expt = float(parts[3]) / 3600.0  # Convert to hours
                        field_obj.interval = float(parts[4]) / 3600.0  # Convert to hours
                        field_obj.n_required = int(parts[5])
                        field_obj.survey_code = SurveyCode(int(parts[6]))
                        
                        # Handle focus field parameters
                        if field_obj.shutter == ShutterCode.FOCUS and len(parts) >= 9:
                            self.focus_increment = float(parts[7])
                            self.focus_default = float(parts[8])
                            n1 = field_obj.n_required // 2
                            self.focus_start = self.focus_default - n1 * self.focus_increment
                        
                        # Validate field parameters
                        if self._validate_field(field_obj):
                            self.fields.append(field_obj)
                            self.num_fields += 1
                        else:
                            logger.warning(f"Invalid field parameters at line {line_num}")
                    
                    except (ValueError, IndexError) as e:
                        logger.warning(f"Error parsing line {line_num}: {e}")
                        continue
            
            logger.info(f"Loaded {self.num_fields} fields from {filename}")
            return self.num_fields
        
        except FileNotFoundError:
            logger.error(f"Sequence file not found: {filename}")
            return -1
        except Exception as e:
            logger.error(f"Error loading sequence: {e}")
            return -1
    
    def _validate_field(self, field_obj: Field) -> bool:
        """
        Validate field parameters.
        
        Args:
            field_obj: Field object to validate
        
        Returns:
            True if valid, False otherwise
        """
        if not (0.0 <= field_obj.ra <= 24.0):
            return False
        if not (-90.0 <= field_obj.dec <= 90.0):
            return False
        if field_obj.expt < 0 or field_obj.expt > Config.MAX_EXPT:
            return False
        if field_obj.interval < Config.MIN_INTERVAL or field_obj.interval > Config.MAX_INTERVAL:
            return False
        if field_obj.n_required < 1 or field_obj.n_required > Config.MAX_OBS_PER_FIELD:
            return False
        if field_obj.shutter == ShutterCode.BAD:
            return False
        
        return True
    
    def save_obs_record(self, filename: Optional[str] = None):
        """
        Save observation record to binary file.
        
        Args:
            filename: Output filename (uses default if None)
        """
        filename = filename or Config.OBS_RECORD_FILE
        
        try:
            with open(filename, 'wb') as f:
                # Save metadata
                metadata = {
                    'num_fields': self.num_fields,
                    'timestamp': datetime.now().isoformat(),
                    'site': self.site.site_name,
                    'filter': self.filter_name,
                }
                pickle.dump(metadata, f)
                
                # Save fields
                pickle.dump(self.fields, f)
                
            logger.info(f"Saved observation record to {filename}")
        
        except Exception as e:
            logger.error(f"Error saving observation record: {e}")
    
    def load_obs_record(self, filename: Optional[str] = None) -> int:
        """
        Load observation record from binary file.
        
        Args:
            filename: Input filename (uses default if None)
        
        Returns:
            Number of fields loaded, or -1 on error
        """
        filename = filename or Config.OBS_RECORD_FILE
        
        if not os.path.exists(filename):
            logger.info(f"No previous observation record found at {filename}")
            return 0
        
        try:
            with open(filename, 'rb') as f:
                # Load metadata
                metadata = pickle.load(f)
                
                # Load fields
                self.fields = pickle.load(f)
                self.num_fields = len(self.fields)
                
                # Count observation status
                n_fresh = sum(1 for f in self.fields if f.n_done == 0)
                n_started = sum(1 for f in self.fields if 0 < f.n_done < f.n_required)
                n_completed = sum(1 for f in self.fields if f.n_done == f.n_required)
                
                logger.info(f"Loaded {self.num_fields} fields from {filename}")
                logger.info(f"Status: {n_fresh} fresh, {n_started} started, {n_completed} completed")
                
                return self.num_fields
        
        except Exception as e:
            logger.error(f"Error loading observation record: {e}")
            return -1
    
    def cleanup(self):
        """Clean up resources and close files"""
        # Close file handles
        for file_handle in [self.hist_file, self.sequence_file, self.log_file, self.obs_record_file]:
            if file_handle and not file_handle.closed:
                file_handle.close()
        
        # Save final observation record
        self.save_obs_record()
        
        logger.info("Scheduler cleanup completed")
    
    def run(self, date: datetime, sequence_file: str):
        """
        Main scheduler execution loop.
        
        Args:
            date: Local date for observations
            sequence_file: Path to observation sequence file
        """
        logger.info(f"Starting scheduler for date {date.strftime('%Y-%m-%d')}")
        logger.info(f"Sequence file: {sequence_file}")
        
        # Load or resume observations
        num_fields = self.load_obs_record()
        if num_fields <= 0:
            num_fields = self.load_sequence(sequence_file)
            if num_fields < 1:
                logger.error("No valid fields to observe")
                return
        
        # Initialize night times and field status
        # (This would normally calculate astronomical times)
        # ... implementation continues ...
        
        logger.info("Scheduler run completed")


# ============================================================================
# Example Usage
# ============================================================================

if __name__ == "__main__":
    # Example usage
    scheduler = Scheduler()
    
    # Load site parameters
    scheduler.load_site_params("DEFAULT")
    
    # Load observation sequence
    if len(sys.argv) > 1:
        sequence_file = sys.argv[1]
    else:
        sequence_file = "test_sequence.txt"
    
    # Run scheduler
    observation_date = datetime.now()
    scheduler.run(observation_date, sequence_file)