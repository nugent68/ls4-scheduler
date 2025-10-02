"""Python translation of scheduler_camera.c.

This module provides a high-level camera interface that mirrors the
behaviour of the legacy C implementation used by the LS4 scheduler.
It preserves the original control flow, timing semantics, socket
protocol, and concurrency model while expressing the logic in Python.

Key features:
* Threaded execution of long-running camera commands with semaphore
  handshakes so the caller can overlap telescope slews with CCD readout.
* Exposure orchestration that updates FITS headers prior to an exposure,
  imprints those headers on the controller, issues Archon commands, and
  waits for exposure or readout completion based on the configured mode.
* Status polling for dedicated status sockets that can shorten the
  latency for detecting exposure completion during high-cadence runs.
* Translation of status payloads returned by ls4_control into a strongly
  typed CameraStatus object that replicates scheduler_status.c parsing.

The public API intentionally mirrors the C function signatures wherever
possible so that higher-level scheduler logic can migrate with minimal
changes.  Helper dataclasses are supplied for the Field, FitsHeader, and
CameraStatus records that the C code manipulated directly.
"""

from __future__ import annotations

import ast
import logging
import math
import socket
import threading
import time
from dataclasses import dataclass, field
from datetime import datetime, timezone
from itertools import count
from typing import Dict, Iterable, List, Optional, Tuple


# ---------------------------------------------------------------------------
# Constants translated from scheduler_camera.h
# ---------------------------------------------------------------------------

MACHINE_NAME = "pco-nuc"
COMMAND_PORT = 5000
STATUS_PORT = 5001

COMMAND_DELAY_USEC = 100_000  # microseconds between commands

ERROR_REPLY = "ERROR"
DONE_REPLY = "DONE"

CLEAR_TIME = 20  # seconds required to clear camera
NUM_CAMERA_CLEARS = 0  # default clears executed per clear request
READOUT_TIME_SEC = 40
TRANSFER_TIME_SEC = 10
EXPOSURE_OVERHEAD_HR = (READOUT_TIME_SEC + 5.0) / 3600.0

CAMERA_TIMEOUT_SEC = 5  # default timeout for short commands

EXP_MODE_SINGLE = "single"
EXP_MODE_FIRST = "first"
EXP_MODE_NEXT = "next"
EXP_MODE_LAST = "last"

BAD_ERROR_CODE = 2
BAD_READOUT_TIME = 60.0

FILENAME_LENGTH = 16
MAXBUFSIZE = 8192

# ---------------------------------------------------------------------------
# Enumerations reproduced from scheduler.h
# ---------------------------------------------------------------------------

BAD_CODE = -1
DARK_CODE = 0
SKY_CODE = 1
FOCUS_CODE = 2
OFFSET_CODE = 3
EVENING_FLAT_CODE = 4
MORNING_FLAT_CODE = 5
DOME_FLAT_CODE = 6
LIGO_CODE = 7  # kept for completeness

BAD_FIELD_TYPE = "unknown"
DARK_FIELD_TYPE = "dark"
SKY_FIELD_TYPE = "sky"
FOCUS_FIELD_TYPE = "focus"
OFFSET_FIELD_TYPE = "offset"
EVENING_FLAT_TYPE = "pmskyflat"
MORNING_FLAT_TYPE = "amskyflat"
DOME_FLAT_TYPE = "domeskyflat"

BAD_STRING_LC = "?"
DARK_STRING_LC = "d"
SKY_STRING_LC = "s"
FOCUS_STRING_LC = "f"
OFFSET_STRING_LC = "p"
EVENING_FLAT_STRING_LC = "e"
MORNING_FLAT_STRING_LC = "m"
DOME_FLAT_STRING_LC = "l"

# Controller state bookkeeping
STATE_NAMES: Tuple[str, ...] = (
    "NOSTATUS",
    "UNKNOWN",
    "IDLE",
    "EXPOSING",
    "READOUT_PENDING",
    "READING",
    "FETCHING",
    "FLUSHING",
    "ERASING",
    "PURGING",
    "AUTOCLEAR",
    "AUTOFLUSH",
    "POWERON",
    "POWEROFF",
    "POWERBAD",
    "FETCH_PENDING",
    "ERROR",
    "ACTIVE",
    "ERRORED",
)

NUM_STATES = len(STATE_NAMES)
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
ERROR_STATE = 16
ACTIVE = 17
ERRORED = 18

