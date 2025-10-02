"""
scheduler_telescope.py

Routines to allow scheduler to open a socket connection
to the telescope controller (questctl on quest17_local), send
commands and read replies.

Converted from scheduler_telescope.c
"""

import socket
import time
import os
import sys
import subprocess
from datetime import datetime, timezone
from typing import Optional, Tuple
from dataclasses import dataclass


# Port definitions
TEL_COMMAND_PORT = 3911  # nighttime
DAYTIME_TEL_COMMAND_PORT = 3912  # daytime

# Command timing
COMMAND_WAIT_TIME = 0.1  # seconds to wait between commands (converted from microseconds)

# Reply status strings
TEL_ERROR_REPLY = "error"
TEL_DONE_REPLY = "ok"

# Telescope Commands
LST_COMMAND = "lst"
OPENDOME_COMMAND = "opendome"
CLOSEDOME_COMMAND = "closedome"
GETFOCUS_COMMAND = "getfocus"
SETFOCUS_COMMAND = "setfocus"
SLAVEDOME_COMMAND = "slavedome"
DOMESTATUS_COMMAND = "domestatus"
STATUS_COMMAND = "status"
WEATHER_COMMAND = "weather"
FILTER_COMMAND = "filter"
POINT_COMMAND = "pointrd"
TRACK_COMMAND = "track"
POSRD_COMMAND = "posrd"
STOW_COMMAND = "stow"
STOPMOUNT_COMMAND = "stopmount"
STOP_COMMAND = "stop"
SET_TRACKING_COMMAND = "settracking"

# Filter definitions
RG610_FILTER = "RG610"
ZZIR_FILTER = "zzir"
RIBU_FILTER = "RIBU"
GZIR_FILTER = "gzir"
CLEAR_FILTER = "clear"

# Timeout values (in seconds)
TELESCOPE_POINT_TIMEOUT_SEC = 300
TELESCOPE_FOCUS_TIMEOUT_SEC = 300
TELESCOPE_COMMAND_TIMEOUT = 300

# Focus related constants
FOCUS_SCRIPT = "~/questops.dir/focus/bin/get_best_focus.csh"
FOCUS_OUTPUT_FILE = "/tmp/best_focus.tmp"
NOMINAL_FOCUS_DEFAULT = 50.0  # Default value, should be defined elsewhere
MIN_FOCUS = 40.0  # Should be defined in scheduler.h
MAX_FOCUS = 60.0  # Should be defined in scheduler.h
MAX_FOCUS_DEVIATION = 0.5  # Should be defined in scheduler.h
MAX_FOCUS_CHANGE = 5.0  # Should be defined in scheduler.h
NUM_FOCUS_ITERATIONS = 2  # Should be defined in scheduler.h

# Telescope offset related constants
TELESCOPE_OFFSETS_FILE = "/home/observer/telescope_offsets.dat"
OFFSET_SCRIPT = "/home/observer/palomar/scripts/get_telescope_offsets.csh"
RA_OFFSET_MIN = -1.00  # minimum RA pointing offset (deg)
RA_OFFSET_MAX = 1.00   # maximum RA pointing offset (deg)
DEC_OFFSET_MIN = -1.00  # minimum DEC pointing offset (deg)
DEC_OFFSET_MAX = 1.00   # maximum DEC pointing offset (deg)

# Configuration flags
USE_TELESCOPE_OFFSETS = True
FAKE_RUN = False
UT_OFFSET = 0.0  # Hours offset for debugging

# Buffer sizes
MAXBUFSIZE = 4096
STR_BUF_LEN = 1024
FILENAME_LENGTH = 256  # Should be defined elsewhere

# Global variables (would be better as a class or module state)
verbose = False
verbose1 = False
stop_flag = False
stow_flag = False
host_name = "localhost"  # Default, should be set properly


@dataclass
class WeatherInfo:
    """Weather information structure"""
    temperature: float = 0.0
    humidity: float = 0.0
    wind_speed: float = 0.0
    wind_direction: float = 0.0
    dew_point: float = 0.0


