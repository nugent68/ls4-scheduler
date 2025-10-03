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
from datetime import datetime, timedelta, timezone
from typing import Dict, List, Tuple, Optional, Union, Any
from dataclasses import dataclass, field
from enum import Enum, IntEnum
import numpy as np

# Import scheduler modules
from scheduler_camera import (
    CameraStatus, FitsHeader as CameraFitsHeader, Field as CameraField,
    update_camera_status, clear_camera, take_exposure, wait_camera_readout,
    imprint_fits_header, get_ut, get_jd, get_tm,
    DARK_CODE, SKY_CODE, FOCUS_CODE, OFFSET_CODE, 
    EVENING_FLAT_CODE, MORNING_FLAT_CODE, DOME_FLAT_CODE,
    EXP_MODE_FIRST, EXP_MODE_NEXT, EXP_MODE_SINGLE
)

from scheduler_telescope import (
    TelescopeStatus, Field as TelescopeField,
    update_telescope_status, point_telescope, stop_telescope, stow_telescope,
    set_telescope_focus, focus_telescope, get_telescope_offsets,
    init_telescope_offsets, print_telescope_status
)

from scheduler_astropy import (
    julian_date, lst, galactic_coordinates, ecliptic_coordinates,
    rise_set_times, moon_position, moon_separation, twilight_times,
    altitude_azimuth, airmass
)

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
    USE_12DEG_START = True
    
    # Time constants
    SIDEREAL_DAY_IN_HOURS = 23.93446972
    LST_SEARCH_INCREMENT = 0.00166  # 1 minute in hours
    LOOP_WAIT_SEC = 10  # seconds to wait between loops if no field selected
    STARTUP_TIME = 0.0  # hours after twilight
    MIN_EXECUTION_TIME = 0.029  # hours
    
    # Observation constraints
    MAX_AIRMASS = 2.0
    MAX_HOURANGLE = 4.3
    MIN_DEC = -89.0  # degrees
    MAX_DEC = 30.0   # degrees
    MIN_MOON_SEPARATION = 15.0  # degrees
    
    # Exposure settings
    MAX_EXPT = 1000.0 / 3600.0  # hours
    MIN_INTERVAL = 0.0  # hours
    MAX_INTERVAL = 12.0  # hours
    LONG_EXPTIME = 1.0  # hours
    MAX_BAD_READOUTS = 3
    CLEAR_INTERVAL = 0.1  # hours since last exposure to start clear
    NUM_CAMERA_CLEARS = 3
    EXPOSURE_OVERHEAD = 0.005  # hours
    
    # Focus settings
    MIN_FOCUS = 24.0  # mm
    MAX_FOCUS = 28.0  # mm
    MIN_FOCUS_INCREMENT = 0.025  # mm
    MAX_FOCUS_INCREMENT = 0.10  # mm
    MAX_FOCUS_CHANGE = 0.3  # mm
    NOMINAL_FOCUS_START = 25.30  # mm
    NOMINAL_FOCUS_INCREMENT = 0.05  # mm
    NOMINAL_FOCUS_DEFAULT = 25.30  # mm
    FOCUS_OVERHEAD = 0.00555  # hours (20 seconds)
    
    # Timing settings
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
    
    # Mathematical constants
    DEG_TO_RAD = math.pi / 180.0
    RAD_TO_DEG = 180.0 / math.pi


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
    DARK = DARK_CODE
    SKY = SKY_CODE
    FOCUS = FOCUS_CODE
    OFFSET = OFFSET_CODE
    EVENING_FLAT = EVENING_FLAT_CODE
    MORNING_FLAT = MORNING_FLAT_CODE
    DOME_FLAT = DOME_FLAT_CODE


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
# Main Scheduler Class
# ============================================================================