ALL_POSITIVE_VAL = 15
ALL_NEGATIVE_VAL = 0

# ---------------------------------------------------------------------------
# Logging helpers
# ---------------------------------------------------------------------------

logger = logging.getLogger(__name__)
_verbose = 0
_verbose1 = 0


def set_verbosity(verbose: int, verbose1: Optional[int] = None) -> None:
    """Configure verbosity flags (matches C globals verbose / verbose1)."""
    global _verbose, _verbose1
    _verbose = verbose
    if verbose1 is not None:
        _verbose1 = verbose1
    else:
        _verbose1 = 1 if verbose > 1 else 0


def _log_verbose(message: str) -> None:
    if _verbose:
        logger.info(message)


def _log_debug(message: str) -> None:
    if _verbose1:
        logger.debug(message)


# ---------------------------------------------------------------------------
# Dataclasses that mirror the original C structs
# ---------------------------------------------------------------------------

@dataclass
class FitsWord:
    keyword: str
    value: str


@dataclass
class FitsHeader:
    words: List[FitsWord] = field(default_factory=list)

    def update(self, keyword: str, value: str) -> None:
        """Replace or append a FITS keyword/value pair."""
        for word in self.words:
            if word.keyword == keyword:
                word.value = value
                return
        self.words.append(FitsWord(keyword=keyword, value=value))

    def __iter__(self) -> Iterable[FitsWord]:
        return iter(self.words)

    def __len__(self) -> int:
        return len(self.words)


@dataclass
class Field:
    """Subset of Field members needed for camera interactions."""
    expt: float  # exposure time in hours
    shutter: int
    script_line: str = ""
    n_done: int = 0
    n_required: int = 0
    ut: List[float] = field(default_factory=list)
    jd: List[float] = field(default_factory=list)
    lst: List[float] = field(default_factory=list)
    ha: List[float] = field(default_factory=list)
    actual_expt: List[float] = field(default_factory=list)
    filename: List[str] = field(default_factory=list)

    def record_observation(self, ut: float, jd: float, lst: float, ha: float, actual_exposure_sec: float, filename: str) -> None:
        """Append bookkeeping for a completed exposure."""
        self.ut.append(ut)
        self.jd.append(jd)
        self.lst.append(lst)
        self.ha.append(ha)
        self.actual_expt.append(actual_exposure_sec / 3600.0)
        self.filename.append(filename[:FILENAME_LENGTH])
        self.n_done += 1


@dataclass
class ControllerState:
    nostatus: int = 0
    unknown: int = 0
    idle: int = 0
    exposing: int = 0
    readout_pending: int = 0
    fetch_pending: int = 0
    reading: int = 0
    fetching: int = 0
    flushing: int = 0
    erasing: int = 0
    purging: int = 0
    autoclear: int = 0
    autoflush: int = 0
    poweron: int = 0
    poweroff: int = 0
    powerbad: int = 0
    error: int = 0
    active: int = 0
    errored: int = 0


@dataclass
class CameraStatus:
    ready: bool = False
    error: bool = False
    error_code: int = 0
    state: str = ""
    comment: str = ""
    date: str = ""
    read_time: float = 0.0
    state_val: List[int] = field(default_factory=lambda: [0] * NUM_STATES)
    cmd_error: bool = False
    cmd_error_msg: str = ""
    cmd_command: str = ""
    cmd_arg_value_list: str = ""
    cmd_reply: str = ""


@dataclass
class ExposureResult:
    filename: str
    ut_hours: float
    jd: float
    actual_exposure_sec: float
    error_code: int
    reply: Optional[str]
    waited_for_readout: bool


@dataclass
class _DoCommandArgs:
    command: str
    timeout: int
    start_semaphore: threading.Semaphore
    done_semaphore: threading.Semaphore
    command_id: int
    reply_holder: List[str]
    result_holder: List[int]


# ---------------------------------------------------------------------------
# Module globals that mirror the C file's static state
# ---------------------------------------------------------------------------

command_start_semaphore = threading.Semaphore(0)
command_done_semaphore = threading.Semaphore(0)

status_channel_active = False
readout_pending = False

cam_status = CameraStatus()
_host_name = socket.gethostname()

_command_counter = count(0)
_command_id_lock = threading.Lock()

_t_exp_start_monotonic = 0.0


