# Air-writing (imu/)

Implemented **inside the existing files** - nothing replaces the existing UI, wrapper or HTTP client.

| Existing file | What changed |
|---|---|
| `lib/lcd/LCD_app.cpp/.h` | UI state machine extended: number select → **Start/Back** confirm → countdown/recording → "All characters are saved!" + **SEND TO SERVER** → upload progress / waiting for OCR / predicted text → actionable error screen with RETRY / TRY AGAIN. Same `Button`s, `TP_DrawBoard()`, `APP_Init()`, `prevent_interaction()`. Added the two queues + message structs (`AppCommand`, `AppEvent`), `APP_Poll()`, bus lock around every display operation/touch scan. MENU/LOGOUT kept; LOGOUT no longer pushes `CORE1_EXIT_FLAG` through the hardware FIFO (it just returns to the first screen). |
| `test/letter_recognition_test.cpp` | Was the WIP entry point (and did not compile). Now: core 0 polls `TP_DrawBoard()` + `APP_Poll()`; core 1 runs the worker that was meant to be there (IMU, estimator, SD, Wi-Fi/HTTP) with the previously commented-out `request.hpp` / `sd_card_wrapper.hpp`. |
| `src/position_estimator.cpp` | Was empty. Now the trajectory estimator and the file/payload formats (+ new `.hpp`). |
| `lib/imu/icm20948.c/.h` | + `imuRawAccelGyroRead()` (one synchronised 12-byte burst, bounded timeout). `imuDataAccGyrGet()` applies an 8-call moving average and `imuDataGet()` blocks 10-200 ms per call, so neither is usable at 30 Hz. |
| `src/sd_card_wrapper.cpp/.hpp` | + non-halting `mount()`/`unmount()`, `makeDir()`, `writeFileAtomic()` (tmp file, write, `f_sync`, close, size check, rename; every result checked). The old halting constructor and methods are unchanged. |
| `lib/http_client/request.cpp/.hpp` | + `HttpRequest(url, host, port)` and `post_ex()`: bounded DNS/connect/total timeouts, lwIP lock around raw API calls, HTTP status + body, extra headers. `post()`/`get()` untouched (except that the error callback no longer leaves a dangling pcb pointer, and `acked` is counted). |
| `config/hw_config.c` | SD card on **SDIO** (GP17-22, as in the lab examples; CLK GP17 collides with the touch IRQ). The shared-SPI1 variant (CS GP22) is only built with `-DAW_SD_USE_SPI=1`; on the lab hardware it failed to mount (`FR_NOT_READY`). Exposes the SPI1 mutex. |
| `CMakeLists.txt`, `lib/lcd/CMakeLists.txt` | `position_estimator` library, link lines of `letter_recognition_test`, optional `-DAW_*` settings. |

New: `test/host/` (PC test of the estimator + formats against the Python reference), `tools/air_writing/` (float64 reference, simulator of ICM-20948 recordings, golden-data generator). Server side: `MLES_server_app` (`routers/air_writing.py`, `services/air_writing.py`, `tests/`).

## Build
```bash
cmake -S . -B build -DPICO_BOARD=pico2_w [-DAW_SERVER_HOST=... -DAW_SERVER_PORT=... -DAW_API_TOKEN=... \
      -DAW_WIFI_SSID=... -DAW_WIFI_PASSWORD=... -DAW_UPLOAD_RESAMPLE_POINTS=0 -DAW_LPF_CUTOFF_HZ=5]
cmake --build build --target letter_recognition_test        # picotool load build/letter_recognition_test.uf2
make -C test/host test                                      # PC test
```
Defaults for host/port/Wi-Fi are the constants already in `request.hpp` (credentials are committed there - do not publish).

## Queues, cores, locking
UI = core 0, worker = core 1 (own 24 KB stack). Two SDK software queues, no hardware FIFO:
`command_queue` (depth 4, UI `queue_try_add`; full ⇒ "Device busy", worker `queue_remove_blocking` when idle) and `event_queue`
(depth 32, UI `queue_try_remove`). Commands `START_CAPTURE`, `SEND_WORD`; events `COUNTDOWN`, `RECORDING`, `CHARACTER_SAVED`,
`ALL_SAVED`, `UPLOAD_PROGRESS`, `WAITING_FOR_OCR`, `OCR_RESULT`, `ERROR`, all with session id and character index; elements are copied structs.
Progress events are coalesced (newest kept when full); completion/error use `queue_add_blocking` and are never dropped. Start/Send are ignored while
`inference->state == PROCESSING`. I2C1/IMU, SD, Wi-Fi: core 1 only; LCD/touch: core 0 only; SPI1 (LCD, touch, SD): one mutex - the SD driver's own - held per complete SD transaction
and per display operation/touch scan (LCD baud set and the SD baud restored inside the lock), never while waiting for queue space, recording, computing or HTTP. Display redraws that
cannot get the bus within 25 ms are retried on the next poll. Cancellation is not implemented (a blocking HTTP exchange cannot be interrupted).