@dataclass
class TelescopeStatus:
    """Telescope status information"""
    ut: float = 0.0
    lst: float = 0.0
    ra: float = 0.0
    dec: float = 0.0
    dome_status: int = 0  # 0=closed, 1=open
    focus: float = NOMINAL_FOCUS_DEFAULT
    filter_string: str = "UNKNOWN"
    weather: WeatherInfo = None
    ra_offset: float = 0.0
    dec_offset: float = 0.0
    
    def __post_init__(self):
        if self.weather is None:
            self.weather = WeatherInfo()


@dataclass
class Field:
    """Field information structure"""
    field_number: int = 0
    n_done: int = 0
    filename: str = ""


def init_telescope_offsets(status: TelescopeStatus) -> int:
    """
    Read in default telescope pointing offsets from TELESCOPE_OFFSETS_FILE
    
    Args:
        status: TelescopeStatus object to update
        
    Returns:
        0 on success, -1 on failure
    """
    # Set offsets to 0 as default
    status.ra_offset = 0.0
    status.dec_offset = 0.0
    
    if verbose:
        print("init_telescope_offsets: Reading default offsets", file=sys.stderr)
        sys.stderr.flush()
    
    if USE_TELESCOPE_OFFSETS:
        try:
            with open(TELESCOPE_OFFSETS_FILE, 'r') as f:
                line = f.readline()
                parts = line.strip().split()
                if len(parts) >= 2:
                    prev_ra_offset = float(parts[0])
                    prev_dec_offset = float(parts[1])
                else:
                    print("init_telescope_offsets: can't read previous offsets", file=sys.stderr)
                    sys.stderr.flush()
                    return -1
        except (IOError, ValueError) as e:
            print(f"init_telescope_offsets: can't open file {TELESCOPE_OFFSETS_FILE}", file=sys.stderr)
            sys.stderr.flush()
            return -1
    else:
        print("init_telescope_offsets: ignoring offsets file, assuming 0 offsets", file=sys.stderr)
        sys.stderr.flush()
        prev_ra_offset = 0.0
        prev_dec_offset = 0.0
    
    if verbose:
        print(f"init_telescope_offsets: previous offsets are {prev_ra_offset:8.6f} {prev_dec_offset:8.6f}", 
              file=sys.stderr)
    
    if (prev_ra_offset < RA_OFFSET_MIN or prev_ra_offset > RA_OFFSET_MAX or
        prev_dec_offset < DEC_OFFSET_MIN or prev_dec_offset > DEC_OFFSET_MAX):
        print("init_telescope_offset: previous offsets out of range.", file=sys.stderr)
        return -1
    
    # Set ra and dec offsets to previous values as new default
    status.ra_offset = prev_ra_offset
    status.dec_offset = prev_dec_offset
    
    return 0