class Scheduler:
    """Main astronomical observation scheduler"""
    
    def __init__(self, config: Optional[Config] = None):
        """Initialize the scheduler"""
        self.config = config or Config()
        self.fields: List[Field] = []
        self.num_fields = 0
        self.site = SiteParams()
        self.night_times = NightTimes()
        self.telescope_status = TelescopeStatus()
        self.camera_status = CameraStatus()
        self.fits_header = CameraFitsHeader()
        
        # Control flags
        self.pause_flag = False
        self.stop_flag = True
        self.stow_flag = True
        self.focus_done = False
        self.offset_done = False
        self.done = False
        self.bad_weather = False
        self.telescope_ready = False
        
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
        
        # Install signal handlers
        self._install_signal_handlers()
        
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
    
    def run_observation_loop(self, sequence_file: str, date: datetime):
        """
        Main observation loop implementing the while loop from scheduler.c line 496
        
        Args:
            sequence_file: Path to observation sequence file
            date: Local date for observations
        """
        # Initialize night times
        self._init_night_times(date)
        
        # Load or resume observations
        num_fields = self.load_obs_record()
        if num_fields <= 0:
            num_fields = self.load_sequence(sequence_file)
            if num_fields < 1:
                logger.error("No valid fields to observe")
                return
        
        # Open output files
        self._open_output_files()
        
        # Wait for sunset if needed
        if not self.config.FAKE_RUN:
            self._wait_for_sunset()
            
            # Initialize camera and telescope
            if not self._initialize_hardware():
                return
        
        # Initialize fields for observation
        jd = get_jd() if not self.config.FAKE_RUN else self.night_times.jd_start
        num_observable = self._init_fields(jd)
        
        logger.info(f"Starting observations with {num_observable} observable fields")
        
        # Main observation loop (line 496 in scheduler.c)
        while jd < self.night_times.jd_sunrise and not self.done:
            
            # Update time
            if not self.config.FAKE_RUN:
                ut = get_ut()
                jd = get_jd()
            else:
                ut = self.night_times.ut_start + (jd - self.night_times.jd_start) * 24.0
            
            # Check for new fields to add
            self._check_new_fields(sequence_file + ".add", jd)
            
            # Handle pause state
            if self.pause_flag:
                self._handle_pause(ut, jd)
                if self.config.FAKE_RUN:
                    jd += Config.LOOP_WAIT_SEC / 86400.0
                else:
                    time.sleep(Config.LOOP_WAIT_SEC)
                continue
            
            # Check weather and telescope status
            if not self.config.FAKE_RUN:
                self._update_telescope_and_weather()
            
            # Handle focus sequence completion
            if self._check_focus_completion():
                continue
                
            # Handle offset sequence completion
            if self._check_offset_completion():
                continue
            
            # Select next field to observe
            i = self._get_next_field(jd)
            
            if i < 0:
                # No fields ready
                logger.debug(f"UT {ut:9.6f}: No fields ready to observe")
                
                # Stop telescope if needed
                if not self.config.FAKE_RUN and self.telescope_ready:
                    if self.bad_weather and not self.stop_flag:
                        logger.info(f"UT {ut:9.6f}: Stopping telescope due to weather")
                        stop_telescope()
                        self.stop_flag = True
                    elif not self.stop_flag:
                        logger.info(f"UT {ut:9.6f}: Stopping telescope - no fields")
                        stop_telescope()
                        self.stop_flag = True
                
                # Check if observations should end
                if jd > self.night_times.jd_end:
                    logger.info("Ending scheduled observations")
                    self.done = True
                else:
                    # Wait before checking again
                    if self.config.FAKE_RUN:
                        jd += Config.LOOP_WAIT_SEC / 86400.0
                    else:
                        time.sleep(Config.LOOP_WAIT_SEC)
            
            # Observe selected field
            elif (self.fields[i].shutter == ShutterCode.DARK or 
                  self.fields[i].shutter == ShutterCode.DOME_FLAT or
                  (not self.bad_weather and self.telescope_ready)):
                
                # Reset focus/offset flags if needed
                if self.focus_done and self.fields[i].shutter == ShutterCode.FOCUS:
                    self.focus_done = False
                elif self.offset_done and self.fields[i].shutter == ShutterCode.OFFSET:
                    self.offset_done = False
                
                # Determine exposure mode
                if self.i_prev < 0:
                    exp_mode = EXP_MODE_FIRST
                else:
                    exp_mode = EXP_MODE_NEXT
                
                # Observe the field
                dt = self._observe_next_field(i, jd, exp_mode)
                
                if dt is not None:
                    # Save observation record
                    self.save_obs_record()
                    
                    # Print history
                    self._print_history(jd)
                    
                    # Update time and previous field
                    if self.config.FAKE_RUN:
                        jd += dt / 24.0
                    
                    self.i_prev = i
                else:
                    logger.error(f"Error observing field {i}")
                    if not self.config.FAKE_RUN and self.telescope_ready and not self.stop_flag:
                        stop_telescope()
                        self.stop_flag = True
            
            else:
                # Wait for conditions to improve
                if not self.telescope_ready:
                    logger.info("Waiting for telescope...")
                elif self.bad_weather:
                    logger.info("Waiting for dome to open...")
                
                if self.config.FAKE_RUN:
                    jd += Config.LOOP_WAIT_SEC / 86400.0
                else:
                    time.sleep(Config.LOOP_WAIT_SEC)
        
        # End of observations
        logger.info(f"UT {ut:9.6f}: Ending observations")
        
        # Stow telescope if needed
        if not self.config.FAKE_RUN and jd > self.night_times.jd_sunrise:
            logger.info("Stowing telescope")
            stow_telescope()
        
        # Print final statistics
        self._print_final_stats()
    
    def _observe_next_field(self, index: int, jd: float, exp_mode: str) -> Optional[float]:
        """
        Observe the next field
        
        Args:
            index: Field index to observe
            jd: Current Julian Date
            exp_mode: Exposure mode
            
        Returns:
            Time taken in hours, or None on error
        """
        field_obj = self.fields[index]
        wait_flag = False  # Don't wait for readout to overlap with slew
        
        logger.info(f"Observing field {field_obj.field_number} "
                   f"(RA={field_obj.ra:.6f}, Dec={field_obj.dec:.5f}, "
                   f"n_done={field_obj.n_done}/{field_obj.n_required})")
        
        # Point telescope if needed (not for darks/flats)
        if (field_obj.shutter not in [ShutterCode.DARK, ShutterCode.DOME_FLAT] and 
            not self.config.FAKE_RUN):
            
            # Calculate pointing with offsets
            ra_point = field_obj.ra
            dec_point = field_obj.dec
            
            if self.config.USE_TELESCOPE_OFFSETS and field_obj.shutter == ShutterCode.SKY:
                ra_point -= self.telescope_status.ra_offset / 15.0
                dec_point -= self.telescope_status.dec_offset
            
            logger.debug(f"Pointing telescope to {ra_point:.6f}, {dec_point:.5f}")
            if point_telescope(ra_point, dec_point) != 0:
                logger.error("Failed to point telescope")
                return None
            
            self.stop_flag = False
        
        # Update telescope status
        if not self.config.FAKE_RUN and field_obj.shutter not in [ShutterCode.DARK, ShutterCode.DOME_FLAT]:
            if update_telescope_status(self.telescope_status) != 0:
                logger.error("Failed to update telescope status")
                return None
        
        # Set focus for focus sequence
        if field_obj.shutter == ShutterCode.FOCUS and not self.config.FAKE_RUN:
            focus = self.focus_start + self.focus_increment * field_obj.n_done
            logger.info(f"Setting focus to {focus:.5f} mm")
            if set_telescope_focus(focus) != 0:
                logger.error("Failed to set telescope focus")
                return None
        
        # Wait for previous readout if needed
        if not self.config.FAKE_RUN and self.i_prev >= 0:
            if wait_camera_readout(self.camera_status) != 0:
                logger.warning("Bad readout of previous exposure")
                if self.i_prev >= 0 and self.fields[self.i_prev].n_done > 0:
                    self.fields[self.i_prev].n_done -= 1
                    self.fields[self.i_prev].jd_next = jd
        
        # Clear camera if needed
        if not self.config.FAKE_RUN:
            dt_clear = (get_ut() - self.ut_prev) if self.ut_prev > 0 else 1000
            if dt_clear > Config.CLEAR_INTERVAL:
                logger.info("Clearing camera")
                for _ in range(Config.NUM_CAMERA_CLEARS):
                    clear_camera()
        
        # Create camera field object
        cam_field = CameraField(
            expt=field_obj.expt,
            shutter=int(field_obj.shutter),
            script_line=field_obj.script_line,
            n_done=field_obj.n_done,
            n_required=field_obj.n_required
        )
        
        # Take exposure
        if not self.config.FAKE_RUN:
            try:
                result = take_exposure(
                    cam_field,
                    self.fits_header,
                    exposure_override_hours=0,
                    exp_mode=exp_mode,
                    wait_for_readout=wait_flag
                )
                
                # Update field with observation data
                field_obj.ut[field_obj.n_done] = result.ut_hours
                field_obj.jd[field_obj.n_done] = result.jd
                field_obj.actual_expt[field_obj.n_done] = result.actual_exposure_sec / 3600.0
                field_obj.filenames[field_obj.n_done] = result.filename
                
                self.ut_prev = result.ut_hours
                dt = field_obj.expt + Config.EXPOSURE_OVERHEAD
                
            except Exception as e:
                logger.error(f"Exposure failed: {e}")
                return None
        else:
            # Simulate exposure
            dt = field_obj.expt + Config.EXPOSURE_OVERHEAD
            field_obj.ut[field_obj.n_done] = get_ut()
            field_obj.jd[field_obj.n_done] = jd
            field_obj.actual_expt[field_obj.n_done] = field_obj.expt
            field_obj.filenames[field_obj.n_done] = f"sim_{field_obj.field_number}_{field_obj.n_done}"
        
        # Update field status
        field_obj.n_done += 1
        field_obj.jd_next = jd + field_obj.interval
        
        # Log observation
        if self.log_file:
            self._log_observation(field_obj, jd)
        
        logger.info(f"Field {field_obj.field_number}: {field_obj.n_done}/{field_obj.n_required} done, "
                   f"next at JD {field_obj.jd_next:.6f}")
        
        return dt
    
    def _get_next_field(self, jd: float) -> int:
        """
        Select the next field to observe
        
        Args:
            jd: Current Julian Date
            
        Returns:
            Index of selected field, or -1 if none available
        """
        # Update all field statuses
        for field_obj in self.fields:
            self._update_field_status(field_obj, jd)
        
        # Priority 1: DO_NOW fields (darks, flats, focus, offset)
        for i, field_obj in enumerate(self.fields):
            if field_obj.status == FieldStatus.DO_NOW:
                logger.debug(f"Selected DO_NOW field {i}")
                return i
        
        # Priority 2: MUST_DO fields that are ready
        ready_mustdo = [(i, f) for i, f in enumerate(self.fields) 
                        if f.status == FieldStatus.READY and f.survey_code == SurveyCode.MUSTDO]
        
        if ready_mustdo:
            # Choose the one with least time left
            i, _ = min(ready_mustdo, key=lambda x: x[1].time_left)
            logger.debug(f"Selected MUST_DO field {i}")
            return i
        
        # Priority 3: Regular fields that are ready
        ready_fields = [(i, f) for i, f in enumerate(self.fields) 
                        if f.status == FieldStatus.READY]
        
        if ready_fields:
            # Choose the one with least time left
            i, _ = min(ready_fields, key=lambda x: x[1].time_left)
            logger.debug(f"Selected ready field {i}")
            return i
        
        # Priority 4: Late fields that can be shortened
        late_fields = [(i, f) for i, f in enumerate(self.fields) 
                       if f.status == FieldStatus.TOO_LATE and f.doable]
        
        if late_fields:
            # Try to shorten interval for the most urgent one
            i, field_obj = max(late_fields, key=lambda x: x[1].time_left)
            self._shorten_interval(field_obj)
            self._update_field_status(field_obj, jd)
            
            if field_obj.status == FieldStatus.READY:
                logger.debug(f"Selected late field {i} with shortened interval")
                return i
        
        return -1
    
    def _update_field_status(self, field_obj: Field, jd: float):
        """Update field status based on current conditions"""
        # Check if field is doable
        if not field_obj.doable:
            field_obj.status = FieldStatus.NOT_DOABLE
            return
        
        # Check if completed
        if field_obj.n_done >= field_obj.n_required:
            field_obj.doable = False
            field_obj.status = FieldStatus.NOT_DOABLE
            return
        
        # Check if risen
        if jd < field_obj.jd_rise:
            field_obj.status = FieldStatus.NOT_DOABLE
            return
        
        # Check if set
        if jd > field_obj.jd_set:
            field_obj.doable = False
            field_obj.status = FieldStatus.NOT_DOABLE
            return
        
        # Check if ready for next observation
        if field_obj.jd_next - jd > Config.MIN_EXECUTION_TIME / 24.0:
            field_obj.status = FieldStatus.NOT_DOABLE
            return
        
        # Darks and dome flats are always DO_NOW when ready
        if field_obj.shutter in [ShutterCode.DARK, ShutterCode.DOME_FLAT]:
            field_obj.status = FieldStatus.DO_NOW
            return
        
        # Focus, offset, and sky flats are DO_NOW in good weather
        if field_obj.shutter in [ShutterCode.FOCUS, ShutterCode.OFFSET, 
                                 ShutterCode.EVENING_FLAT, ShutterCode.MORNING_FLAT]:
            if not self.bad_weather:
                field_obj.status = FieldStatus.DO_NOW
            else:
                field_obj.status = FieldStatus.NOT_DOABLE
            return
        
        # Regular sky fields - check time constraints
        field_obj.time_required = (field_obj.n_required - field_obj.n_done) * field_obj.interval
        field_obj.time_up = (field_obj.jd_set - jd) * 24.0
        field_obj.time_left = field_obj.time_up - field_obj.time_required
        
        if field_obj.time_left < 0:
            field_obj.status = FieldStatus.TOO_LATE
        else:
            field_obj.status = FieldStatus.READY
    
    def _shorten_interval(self, field_obj: Field):
        """Shorten observation interval for late fields"""
        new_interval = field_obj.time_up / (field_obj.n_required - field_obj.n_done)
        
        if new_interval > Config.MIN_INTERVAL:
            field_obj.interval = new_interval
            field_obj.time_required = new_interval * (field_obj.n_required - field_obj.n_done)
            field_obj.time_left = 0
        else:
            field_obj.doable = False
    
    def _check_focus_completion(self) -> bool:
        """Check if focus sequence is complete and process it"""
        if (self.i_prev >= 0 and not self.focus_done and
            self.fields[self.i_prev].shutter == ShutterCode.FOCUS and
            self.fields[self.i_prev].n_done == self.fields[self.i_prev].n_required):
            
            if not self.config.FAKE_RUN:
                # Wait for readout
                if wait_camera_readout(self.camera_status) != 0:
                    logger.warning("Bad readout of last focus exposure")
                    self.fields[self.i_prev].n_done -= 1
                    return True
                
                # Get and set best focus
                logger.info("Focus sequence complete, determining best focus")
                
                # Create telescope field for focus routine
                tel_field = TelescopeField(
                    field_number=self.fields[self.i_prev].field_number,
                    n_done=self.fields[self.i_prev].n_done,
                    filename="".join(self.fields[self.i_prev].filenames[:self.fields[self.i_prev].n_done])
                )
                
                result = focus_telescope(tel_field, self.telescope_status, self.focus_default)
                if result < 0:
                    logger.error("Unable to focus telescope")
                    self.save_obs_record()
                    sys.exit(-1)
                elif result > 0:
                    logger.warning(f"Bad focus sequence, using default: {self.telescope_status.focus:.5f}")
                else:
                    logger.info(f"Telescope focus set to {self.telescope_status.focus:.5f}")
            
            self.focus_done = True
            return True
        
        return False
    
    def _check_offset_completion(self) -> bool:
        """Check if offset sequence is complete and process it"""
        if (self.i_prev >= 0 and not self.offset_done and
            self.fields[self.i_prev].shutter == ShutterCode.OFFSET and
            self.fields[self.i_prev].n_done == self.fields[self.i_prev].n_required):
            
            if not self.config.FAKE_RUN:
                # Wait for readout
                if wait_camera_readout(self.camera_status) != 0:
                    logger.warning("Bad readout of last offset exposure")
                    self.fields[self.i_prev].n_done -= 1
                    return True
                
                # Get and set telescope offsets
                logger.info("Offset exposure complete, determining telescope offsets")
                
                # Create telescope field for offset routine
                tel_field = TelescopeField(
                    field_number=self.fields[self.i_prev].field_number,
                    n_done=self.fields[self.i_prev].n_done,
                    filename=self.fields[self.i_prev].filenames[self.fields[self.i_prev].n_done - 1]
                )
                
                if get_telescope_offsets(tel_field, self.telescope_status) != 0:
                    logger.warning("Unable to get offsets, using previous values")
                else:
                    logger.info(f"Telescope offsets set to {self.telescope_status.ra_offset:.6f}, "
                               f"{self.telescope_status.dec_offset:.6f} deg")
            
            self.offset_done = True
            return True
        
        return False
    
    def _init_fields(self, jd: float) -> int:
        """Initialize field rise/set times and observability"""
        num_observable = 0
        
        for field_obj in self.fields:
            # Initialize observation tracking
            field_obj.n_done = 0
            field_obj.status = FieldStatus.NOT_DOABLE
            field_obj.selection_code = SelectionCode.NOT_SELECTED
            
            # Calculate galactic coordinates
            field_obj.gal_long, field_obj.gal_lat = galactic_coordinates(
                field_obj.ra, field_obj.dec
            )
            
            # Calculate ecliptic coordinates
            field_obj.epoch, field_obj.ecl_long, field_obj.ecl_lat = ecliptic_coordinates(
                field_obj.ra, field_obj.dec, jd
            )
            
            # Special handling for darks and flats
            if field_obj.shutter == ShutterCode.DARK:
                field_obj.doable = True
                field_obj.jd_rise = self.night_times.jd_start
                field_obj.jd_set = self.night_times.jd_end
                field_obj.jd_next = max(jd, field_obj.jd_rise)
                num_observable += 1
                continue
            
            if field_obj.shutter in [ShutterCode.DOME_FLAT, ShutterCode.FOCUS, ShutterCode.OFFSET]:
                field_obj.doable = True
                field_obj.jd_rise = self.night_times.jd_start
                field_obj.jd_set = self.night_times.jd_end
                field_obj.jd_next = self.night_times.jd_start
                num_observable += 1
                continue
            
            # Calculate rise/set times for sky fields
            rise_jd, set_jd = self._get_field_rise_set(field_obj)
            
            if rise_jd is None or set_jd is None:
                field_obj.doable = False
                continue
            
            field_obj.jd_rise = rise_jd
            field_obj.jd_set = set_jd
            field_obj.time_up = (set_jd - rise_jd) * 24.0
            field_obj.time_required = (field_obj.n_required - 1) * field_obj.interval
            field_obj.time_left = field_obj.time_up - field_obj.time_required
            
            # Check observability constraints
            if field_obj.time_left < 0 and field_obj.survey_code != SurveyCode.MUSTDO:
                field_obj.doable = False
                continue
            
            # Check moon interference for SNe fields
            if field_obj.survey_code == SurveyCode.SNE:
                moon_ra, moon_dec, illumination = moon_position(jd)
                if illumination > 0.5:
                    separation = moon_separation(field_obj.ra, field_obj.dec, moon_ra, moon_dec)
                    if separation < Config.MIN_MOON_SEPARATION:
                        field_obj.doable = False
                        continue
            
            # Check galactic latitude for SNe fields
            if field_obj.survey_code == SurveyCode.SNE and abs(field_obj.gal_lat) < 15.0:
                field_obj.doable = False
                continue
            
            # Field is observable
            field_obj.doable = True
            field_obj.jd_next = max(jd, field_obj.jd_rise)
            num_observable += 1
        
        logger.info(f"Initialized {self.num_fields} fields, {num_observable} are observable")
        return num_observable
    
    def _get_field_rise_set(self, field_obj: Field) -> Tuple[Optional[float], Optional[float]]:
        """Calculate rise and set times for a field considering airmass constraints"""
        # Use airmass constraint to determine altitude threshold
        max_airmass = Config.MAX_AIRMASS
        min_altitude = math.degrees(math.asin(1.0 / max_airmass))
        
        # Get rise/set times
        rise_jd, set_jd = rise_set_times(
            field_obj.ra, field_obj.dec, 
            self.night_times.jd_start,
            self.site.longitude, self.site.latitude,
            min_altitude
        )
        
        # Constrain to observing window
        if rise_jd and rise_jd < self.night_times.jd_start:
            rise_jd = self.night_times.jd_start
        if set_jd and set_jd > self.night_times.jd_end:
            set_jd = self.night_times.jd_end
        
        # Check if field is observable during the night
        if rise_jd and set_jd and rise_jd < set_jd:
            return rise_jd, set_jd
        
        return None, None
    
    def _init_night_times(self, date: datetime):
        """Initialize night timing information"""
        # Calculate JD for local noon
        jd_noon = julian_date(date.year, date.month, date.day, 12, 0, 0)
        
        # Get twilight times
        twilights = twilight_times(jd_noon, self.site.longitude, self.site.latitude)
        
        # Set night times
        self.night_times.jd_sunset = twilights.get('sunset', jd_noon + 0.3)
        self.night_times.jd_sunrise = twilights.get('sunrise', jd_noon + 0.7)
        self.night_times.jd_evening12 = twilights.get('nautical_dusk', self.night_times.jd_sunset + 0.05)
        self.night_times.jd_morning12 = twilights.get('nautical_dawn', self.night_times.jd_sunrise - 0.05)
        self.night_times.jd_evening18 = twilights.get('astronomical_dusk', self.night_times.jd_evening12 + 0.03)
        self.night_times.jd_morning18 = twilights.get('astronomical_dawn', self.night_times.jd_morning12 - 0.03)
        
        # Set observation start/end times
        if self.config.USE_12DEG_START:
            self.night_times.jd_start = self.night_times.jd_evening12 + Config.STARTUP_TIME / 24.0
            self.night_times.jd_end = self.night_times.jd_morning12 - Config.MIN_EXECUTION_TIME / 24.0
        else:
            self.night_times.jd_start = self.night_times.jd_evening18 + Config.STARTUP_TIME / 24.0
            self.night_times.jd_end = self.night_times.jd_morning18 - Config.MIN_EXECUTION_TIME / 24.0
        
        # Calculate UT times (simplified - should account for time zone properly)
        self.night_times.ut_start = (self.night_times.jd_start - int(self.night_times.jd_start - 0.5) - 0.5) * 24.0
        self.night_times.ut_end = (self.night_times.jd_end - int(self.night_times.jd_end - 0.5) - 0.5) * 24.0
        
        # Calculate LST times
        self.night_times.lst_start = lst(self.night_times.jd_start, self.site.longitude)
        self.night_times.lst_end = lst(self.night_times.jd_end, self.site.longitude)
        
        # Get moon position
        moon_ra, moon_dec, illumination = moon_position(self.night_times.jd_start)
        self.night_times.ra_moon = moon_ra
        self.night_times.dec_moon = moon_dec
        self.night_times.percent_moon = illumination
    
    def _wait_for_sunset(self):
        """Wait until after sunset to begin observations"""
        jd = get_jd()
        
        while jd < self.night_times.jd_sunset:
            logger.info(f"Waiting for sunset... (JD {jd:.6f} < {self.night_times.jd_sunset:.6f})")
            time.sleep(60)
            jd = get_jd()
        
        if jd > self.night_times.jd_sunrise:
            logger.info("Sun is already up, exiting")
            sys.exit(0)
        
        logger.info("Sun is down, starting observation program")
    
    def _initialize_hardware(self) -> bool:
        """Initialize camera and telescope hardware"""
        # Check camera status
        logger.info("Checking camera status")
        rc, _ = update_camera_status(self.camera_status)
        if rc != 0:
            logger.error("Cannot update camera status")
            return False
        
        logger.info("Camera is responding")
        
        # Initialize telescope offsets
        logger.info("Initializing telescope offsets")
        if init_telescope_offsets(self.telescope_status) != 0:
            logger.warning("Problem initializing telescope offsets")
        
        # Check telescope status
        logger.info("Checking telescope status")
        if update_telescope_status(self.telescope_status) != 0:
            logger.warning("Telescope status not yet available")
            self.telescope_ready = False
        else:
            print_telescope_status(self.telescope_status)
            self.telescope_ready = True
        
        return True
    
    def _update_telescope_and_weather(self):
        """Update telescope and weather status"""
        if update_telescope_status(self.telescope_status) != 0:
            logger.warning("Cannot update telescope status")
            self.bad_weather = True
            self.telescope_ready = False
        else:
            self.telescope_ready = True
            self.bad_weather = (self.telescope_status.dome_status != 1)
            
            if self.bad_weather and not self.stop_flag:
                logger.info("Bad weather detected, stopping telescope")
                stop_telescope()
                self.stop_flag = True
    
    def _handle_pause(self, ut: float, jd: float):
        """Handle pause state"""
        logger.info(f"UT {ut:9.6f}: Observations paused")
        
        if self.telescope_ready:
            if self.bad_weather and not self.stop_flag:
                logger.info("Stowing telescope due to weather")
                stow_telescope()
                self.stow_flag = True
                self.stop_flag = True
            elif not self.stop_flag:
                logger.info("Stopping telescope")
                stop_telescope()
                self.stop_flag = True
    
    def _check_new_fields(self, filename: str, jd: float):
        """Check for and add new fields to the sequence"""
        if not os.path.exists(filename):
            return
        
        # Load new fields
        new_fields = []
        try:
            with open(filename, 'r') as f:
                # Parse new fields (simplified)
                pass
        except:
            return
        
        # Add to sequence if any
        # (Implementation would go here)
    
    def _open_output_files(self):
        """Open output files for logging"""
        try:
            self.hist_file = open(Config.HISTORY_FILE, 'a')
            self.sequence_file = open(Config.SELECTED_FIELDS_FILE, 'a')
            self.log_file = open(Config.LOG_OBS_FILE, 'a')
        except Exception as e:
            logger.error(f"Failed to open output files: {e}")
    
    def _log_observation(self, field_obj: Field, jd: float):
        """Log observation to file"""
        if self.log_file:
            obs_num = field_obj.n_done - 1
            line = (f"{field_obj.ra:10.6f} {field_obj.dec:10.6f} "
                   f"{field_obj.shutter} {field_obj.n_done} "
                   f"{field_obj.expt * 3600:6.1f} "
                   f"{field_obj.ha[obs_num]:10.6f} "
                   f"{jd:11.6f} "
                   f"{field_obj.actual_expt[obs_num] * 3600:10.6f} "
                   f"{field_obj.filenames[obs_num]} "
                   f"# Field {field_obj.field_number}\n")
            self.log_file.write(line)
            self.log_file.flush()
    
    def _print_history(self, jd: float):
        """Print observation history line"""
        if self.hist_file:
            line = f"{jd - 2450000:12.6f} "
            for field_obj in self.fields:
                if field_obj.n_done == field_obj.n_required:
                    line += "."
                else:
                    line += str(field_obj.n_done)
            line += "\n"
            self.hist_file.write(line)
            self.hist_file.flush()
    
    def _print_final_stats(self):
        """Print final observation statistics"""
        num_completed = sum(1 for f in self.fields if f.n_done == f.n_required)
        num_observable = sum(1 for f in self.fields if f.doable)
        
        logger.info(f"Final statistics: {self.num_fields} fields loaded, "
                   f"{num_observable} observable, {num_completed} completed")
        
        # Write completed fields to file
        if self.sequence_file:
            for field_obj in self.fields:
                if field_obj.n_done == field_obj.n_required:
                    self.sequence_file.write(field_obj.script_line + "\n")
            self.sequence_file.flush()
    
    def load_sequence(self, filename: str) -> int:
        """Load observation sequence from file"""
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
                        
                        # Parse shutter code
                        shutter_str = parts[2].upper()
                        if shutter_str == 'Y':
                            field_obj.shutter = ShutterCode.SKY
                        elif shutter_str == 'N':
                            field_obj.shutter = ShutterCode.DARK
                        elif shutter_str == 'F':
                            field_obj.shutter = ShutterCode.FOCUS
                        elif shutter_str == 'O' or shutter_str == 'P':
                            field_obj.shutter = ShutterCode.OFFSET
                        elif shutter_str == 'E':
                            field_obj.shutter = ShutterCode.EVENING_FLAT
                        elif shutter_str == 'M':
                            field_obj.shutter = ShutterCode.MORNING_FLAT
                        elif shutter_str == 'L':
                            field_obj.shutter = ShutterCode.DOME_FLAT
                        else:
                            field_obj.shutter = ShutterCode.BAD
                        
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
        """Validate field parameters"""
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
    
    def load_site_params(self, site_name: str = "DEFAULT"):
        """Load site parameters for the observatory"""
        self.site.site_name = site_name
        
        # Default to La Silla observatory
        if site_name == "DEFAULT" or site_name == "Fake":
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
    
    def save_obs_record(self, filename: Optional[str] = None):
        """Save observation record to binary file"""
        filename = filename or Config.OBS_RECORD_FILE
        
        try:
            with open(filename, 'wb') as f:
                # Save metadata
                metadata = {
                    'num_fields': self.num_fields,
                    'timestamp': datetime.now(timezone.utc).isoformat(),
                    'site': self.site.site_name,
                    'filter': self.filter_name,
                }
                pickle.dump(metadata, f)
                
                # Save fields
                pickle.dump(self.fields, f)
                
            logger.debug(f"Saved observation record to {filename}")
        
        except Exception as e:
            logger.error(f"Error saving observation record: {e}")
    
    def load_obs_record(self, filename: Optional[str] = None) -> int:
        """Load observation record from binary file"""
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
        for file_handle in [self.hist_file, self.sequence_file, self.log_file]:
            if file_handle and not file_handle.closed:
                file_handle.close()
        
        # Save final observation record
        self.save_obs_record()
        
        logger.info("Scheduler cleanup completed")


