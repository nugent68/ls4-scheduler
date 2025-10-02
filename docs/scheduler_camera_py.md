# Python `scheduler_camera` module overview

This document summarises the Python translation of `scheduler_camera.c`
and highlights integration guidance for downstream components that
previously linked against the C implementation.

## Module entry point

The module lives at [`src/scheduler_camera.py`](src/scheduler_camera.py) and
exposes a high-level API compatible with the original C routines.  Core
functions include:

- [`set_host_name`](src/scheduler_camera.py#L268)
- [`configure_status_channel`](src/scheduler_camera.py#L276)
- [`init_semaphores`](src/scheduler_camera.py#L283)
- [`imprint_fits_header`](src/scheduler_camera.py#L339)
- [`take_exposure`](src/scheduler_camera.py#L551)
- [`wait_exp_done`](src/scheduler_camera.py#L486)
- [`wait_camera_readout`](src/scheduler_camera.py#L508)
- [`clear_camera`](src/scheduler_camera.py#L530)
- [`update_camera_status`](src/scheduler_camera.py#L428)

The constants mirror the header definitions from
[`src/scheduler_camera.h`](src/scheduler_camera.h).

## Data model

The Python translation replaces C structs with dataclasses:

- [`FitsHeader`](src/scheduler_camera.py#L180) manages FITS keywords.
- [`Field`](src/scheduler_camera.py#L198) captures the subset of field metadata
  the camera routines consume.
- [`CameraStatus`](src/scheduler_camera.py#L224) mirrors the scheduler's camera
  status structure.
- [`ExposureResult`](src/scheduler_camera.py#L248) reports exposure outcomes.

Consumers can continue to populate these objects via helper constructors or
convert from existing C structures when migrating higher-level logic.

## Concurrency model

The asynchronous exposure path still relies on paired semaphores that gate
a background worker thread (`do_camera_command_thread`).  Invoke
[`init_semaphores`](src/scheduler_camera.py#L283) during start-up to reset the
synchronisation primitives before issuing threaded exposures.

## Socket layer

[`do_command`](src/scheduler_camera.py#L449) wraps Python's `socket` module to
mirror the scheduler's command channel protocol, including reply validation
against the `"DONE"` sentinel.  Timeouts and delays (e.g.
`COMMAND_DELAY_USEC`) match the C values.

## Status parsing

[`parse_status`](src/scheduler_camera.py#L405) uses `ast.literal_eval` to
decode the controller's Python-esque reply payload into a `CameraStatus`
instance.  This mirrors the string parsing originally implemented in
[`src/scheduler_status.c`](src/scheduler_status.c#L1).

## Usage considerations

1. **Thread safety**  
   Module-level globals (`cam_status`, `status_channel_active`,
   semaphores) emulate the original shared state.  When embedding in a larger
   Python service, ensure initialisation is serialised (call
   [`init_semaphores`](src/scheduler_camera.py#L283) and
   [`set_host_name`](src/scheduler_camera.py#L268) once).

2. **Exception handling**  
   Socket failures raise Python exceptions (`ConnectionError`, `TimeoutError`)
   instead of returning `-1`.  Wrap top-level invocations with try/except if the
   caller expects numeric error codes.

3. **Time utilities**  
   Helpers [`get_tm`](src/scheduler_camera.py#L296),
   [`get_ut`](src/scheduler_camera.py#L309), and [`get_jd`](src/scheduler_camera.py#L325)
   offer simple replacements for the C counterparts defined in `scheduler.c`.

4. **FITS header updates**  
   The module exposes [`FitsHeader.update`](src/scheduler_camera.py#L182) and
   [`imprint_fits_header`](src/scheduler_camera.py#L339) to replicate the
   `update_fits_header`/`imprint_fits_header` behaviour.

5. **Exposure orchestration**  
   [`take_exposure`](src/scheduler_camera.py#L551) returns an
   [`ExposureResult`](src/scheduler_camera.py#L248) with filename, UT, JD, and
   actual exposure seconds.  The caller should update its `Field` bookkeeping
   (e.g. via [`Field.record_observation`](src/scheduler_camera.py#L210)) to stay
   aligned with the original scheduler loop.

6. **Status polling**  
   Enable the dedicated status channel via
   [`configure_status_channel`](src/scheduler_camera.py#L276) when the controller
   supports it.  This allows [`wait_exp_done`](src/scheduler_camera.py#L486) to
   poll camera status aggressively for low latency exposure termination.

## Migration checklist

- Initialise module-level state early:
  ```python
  import scheduler_camera as cam

  cam.set_host_name("pco-nuc")
  cam.configure_status_channel(True)
  cam.init_semaphores()
  ```

- Translate C `Fits_Header` arrays into `FitsHeader(words=[...])`.
- Replace direct socket calls with [`do_camera_command`](src/scheduler_camera.py#L440)
  from Python logic when porting other modules.
- Wrap top-level invocations of `take_exposure` / `wait_camera_readout`
  with error handling to map Python exceptions to legacy error codes if the
  surrounding scheduler still uses sentinel values.

## Future work

- Implement a compatibility layer that serialises `Field` and `CameraStatus`
  dataclasses back to the binary structures persisted by the original
  scheduler if on-disk compatibility is required.
- Provide asyncio wrappers for environments that prefer coroutine-based
  concurrency over threads.
- Expand unit tests that simulate controller replies to validate parsing and
  timeout handling.