def get_telescope_offsets(field: Field, status: TelescopeStatus) -> int:
    """
    Use system call to OFFSET_PROGRAM to determine offsets to telescope pointing
    
    Args:
        field: Field object with filename
        status: TelescopeStatus object to update
        
    Returns:
        0 on success, -1 if offsets cannot be determined
    """
    if verbose:
        print("get_telescope_offsets: initialize offsets from stored values", file=sys.stderr)
        sys.stderr.flush()
    
    if init_telescope_offsets(status) != 0:
        print("get_telescope_offsets: problem reading stored offsets. Using defaults", file=sys.stderr)
        sys.stderr.flush()
    
    if verbose:
        print(f"get_telescope_offsets: initializing default offsets to {status.ra_offset:8.6f} {status.dec_offset:8.6f}",
              file=sys.stderr)
        sys.stderr.flush()
    
    # Form system command for offset script
    command_string = f"{OFFSET_SCRIPT} {field.filename}\n"
    
    if verbose:
        print(f"get_telescope_offsets: {command_string}", file=sys.stderr)
        sys.stderr.flush()
    
    if not FAKE_RUN:
        # Run offset script
        result = subprocess.run(command_string, shell=True, capture_output=True)
        if result.returncode != 0:
            print("get_telescope_offsets: system command unsuccessful", file=sys.stderr)
            print(f"get_telescope_offsets: Assuming default offset values {status.ra_offset:8.6f} {status.dec_offset:8.6f}",
                  file=sys.stderr)
            sys.stderr.flush()
            return -1
        
        if verbose:
            print("get_telescope_offsets: Reading new offsets", file=sys.stderr)
            sys.stderr.flush()
        
        # Reopen TELESCOPE_OFFSETS_FILE
        try:
            with open(TELESCOPE_OFFSETS_FILE, 'r') as f:
                line = f.readline()
                parts = line.strip().split()
                if len(parts) >= 2:
                    ra_offset = float(parts[0])
                    dec_offset = float(parts[1])
                else:
                    print("get_telescope_offsets: can't read new offsets", file=sys.stderr)
                    sys.stderr.flush()
                    return -1
        except (IOError, ValueError) as e:
            print(f"get_telescope_offsets: can't open file {TELESCOPE_OFFSETS_FILE}", file=sys.stderr)
            sys.stderr.flush()
            return -1
        
        if verbose:
            print(f"get_telescope_offsets: new offsets are {ra_offset:8.6f} {dec_offset:8.6f}", file=sys.stderr)
        
        if (ra_offset < RA_OFFSET_MIN or ra_offset > RA_OFFSET_MAX or
            dec_offset < DEC_OFFSET_MIN or dec_offset > DEC_OFFSET_MAX):
            print(f"get_telescope_offset: new offsets out of range. Substituting default values {status.ra_offset:8.5f} {status.dec_offset:8.5f}",
                  file=sys.stderr)
            return -1
        else:
            status.ra_offset = ra_offset
            status.dec_offset = dec_offset
    
    print(f"get_telescope_offset: setting telescope offsets to {status.ra_offset:8.5f} {status.dec_offset:8.5f}",
          file=sys.stderr)
    sys.stderr.flush()
    
    return 0