## Acquisition and estimation
150 ticks on an absolute 30 Hz schedule: 60 stationary preparation + 90 gesture; each is the mean of 4 reads (120 Hz, anti-aliasing before decimation, on top of the chip's 5.7 Hz low-pass), timestamped with the centroid of the transactions.
Estimator (`position_estimator.cpp`, mirrored by `tools/air_writing/reference_trajectory.py`): counts → m/s², rad/s; stationary window → gravity, gyro bias, rest check; initial quaternion from gravity; 6-axis Mahony over the gesture
(sensor→world); `R(q)a − [0,0,|g|]`; causal 4th-order Butterworth (5 Hz, designed for the measured rate, `sosfilt_zi` start); trapezoidal integration with measured intervals; terminal-velocity linear detrend only if start **and** end are at rest;
position. No normalisation/padding/quantisation. Per-character state is reset (`out` is fully reinitialised).
**IMU facts:** ±2 g (16384 LSB/g), ±1000 dps (32.8 LSB/dps), accelerometer reads +g on world Z at rest and is *not* gravity-compensated, no orientation is provided (own quaternion filter).

## SD card
`/AW/<request_id>/session.json`, `c<i>_raw.csv` (raw counts + quaternion + timestamps), `c<i>_pos.csv`, `c<i>_x.txt` (exactly the `x` string that is uploaded), `c<i>_meta.json` (units, calibration, frames, processing version, quality; written last).
A character is "saved" only after all four files are written, synced, closed and verified. Failures unmount the volume (next start re-initialises) and keep earlier recordings.

## Server API (`MLES_server_app`, `POST /api/v1/air_writing`)
One JSON request per character: `{"request_id":"0a1b2c3d","packet_index":i,"total_packets":n,"x":"x0,y0,z0,x1,y1,z1,..."}` (0-based index; `x` = metres, world frame Z up).
The server builds a 2D image per request (stored in `.tmp/buffers/{request_id}/`); the reply to the last one (`packet_index == total_packets-1`) is `{"data":"<OCR text>"}`.
Details, error codes and idempotency: `MLES_server_app/README.md`. The firmware sends the request body built from `c<i>_x.txt` (helper `trajectory_text()` in `position_estimator.cpp`;
`request_id` = `format_session_id()`), with 8 s connect, 15 s per character, 45 s for the last (OCR) request, 2 attempts on network errors/5xx.
The server assumes the writer looks along +Y (`UP`/`VIEWER` in `services/air_writing.py`, `kDefaultUp/kDefaultViewer` here).

## Verified / not verified
Verified: C++ estimator vs float64 Python on 7 identical recordings (max difference 6.4e-6 m in position, 4.2e-6 m/s² acceleration; stationary drift 2.4 mm; no drift correction when the ending moves); `x` string / raw-CSV round trip; server tests (45) incl. real Tesseract;
simulated IMU → C++ estimator → server → Tesseract: 21/24 words exact (the single-stroke "E" is the weak glyph); firmware builds (`letter_recognition_test`, `main`, `gesture_recognition_test`).
**Not verified (no hardware):** UI screens, touch, IMU timing under display activity (the serial log prints late ticks / lateness per character), SD failures, Wi-Fi/HTTP timeouts and retries, duplicate-upload flow on the device.
Assumptions to confirm: sensor +Y points away from the writer (`kDefaultViewer`/`kDefaultUp` in `position_estimator.hpp`), chip filter 5.7 Hz / ODR ~140 Hz as set in `icm20948.c`, factory accelerometer calibration (`kAccelBiasMps2`, `kAccelScale`).
Dropped from the earlier attempt to keep the change small: hardware-free mock worker, fake-queue and protocol-parser host tests (their logic is now inline in the files above and only checked on hardware).