def set_host_name(host: str) -> None:
    """Override the default host name used for camera connections."""
    global _host_name
    _host_name = host


def configure_status_channel(active: bool) -> None:
    """Enable or disable dedicated status channel polling."""
    global status_channel_active
    status_channel_active = active


def init_semaphores() -> None:
    """Reset the command synchronization semaphores (parity with C helper)."""
    global command_start_semaphore, command_done_semaphore
    command_start_semaphore = threading.Semaphore(0)
    command_done_semaphore = threading.Semaphore(0)
    _log_debug("init_semaphores: start and done semaphores reset")


# ---------------------------------------------------------------------------
# Time utilities
# ---------------------------------------------------------------------------

def get_tm() -> Tuple[datetime, float]:
    """Return the current UTC datetime and UT hours since midnight."""
    now = datetime.now(timezone.utc)
    ut_hours = now.hour + now.minute / 60.0 + now.second / 3600.0 + now.microsecond / 3_600_000_000.0
    return now, ut_hours


def get_ut() -> float:
    """Return fractional UT hours since midnight."""
    return get_tm()[1]


def julian_date(dt: datetime) -> float:
    """Convert a timezone-aware datetime to Julian Date (UTC)."""
    if dt.tzinfo is None:
        raise ValueError("datetime must be timezone-aware (UTC)")
    year = dt.year
    month = dt.month
    day = dt.day + (dt.hour + dt.minute / 60.0 + dt.second / 3600.0 + dt.microsecond / 3_600_000_000.0) / 24.0
    if month <= 2:
        year -= 1
        month += 12
    A = math.floor(year / 100)
    B = 2 - A + math.floor(A / 4)
    jd = math.floor(365.25 * (year + 4716)) + math.floor(30.6001 * (month + 1)) + day + B - 1524.5
    return jd


def get_jd() -> float:
    """Return the current Julian Date."""
    now = datetime.now(timezone.utc)
    return julian_date(now)


# ---------------------------------------------------------------------------
# FITS header helpers
# ---------------------------------------------------------------------------

def imprint_fits_header(header: FitsHeader) -> int:
    """Send header keyword/value pairs to the camera controller."""
    next_id = _next_command_id()
    for word in header:
        command = f"header {word.keyword} {word.value}"
        rc, reply = do_camera_command(command, CAMERA_TIMEOUT_SEC, next_id, _host_name)
        if rc != 0:
            _log_verbose(f"imprint_fits_header: error sending {command}: {reply}")
            return -1
    return 0


# ---------------------------------------------------------------------------
# Helper functions mirroring scheduler.c utilities
# ---------------------------------------------------------------------------

def get_shutter_string(shutter: int) -> Tuple[str, str]:
    """Return shutter keyword and description string."""
    if shutter == DARK_CODE:
        return DARK_STRING_LC, DARK_FIELD_TYPE
    if shutter == SKY_CODE:
        return SKY_STRING_LC, SKY_FIELD_TYPE
    if shutter == FOCUS_CODE:
        return FOCUS_STRING_LC, FOCUS_FIELD_TYPE
    if shutter == OFFSET_CODE:
        return OFFSET_STRING_LC, OFFSET_FIELD_TYPE
    if shutter == EVENING_FLAT_CODE:
        return EVENING_FLAT_STRING_LC, EVENING_FLAT_TYPE
    if shutter == MORNING_FLAT_CODE:
        return MORNING_FLAT_STRING_LC, MORNING_FLAT_TYPE
    if shutter == DOME_FLAT_CODE:
        return DOME_FLAT_STRING_LC, DOME_FLAT_TYPE
    if shutter == BAD_CODE:
        return BAD_STRING_LC, BAD_FIELD_TYPE
    return BAD_STRING_LC, BAD_FIELD_TYPE


def get_filename(timestamp: datetime, shutter: int) -> str:
    """Construct file name root yyyymmddHHMMSSx."""
    shutter_code, _ = get_shutter_string(shutter)
    return timestamp.strftime(f"%Y%m%d%H%M%S{shutter_code}")