# ============================================================================
# Main Entry Point
# ============================================================================

if __name__ == "__main__":
    import argparse
    
    parser = argparse.ArgumentParser(description='Astronomical Observation Scheduler')
    parser.add_argument('sequence_file', help='Observation sequence file')
    parser.add_argument('year', type=int, help='Observation year')
    parser.add_argument('month', type=int, help='Observation month')
    parser.add_argument('day', type=int, help='Observation day')
    parser.add_argument('verbose', type=int, help='Verbosity level (0-2)')
    parser.add_argument('--fake', action='store_true', help='Run in simulation mode')
    
    args = parser.parse_args()
    
    # Configure for fake run if requested
    if args.fake:
        Config.FAKE_RUN = True
    
    # Set verbosity
    if args.verbose > 0:
        logging.getLogger().setLevel(logging.DEBUG)
    
    # Create scheduler
    scheduler = Scheduler()
    
    # Load site parameters
    scheduler.load_site_params("Fake" if Config.FAKE_RUN else "DEFAULT")
    
    # Create observation date
    obs_date = datetime(args.year, args.month, args.day)
    
    try:
        # Run the observation loop
        scheduler.run_observation_loop(args.sequence_file, obs_date)
    except KeyboardInterrupt:
        logger.info("Interrupted by user")
    except Exception as e:
        logger.error(f"Unhandled exception: {e}", exc_info=True)
    finally:
        scheduler.cleanup()