def get_median_focus(filename: str) -> float:
    """
    Get median focus value from focus output file
    
    Args:
        filename: Path to focus output file
        
    Returns:
        Median focus value, or -1.0 on error
    """
    try:
        with open(filename, 'r') as f:
            lines = f.readlines()
    except IOError:
        print(f"get_median_focus: could not open file {filename}", file=sys.stderr)
        sys.stderr.flush()
        return -1.0
    
    focus_values = []
    for line in lines:
        if "best focus:" in line:
            parts = line.split()
            try:
                # Find the value after "best focus:"
                idx = parts.index("focus:") + 1
                if idx < len(parts):
                    value = float(parts[idx])
                    focus_values.append(value)
                    if verbose:
                        print(f"get_median_focus: value {len(focus_values)} is {value:8.5f}", file=sys.stderr)
                        sys.stderr.flush()
            except (ValueError, IndexError):
                continue
            
            if len(focus_values) >= 20:
                print("get_median_focus: too many focus values", file=sys.stderr)
                return -1.0
    
    if len(focus_values) == 0:
        print("get_median_focus: no focus values", file=sys.stderr)
        sys.stderr.flush()
        return -1.0
    
    if len(focus_values) == 1:
        return focus_values[0]
    
    if len(focus_values) == 2:
        return (focus_values[0] + focus_values[1]) / 2.0
    
    # Sort values and find median
    focus_values.sort()
    n = len(focus_values)
    if n % 2 == 0:
        median = (focus_values[n//2 - 1] + focus_values[n//2]) / 2.0
    else:
        median = focus_values[n//2]
    
    return median


def focus_telescope(field: Field, status: TelescopeStatus, focus_default: float) -> int:
    """
    Use system call to FOCUS_PROGRAM to determine best focus
    
    Args:
        field: Field object with exposure information
        status: TelescopeStatus object to update
        focus_default: Default focus value
        
    Returns:
        0 on success, -1 if telescope won't focus, +1 if image sequence is bad
    """
    if verbose:
        print(f"focus_telescope: Field {field.field_number}, n_done {field.n_done}", file=sys.stderr)
        sys.stderr.flush()
    
    # Form system command for focus script
    command_parts = [FOCUS_SCRIPT]
    for i in range(field.n_done):
        # Extract filename from field.filename at position i*FILENAME_LENGTH
        start_idx = i * FILENAME_LENGTH
        end_idx = start_idx + FILENAME_LENGTH
        fname = field.filename[start_idx:end_idx].rstrip('\0')
        command_parts.append(fname)
    
    command_string = ' '.join(command_parts) + '\n'
    
    if verbose:
        print(f"focus_telescope: {command_string}", file=sys.stderr)
        sys.stderr.flush()
    
    if FAKE_RUN:
        status.focus = focus_default
    else:
        # Run focus script
        result = subprocess.run(command_string, shell=True, capture_output=True)
        if result.returncode != 0:
            print("focus_telescope: system command unsuccessful", file=sys.stderr)
            sys.stderr.flush()
        
        median = get_median_focus(FOCUS_OUTPUT_FILE)
        if median <= 0:
            print("focus_telescope: could not get focus", file=sys.stderr)
            focus = focus_default
        elif median < MIN_FOCUS or median > MAX_FOCUS:
            print(f"focus_telescope: median out of range: {median:8.5f}", file=sys.stderr)
            focus = focus_default
        elif abs(median - focus_default) > MAX_FOCUS_CHANGE:
            print(f"focus_telescope: unexpected change of focus: {median:8.5f}", file=sys.stderr)
            focus = focus_default
        else:
            focus = median
            print(f"focus_telescope: best focus is {focus:8.5f} mm", file=sys.stderr)
        
        print(f"focus_telescope: setting focus to {focus:8.5f} mm", file=sys.stderr)
        sys.stderr.flush()
        
        if set_telescope_focus(focus) != 0:
            print("focus_telescope: could not set telescope focus", file=sys.stderr)
            return -1
        
        if verbose:
            print("focus_telescope: updating telescope status", file=sys.stderr)
            sys.stderr.flush()
        
        if update_telescope_status(status) != 0:
            print("focus_telescope: could not update telescope status", file=sys.stderr)
            return -1
    
    print(f"focus_telescope: telescope focus now set at {status.focus:8.5f} mm", file=sys.stderr)
    
    return 0


def stow_telescope() -> int:
    """
    Stow the telescope
    
    Returns:
        0 on success, -1 on failure
    """
    global stop_flag, stow_flag
    
    if verbose:
        print("stow_telescope: stowing telescope", file=sys.stderr)
        sys.stderr.flush()
    
    command = STOW_COMMAND
    reply = do_telescope_command(command, TELESCOPE_POINT_TIMEOUT_SEC, host_name)
    
    if reply is None:
        print(f"stow_telescope: stow error: reply : {reply}", file=sys.stderr)
        return -1
    else:
        if verbose:
            print("stow_telescope: stow command successful", file=sys.stderr)
        stow_flag = True
        stop_flag = True
    
    return 0


def get_telescope_focus() -> Tuple[int, float]:
    """
    Get current telescope focus
    
    Returns:
        Tuple of (status, focus) where status is 0 on success, -1 on failure
    """
    focus = NOMINAL_FOCUS_DEFAULT
    
    reply = do_telescope_command(GETFOCUS_COMMAND, TELESCOPE_COMMAND_TIMEOUT, host_name)
    
    if reply is None:
        print("get_telescope_focus: error getting focus", file=sys.stderr)
        sys.stderr.flush()
        return -1, focus
    elif TEL_DONE_REPLY in reply:
        parts = reply.split()
        if len(parts) >= 2:
            try:
                focus = float(parts[1])
                return 0, focus
            except ValueError:
                pass
    
    print("get_telescope_focus: bad reply from telescope", file=sys.stderr)
    print(f"get_telescope_focus: reply: {reply}", file=sys.stderr)
    return -1, focus


def set_telescope_focus(focus: float) -> int:
    """
    Set telescope focus
    
    Args:
        focus: Target focus value
        
    Returns:
        0 on success, -1 on failure
    """
    if focus < MIN_FOCUS or focus > MAX_FOCUS - MAX_FOCUS_DEVIATION:
        print(f"set_telescope_focus: focus out of range: {focus:8.5f}", file=sys.stderr)
        return -1
    
    # First get the current focus
    status, focus1 = get_telescope_focus()
    if status != 0:
        print("set_telescope_focus: error reading resulting focus", file=sys.stderr)
        return -1
    elif verbose:
        print(f"set_telescope_focus: current focus is {focus1:8.5f}", file=sys.stderr)
        print(f"set_telescope_focus: target focus is {focus:8.5f}", file=sys.stderr)
    
    if focus < focus1:
        focus1 = focus1 + MAX_FOCUS_DEVIATION
        if verbose:
            print(f"set_telescope_focus: advancing focus to {focus1:8.5f} before a decrement", file=sys.stderr)
            sys.stderr.flush()
        
        command = f"{SETFOCUS_COMMAND} {focus1:9.5f}"
        reply = do_telescope_command(command, TELESCOPE_FOCUS_TIMEOUT_SEC, host_name)
        
        if reply is None:
            print(f"set_telescope_focus: setfocus reply error: reply : {reply}", file=sys.stderr)
            return -1
        elif verbose:
            print("set_telescope_focus: setfocus successful", file=sys.stderr)
        
        status, focus1 = get_telescope_focus()
        if status != 0:
            print("set_telescope_focus: error reading resulting focus", file=sys.stderr)
            return -1
        elif verbose:
            print(f"set_telescope_focus: focus now {focus1:8.5f}", file=sys.stderr)
    
    # Now go to desired focus, repeat NUM_FOCUS_ITERATIONS times
    if verbose:
        print(f"set_telescope_focus: now setting to target focus {focus:8.5f}", file=sys.stderr)
        sys.stderr.flush()
    
    for i in range(1, NUM_FOCUS_ITERATIONS + 1):
        if verbose and i > 1:
            print("set_telescope_focus: setting to target focus again", file=sys.stderr)
            sys.stderr.flush()
        
        command = f"{SETFOCUS_COMMAND} {focus:9.5f}"
        reply = do_telescope_command(command, TELESCOPE_FOCUS_TIMEOUT_SEC, host_name)
        
        if reply is None:
            print(f"set_telescope_focus: setfocus reply error: reply : {reply}", file=sys.stderr)
            return -1
        elif verbose:
            print("set_telescope_focus: command successful", file=sys.stderr)
        
        status, focus1 = get_telescope_focus()
        if status != 0:
            print("set_telescope_focus: error reading resulting focus", file=sys.stderr)
            return -1
        elif verbose:
            print(f"set_telescope_focus: focus now {focus1:8.5f}", file=sys.stderr)
    
    if abs(focus1 - focus) > MAX_FOCUS_DEVIATION:
        print(f"set_telescope_focus: unable to set focus to {focus:8.5f}. Current focus is {focus1:8.5f}",
              file=sys.stderr)
        sys.stderr.flush()
        return -1
    
    return 0


def stop_telescope() -> int:
    """
    Stop the telescope
    
    Returns:
        0 on success, -1 on failure
    """
    global stop_flag
    
    if verbose:
        print("stop_telescope: stopping telescope", file=sys.stderr)
        sys.stderr.flush()
    
    command = STOP_COMMAND
    reply = do_telescope_command(command, TELESCOPE_POINT_TIMEOUT_SEC, host_name)
    
    if reply is None:
        print(f"stop_telescope: stop error: reply : {reply}", file=sys.stderr)
        return -1
    else:
        if verbose:
            print("stop_telescope: stop command successful", file=sys.stderr)
        stop_flag = True
    
    return 0


def point_telescope(ra: float, dec: float, ra_rate: float = 0.0, dec_rate: float = 0.0) -> int:
    """
    Point the telescope
    
    Args:
        ra: Right ascension (hours)
        dec: Declination (degrees)
        ra_rate: RA tracking rate
        dec_rate: Dec tracking rate
        
    Returns:
        0 on success, -1 on failure
    """
    global stop_flag
    
    if ra > 24.0:
        ra = ra - 24.0
    
    command = f"{TRACK_COMMAND} {ra:9.6f} {dec:9.5f}"
    reply = do_telescope_command(command, TELESCOPE_POINT_TIMEOUT_SEC, host_name)
    
    if reply is None:
        print(f"point_telescope: pointing error: reply : {reply}", file=sys.stderr)
        return -1
    else:
        if verbose:
            print("point_telescope: pointing command successful", file=sys.stderr)
        stop_flag = False
    
    if ra_rate != 0.0 or dec_rate != 0.0:
        command = f"{SET_TRACKING_COMMAND} {ra_rate:9.6f} {dec_rate:9.6f}"
        reply = do_telescope_command(command, TELESCOPE_POINT_TIMEOUT_SEC, host_name)
        
        if reply is None:
            print(f"point_telescope: set_tracking error: reply : {reply}", file=sys.stderr)
            return -1
        else:
            if verbose:
                print("point_telescope: set_tracking command successful", file=sys.stderr)
            stop_flag = False
    
    return 0


def update_telescope_status(status: TelescopeStatus) -> int:
    """
    Update telescope status
    
    Args:
        status: TelescopeStatus object to update
        
    Returns:
        0 on success, -1 on failure
    """
    status.ut = get_ut()
    
    # Get dome status
    reply = do_telescope_command(DOMESTATUS_COMMAND, TELESCOPE_COMMAND_TIMEOUT, host_name)
    if reply is None:
        print("update_telescope_status: error getting domestatus", file=sys.stderr)
        sys.stderr.flush()
        return -1
    elif TEL_DONE_REPLY in reply:
        if "open" in reply:
            status.dome_status = 1
        else:
            status.dome_status = 0
    
    # Get LST
    reply = do_telescope_command(LST_COMMAND, TELESCOPE_COMMAND_TIMEOUT, host_name)
    if reply is None:
        print("update_telescope_status: error getting lst", file=sys.stderr)
        sys.stderr.flush()
        return -1
    elif TEL_DONE_REPLY in reply:
        parts = reply.split()
        if len(parts) >= 2:
            try:
                status.lst = float(parts[1])
            except ValueError:
                pass
    
    # Get focus
    result, focus = get_telescope_focus()
    if result != 0:
        print("update_telescope_status: error getting focus", file=sys.stderr)
        sys.stderr.flush()
        return -1
    status.focus = focus
    
    # Set filter to UNKNOWN (as in original code)
    status.filter_string = "UNKNOWN"
    
    # Get position
    reply = do_telescope_command(POSRD_COMMAND, TELESCOPE_COMMAND_TIMEOUT, host_name)
    if reply is None:
        print("update_telescope_status: error getting position", file=sys.stderr)
        sys.stderr.flush()
        return -1
    elif TEL_DONE_REPLY in reply:
        parts = reply.split()
        if len(parts) >= 3:
            try:
                status.ra = float(parts[1])
                status.dec = float(parts[2])
            except ValueError:
                pass
    
    # Get weather
    reply = do_telescope_command(WEATHER_COMMAND, TELESCOPE_COMMAND_TIMEOUT, host_name)
    if reply is None:
        print("update_telescope_status: error getting weather", file=sys.stderr)
        sys.stderr.flush()
        return -1
    elif TEL_DONE_REPLY in reply:
        # Parse weather data from reply
        # Format: ok ... : temperature : humidity : wind_speed : wind_direction : dew_point
        parts = reply.split(':')
        try:
            if len(parts) > 1:
                status.weather.temperature = float(parts[1].strip())
            if len(parts) > 2:
                status.weather.humidity = float(parts[2].strip())
            if len(parts) > 3:
                status.weather.wind_speed = float(parts[3].strip())
            if len(parts) > 4:
                status.weather.wind_direction = float(parts[4].strip())
            if len(parts) > 5:
                status.weather.dew_point = float(parts[5].strip())
        except ValueError:
            pass
    
    return 0


def print_telescope_status(status: TelescopeStatus, output=sys.stdout):
    """
    Print telescope status
    
    Args:
        status: TelescopeStatus object
        output: Output file object
    """
    w = status.weather
    
    print(f"UT    : {status.ut:10.6f}  ", end='', file=output)
    print(f"LST   : {status.lst:10.6f}  ", end='', file=output)
    print(f"RA    : {status.ra:10.6f}  ", end='', file=output)
    print(f"Dec   : {status.dec:10.6f}  ", end='', file=output)
    
    if status.dome_status == 1:
        print("dome  : open  ", end='', file=output)
    else:
        print("dome  : closed  ", end='', file=output)
    
    print(f"Focus : {status.focus:7.3f}  ", end='', file=output)
    print(f"Filter: {status.filter_string}  ", end='', file=output)
    print(f"Temp  : {w.temperature:5.1f}  ", end='', file=output)
    print(f"Humid : {w.humidity:5.1f}  ", end='', file=output)
    print(f"Wnd Sp: {w.wind_speed:5.1f}  ", end='', file=output)
    print(f"Wnd Dr: {w.wind_direction:5.1f}", file=output)


def send_command(command: str, host: str, port: int, timeout: int) -> Optional[str]:
    """
    Send command to telescope controller via socket
    
    Args:
        command: Command string to send
        host: Hostname or IP address
        port: Port number
        timeout: Timeout in seconds
        
    Returns:
        Reply string on success, None on failure
    """
    try:
        # Create socket
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(timeout)
        
        # Connect to server
        sock.connect((host, port))
        
        # Send command
        if not command.endswith('\n'):
            command += '\n'
        sock.sendall(command.encode())
        
        # Receive reply
        reply = sock.recv(MAXBUFSIZE).decode().strip()
        
        # Close socket
        sock.close()
        
        return reply
        
    except (socket.error, socket.timeout) as e:
        print(f"send_command: socket error: {e}", file=sys.stderr)
        return None


def do_telescope_command(command: str, timeout: int, host: str) -> Optional[str]:
    """
    Execute telescope command
    
    Args:
        command: Command to execute
        timeout: Timeout in seconds
        host: Hostname
        
    Returns:
        Reply string on success, None on failure
    """
    if verbose1:
        print(f"do_telescope_command: sending command: {command}", file=sys.stderr)
        sys.stderr.flush()
    
    reply = send_command(command, host, TEL_COMMAND_PORT, timeout)
    
    if reply is None:
        print(f"do_telescope_command: error sending command {command}", file=sys.stderr)
        sys.stderr.flush()
        return None
    
    if COMMAND_WAIT_TIME > 0:
        time.sleep(COMMAND_WAIT_TIME)
    
    if TEL_ERROR_REPLY in reply or len(reply) == 0:
        print(f"do_telescope_command: error reading domestatus : {reply}", file=sys.stderr)
        return None
    elif TEL_DONE_REPLY in reply:
        if verbose1:
            print(f"do_telescope_command: reply was {reply}", file=sys.stderr)
            sys.stderr.flush()
        return reply
    else:
        print(f"do_telescope_command: bad response from telescope : {reply}", file=sys.stderr)
        return None


def do_daytime_telescope_command(command: str, timeout: int, host: str) -> Optional[str]:
    """
    Execute telescope command in daytime mode
    Use this when telescope controller is off or dome has not yet opened
    
    Args:
        command: Command to execute
        timeout: Timeout in seconds
        host: Hostname
        
    Returns:
        Reply string on success, None on failure
    """
    if verbose1:
        print(f"do_telescope_command: sending command: {command}", file=sys.stderr)
        sys.stderr.flush()
    
    reply = send_command(command, host, DAYTIME_TEL_COMMAND_PORT, timeout)
    
    if reply is None:
        print(f"do_telescope_command: error sending command {command}", file=sys.stderr)
        sys.stderr.flush()
        return None
    
    if COMMAND_WAIT_TIME > 0:
        time.sleep(COMMAND_WAIT_TIME)
    
    if TEL_ERROR_REPLY in reply or len(reply) == 0:
        print(f"do_telescope_command: error reading domestatus : {reply}", file=sys.stderr)
        return None
    elif TEL_DONE_REPLY in reply:
        if verbose1:
            print(f"do_telescope_command: reply was {reply}", file=sys.stderr)
            sys.stderr.flush()
        return reply
    else:
        print(f"do_telescope_command: bad response from telescope : {reply}", file=sys.stderr)
        return None


def get_ut() -> float:
    """
    Get current UT time in fractional hours
    
    Returns:
        UT time in hours
    """
    now = datetime.now(timezone.utc)
    ut = now.hour + now.minute / 60.0 + now.second / 3600.0
    
    # Debug offset
    if UT_OFFSET != 0.0:
        ut = ut + UT_OFFSET
        if ut > 24.0:
            ut = ut - 24.0
    
    return ut


def get_tm() -> Tuple[datetime, float]:
    """
    Get current time structure
    
    Returns:
        Tuple of (datetime object, UT in hours)
    """
    now = datetime.now(timezone.utc)
    ut = now.hour + now.minute / 60.0 + now.second / 3600.0
    
    # Debug offset
    if UT_OFFSET != 0.0:
        ut = ut + UT_OFFSET
        if ut > 24.0:
            ut = ut - 24.0
            # Advance day
            from datetime import timedelta
            now = now + timedelta(days=1)
        
        # Adjust time components
        hour = int(ut)
        minute = int((ut - hour) * 60.0)
        second = int((ut - hour - minute / 60.0) * 3600.0)
        now = now.replace(hour=hour, minute=minute, second=second)
    
    return now, ut


def leap_year_check(year: int) -> bool:
    """
    Check if year is a leap year
    
    Args:
        year: Year to check
        
    Returns:
        True if leap year, False otherwise
    """
    if year % 4 != 0:
        return False
    if year % 100 != 0:
        return True
    if year % 400 == 0:
        return True
    return False


def date_to_jd(date_time: datetime) -> float:
    """
    Convert datetime to Julian Date
    
    Args:
        date_time: datetime object
        
    Returns:
        Julian Date
    """
    # Simple approximation - should use proper astronomical calculation
    # This is a placeholder implementation
    a = (14 - date_time.month) // 12
    y = date_time.year + 4800 - a
    m = date_time.month + 12 * a - 3
    
    jdn = date_time.day + (153 * m + 2) // 5 + 365 * y + y // 4 - y // 100 + y // 400 - 32045
    jd = jdn + (date_time.hour - 12) / 24.0 + date_time.minute / 1440.0 + date_time.second / 86400.0
    
    return jd


def get_jd() -> float:
    """
    Get current Julian Date
    
    Returns:
        Current Julian Date
    """
    dt, ut = get_tm()
    return date_to_jd(dt)


# Module initialization
def init_module(host: str = "localhost", verbose_level: int = 0):
    """
    Initialize the telescope module
    
    Args:
        host: Hostname for telescope controller
        verbose_level: Verbosity level (0=quiet, 1=verbose, 2=very verbose)
    """
    global host_name, verbose, verbose1
    
    host_name = host
    verbose = verbose_level >= 1
    verbose1 = verbose_level >= 2


# Example usage
if __name__ == "__main__":
    # Initialize module
    init_module("localhost", 1)
    
    # Create status object
    status = TelescopeStatus()
    
    # Update status
    if update_telescope_status(status) == 0:
        print_telescope_status(status)
    
    # Example field
    field = Field(field_number=1, n_done=0, filename="test.fits")
    
    # Get telescope offsets
    get_telescope_offsets(field, status)