def expose_timeout(exp_mode: str, exp_time_sec: float, wait_flag: bool) -> int:
    """Compute timeout in seconds for exposure replies."""
    exp_time = float(exp_time_sec)
    if not wait_flag:
        t = exp_time + READOUT_TIME_SEC
    else:
        if EXP_MODE_SINGLE in exp_mode:
            t = exp_time + READOUT_TIME_SEC + TRANSFER_TIME_SEC
        elif EXP_MODE_FIRST in exp_mode:
            t = exp_time + READOUT_TIME_SEC
        elif EXP_MODE_NEXT in exp_mode:
            t = max(exp_time + READOUT_TIME_SEC, TRANSFER_TIME_SEC)
        elif EXP_MODE_LAST in exp_mode:
            t = TRANSFER_TIME_SEC
        else:
            raise ValueError(f"Unrecognized exposure mode '{exp_mode}'")
    return int(math.ceil(t + 5.0))


# ---------------------------------------------------------------------------
# Status parsing utilities translated from scheduler_status.c
# ---------------------------------------------------------------------------

def binary_string_to_int(binary: str) -> int:
    if not binary:
        return 0
    result = 0
    for char in binary.strip():
        if char not in "01":
            return 0
        result = (result << 1) | (ord(char) - ord("0"))
    return result


def parse_status(reply: str, status: Optional[CameraStatus] = None) -> CameraStatus:
    """Parse a controller reply string into a CameraStatus."""
    status = status or CameraStatus()
    status.read_time = time.time()

    try:
        start = reply.index("{")
        end = reply.rindex("}")
        payload = reply[start : end + 1]
        data = ast.literal_eval(payload)
    except (ValueError, SyntaxError) as exc:
        raise ValueError(f"Unable to parse status payload: {reply}") from exc

    status.ready = bool(data.get("ready", False))
    status.error = bool(data.get("error", False))
    status.error_code = int(data.get("cmd_error_code", data.get("error_code", 0)) or 0)
    status.state = str(data.get("state", "UNKNOWN"))
    status.comment = str(data.get("comment", ""))
    status.date = str(data.get("date", ""))

    for idx, name in enumerate(STATE_NAMES):
        raw_val = data.get(name, "0000")
        status.state_val[idx] = binary_string_to_int(str(raw_val))

    status.cmd_error = bool(data.get("cmd_error", False))
    status.cmd_error_msg = str(data.get("cmd_error_msg", ""))
    status.cmd_command = str(data.get("cmd_command", ""))
    status.cmd_arg_value_list = str(data.get("cmd_arg_value_list", ""))
    status.cmd_reply = str(data.get("cmd_reply", ""))

    return status


def update_camera_status(status: Optional[CameraStatus] = None) -> Tuple[int, Optional[str]]:
    """Query controller status and update the global CameraStatus."""
    next_id = _next_command_id()
    rc, reply = do_status_command(STATUS_COMMAND, CAMERA_TIMEOUT_SEC, next_id, _host_name)
    if rc != 0:
        return rc, reply
    global cam_status
    cam_status = parse_status(reply, status or cam_status)
    return 0, reply


# ---------------------------------------------------------------------------
# Command execution helpers (socket layer)
# ---------------------------------------------------------------------------

def _next_command_id() -> int:
    with _command_id_lock:
        return next(_command_counter)


def send_command(command: str, host: str, port: int, timeout_sec: int) -> str:
    """Send a command string and return the raw reply."""
    command_bytes = command.encode("utf-8")
    address = (host, port)
    _log_debug(f"send_command[{port}]: {get_ut():12.6f} connecting to {host}:{port} command='{command}' timeout={timeout_sec}")
    try:
        with socket.create_connection(address, timeout=timeout_sec) as sock:
            sock.settimeout(timeout_sec)
            sock.sendall(command_bytes)
            sock.sendall(b"\n")
            chunks: List[bytes] = []
            while True:
                try:
                    chunk = sock.recv(MAXBUFSIZE)
                except socket.timeout as exc:
                    raise TimeoutError(f"timeout waiting for reply from {host}:{port}") from exc
                if not chunk:
                    break
                chunks.append(chunk)
                if chunk.strip().endswith(b"]"):
                    break
            reply = b"".join(chunks).decode("utf-8", errors="replace").strip()
            _log_debug(f"send_command[{port}]: {get_ut():12.6f} reply='{reply}'")
            return reply
    except OSError as exc:
        raise ConnectionError(f"send_command[{port}] failed: {exc}") from exc


def do_command(command: str, timeout_sec: int, port: int, command_id: int, host: Optional[str]) -> Tuple[int, str]:
    """Low-level command dispatcher that mirrors the C logic."""
    target_host = host or _host_name
    try:
        reply = send_command(command, target_host, port, timeout_sec)
    except Exception as exc:  # noqa: BLE001 we want to pass through all socket errors
        logger.error("do_command[%d]: %s", command_id, exc)
        return -1, str(exc)

    time.sleep(COMMAND_DELAY_USEC / 1_000_000.0)

    if not reply or DONE_REPLY not in reply or "ERROR_REPLY" in reply or ERROR_REPLY in reply:
        logger.error(
            "do_command[%d]: %12.6f : command '%s' returned error reply: %s",
            command_id,
            get_ut(),
            command,
            reply,
        )
        return -1, reply

    _log_debug(f"do_command[{command_id}]: {get_ut():12.6f} reply '{reply}'")
    return 0, reply


def do_camera_command(command: str, timeout_sec: int, command_id: int, host: Optional[str]) -> Tuple[int, str]:
    return do_command(command, timeout_sec, COMMAND_PORT, command_id, host)


def do_status_command(command: str, timeout_sec: int, command_id: int, host: Optional[str]) -> Tuple[int, str]:
    return do_command(command, timeout_sec, STATUS_PORT, command_id, host)


# ---------------------------------------------------------------------------
# Thread orchestration replicating do_camera_command_thread
# ---------------------------------------------------------------------------

def do_camera_command_thread(args: _DoCommandArgs) -> None:
    """Thread target replicating the C worker."""
    start_time = get_ut()
    _log_debug(
        f"do_camera_command_thread[{args.command_id}]: {start_time:12.6f} waiting for start semaphore timeout={args.timeout}"
    )
    try:
        acquired = args.start_semaphore.acquire(timeout=args.timeout)
        if not acquired:
            logger.error(
                "do_camera_command_thread[%d]: %12.6f timeout waiting to start", args.command_id, get_ut()
            )
            args.result_holder[0] = -1
            args.reply_holder[0] = "timeout waiting for start semaphore"
            return
        _log_debug(
            f"do_camera_command_thread[{args.command_id}]: {get_ut():12.6f} issuing command '{args.command}'"
        )
        rc, reply = do_camera_command(args.command, args.timeout, args.command_id, _host_name)
        args.result_holder[0] = rc
        args.reply_holder[0] = reply
    finally:
        args.done_semaphore.release()
        _log_debug(
            f"do_camera_command_thread[{args.command_id}]: {get_ut():12.6f} posted done semaphore"
        )


# ---------------------------------------------------------------------------
# Exposure orchestration
# ---------------------------------------------------------------------------

def wait_exp_done(expected_exposure_sec: float) -> float:
    """Wait for an exposure to complete, optionally polling status channel."""
    timeout_sec = int(expected_exposure_sec + 5)
    _log_debug(
        f"wait_exp_done: {get_ut():12.6f} waiting up to {timeout_sec} sec for exposure completion"
    )
    time.sleep(max(0.0, expected_exposure_sec))

    if not status_channel_active:
        return expected_exposure_sec

    deadline = time.time() + timeout_sec
    while time.time() < deadline:
        rc, reply = update_camera_status()
        if rc != 0:
            logger.error("wait_exp_done: unable to update camera status: %s", reply)
            break
        if cam_status.state_val[EXPOSING] == ALL_NEGATIVE_VAL:
            _log_debug(
                f"wait_exp_done: {get_ut():12.6f} exposure finished via status channel"
            )
            return max(0.0, expected_exposure_sec - (deadline - time.time()))
        time.sleep(0.1)

    logger.error("wait_exp_done: %12.6f timeout waiting for exposure completion", get_ut())
    return -1.0


def wait_camera_readout(status: Optional[CameraStatus] = None) -> int:
    """Wait for asynchronous readout to complete when using non-blocking exposures."""
    global readout_pending, cam_status
    if not readout_pending:
        _log_debug("wait_camera_readout: no readout pending")
        return 0

    timeout_sec = READOUT_TIME_SEC
    end_time = time.time() + timeout_sec
    while time.time() < end_time:
        acquired = command_done_semaphore.acquire(blocking=False)
        if acquired:
            readout_pending = False
            _log_debug(
                f"wait_camera_readout: {get_ut():12.6f} readout complete"
            )
            if status is not None:
                status.read_time = cam_status.read_time
                status.state_val = cam_status.state_val.copy()
                status.ready = cam_status.ready
                status.error = cam_status.error
            return 0
        time.sleep(0.1)

    logger.error(
        "wait_camera_readout: %12.6f timeout waiting for readout completion", get_ut()
    )
    return -1


def clear_camera(host: Optional[str] = None) -> int:
    """Issue the controller clear command."""
    next_id = _next_command_id()
    timeout = CLEAR_TIME + 5
    command = f"clear {CLEAR_TIME}"
    rc, reply = do_camera_command(command, timeout, next_id, host or _host_name)
    if rc != 0:
        logger.error("clear_camera: %s", reply)
    return rc


def take_exposure(
    field: Field,
    header: FitsHeader,
    exposure_override_hours: float = 0.0,
    exp_mode: str = EXP_MODE_SINGLE,
    wait_for_readout: bool = True,
    host: Optional[str] = None,
) -> ExposureResult:
    """High-level exposure routine that mirrors the C implementation."""
    global readout_pending, _t_exp_start_monotonic

    actual_exposure_hours = exposure_override_hours if exposure_override_hours > 0 else field.expt
    exposure_seconds = actual_exposure_hours * 3600.0

    timestamp, ut_hours = get_tm()
    jd = get_jd()

    _, field_description = get_shutter_string(field.shutter)
    filename = get_filename(timestamp, field.shutter)

    _log_verbose(
        f"take_exposure: exposing {exposure_seconds:7.1f} sec shutter={field.shutter} "
        f"filename={filename} mode={exp_mode} wait={wait_for_readout}"
    )

    header.update("sequence", str(field.n_done + 1))
    header.update("imagetyp", field_description)
    header.update("flatfile", filename)

    comment = "no comment"
    if "#" in field.script_line:
        comment = field.script_line.split("#", 1)[1].strip()
    header.update("comment", f"'{comment}'")

    if imprint_fits_header(header) != 0:
        raise RuntimeError("take_exposure: imprint_fits_header failed")

    shutter_state = "True" if field.shutter else "False"
    timeout_sec = expose_timeout(exp_mode, exposure_seconds, wait_for_readout)
    command = f"{EXPOSE_COMMAND} {shutter_state} {exposure_seconds:9.3f} {filename} {exp_mode}"

    host_name = host or _host_name

    reply: Optional[str] = None
    error_code = 0
    waited_for_readout = wait_for_readout

    if not wait_for_readout:
        readout_pending = True
        reply_holder = [""]
        result_holder = [0]
        args = _DoCommandArgs(
            command=command,
            timeout=timeout_sec,
            start_semaphore=command_start_semaphore,
            done_semaphore=command_done_semaphore,
            command_id=_next_command_id(),
            reply_holder=reply_holder,
            result_holder=result_holder,
        )
        thread = threading.Thread(
            target=do_camera_command_thread,
            args=(args,),
            name=f"camera-command-{args.command_id}",
            daemon=True,
        )
        thread.start()
        command_start_semaphore.release()
        _t_exp_start_monotonic = time.monotonic()
        actual_wait = wait_exp_done(exposure_seconds)
        if actual_wait < 0:
            error_code = BAD_ERROR_CODE
        actual_exposure_seconds = max(0.0, actual_wait)
        reply = None  # asynchronously populated by worker
    else:
        _t_exp_start_monotonic = time.monotonic()
        rc, reply_str = do_camera_command(command, timeout_sec, _next_command_id(), host_name)
        if rc != 0:
            raise RuntimeError(
                f"take_exposure: exposure command failed: {reply_str}"
            )
        reply = reply_str
        try:
            actual_exposure_seconds = float(reply.split()[0])
        except (ValueError, IndexError):
            actual_exposure_seconds = exposure_seconds
        readout_pending = False

    result = ExposureResult(
        filename=filename,
        ut_hours=ut_hours,
        jd=jd,
        actual_exposure_sec=actual_exposure_seconds,
        error_code=error_code,
        reply=reply,
        waited_for_readout=wait_for_readout,
    )
    return result


# Constant names for command access (for parity with the C header exports)
OPEN_COMMAND = "open_shutter"
CLOSE_COMMAND = "close_shutter"
STATUS_COMMAND = "status"
CLEAR_COMMAND = "clear"
HEADER_COMMAND = "header"
EXPOSE_COMMAND = "expose"
SHUTDOWN_COMMAND = "shutdown"
REBOOT_COMMAND = "reboot"
RESTART_COMMAND = "restart"