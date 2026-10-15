# Air-writing (imu/)

Implemented **inside the existing files** - nothing replaces the existing UI, wrapper or HTTP client.

| Existing file | What changed |
|---|---|
| `lib/lcd/LCD_app.cpp/.h` | UI state machine extended: number select → **Start/Back** confirm → countdown/recording → "All characters are saved!" + **SEND TO SERVER** → upload progress / waiting for OCR / predicted text → actionable error screen with RETRY / TRY AGAIN. Same `Button`s, `TP_DrawBoard()`, `APP_Init()`, `prevent_interaction()`. Added the two queues + message structs (`AppCommand`, `AppEvent`), `APP_Poll()`, bus lock around every display operation/touch scan. MENU/LOGOUT kept; LOGOUT no longer pushes `CORE1_EXIT_FLAG` through the hardware FIFO (it just returns to the first screen). |
| `test/letter_recognition_test.cpp` | Entry point. Core 0: `TP_DrawBoard()` + `APP_Poll()`, command dispatch, IMU recording (polling as in `lab_0_1.cpp`) and the upload. Core 1: trajectory computation, SD card, events to the UI. |
| `src/position_estimator.cpp` | `estimate_trajectory()`: the position estimation of `visualize_imu_data.ipynb` (see below). |
| `lib/imu/icm20948.c/.h` | + `imuRawAccelGyroRead()` (one synchronised 12-byte burst, bounded timeout). `imuDataAccGyrGet()` applies an 8-call moving average and `imuDataGet()` blocks 10-200 ms per call, so neither is usable at 30 Hz. |
| `src/sd_card_wrapper.cpp/.hpp` | + non-halting `mount()`/`unmount()`, `makeDir()`, `writeFileAtomic()` (tmp file, write, `f_sync`, close, size check, rename; every result checked). The old halting constructor and methods are unchanged. |
| `lib/http_client/request.cpp/.hpp` | + `HttpRequest(url, host, port)` and `post_ex()`: bounded DNS/connect/total timeouts, lwIP lock around raw API calls, HTTP status + body, extra headers. `post()`/`get()` untouched (except that the error callback no longer leaves a dangling pcb pointer, and `acked` is counted). |
| `config/hw_config.c` | SD card on **SDIO** (GP17-22, as in the lab examples; CLK GP17 collides with the touch IRQ). The shared-SPI1 variant (CS GP22) is only built with `-DAW_SD_USE_SPI=1`; on the lab hardware it failed to mount (`FR_NOT_READY`). Exposes the SPI1 mutex. |
| `CMakeLists.txt`, `lib/lcd/CMakeLists.txt` | `position_estimator` library, link lines of `letter_recognition_test`, optional `-DAW_*` settings. |

New: `test/host/` (PC test of the estimator against the notebook). Server side: `MLES_server_app` (`services/air_writing.py`).

## Build
```bash
cmake -S . -B build -DPICO_BOARD=pico2_w [-DAW_SERVER_HOST=... -DAW_SERVER_PORT=... -DAW_API_TOKEN=... \
      -DAW_WIFI_SSID=... -DAW_WIFI_PASSWORD=... -DAW_UPLOAD_RESAMPLE_POINTS=0 -DAW_LPF_CUTOFF_HZ=50]
cmake --build build --target letter_recognition_test        # picotool load build/letter_recognition_test.uf2
make -C test/host test                                      # PC test (needs numpy + scipy)
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
Recording per character, as in `lab_0_1.cpp`: `imuDataAccGyrGet()` (the lab's `imuDataOnlyGet()`) is called back to back, without a sampling timer, giving about 2 kHz (the chip updates at ~140 Hz and the fast-read functions average 8 calls - the data the notebook was verified on).
Sequence: 2 s "Hold still" countdown, 0.2 s stationary prefix, "Write now!", 3 s gesture. The raw counts stay in RAM (`kMaxRecordingSamples` = 8192); the sample rate is measured over the gesture.
Estimator (`position_estimator.cpp`, `estimate_trajectory()`, runs on core 1 after the character because the drift correction needs the whole recording): the notebook's method - counts → m/s² (16384 LSB/g), subtract the mean of the stationary prefix,
4th-order Butterworth low-pass (50 Hz, `sosfilt_zi` start) over prefix + gesture, crop the prefix, trapezoidal velocity, linear detrend to zero final velocity, trapezoidal position. Accelerometer only, fixed sensor orientation (no gyroscope, quaternion or ZUPT).
The positions are in the **sensor frame** (the notebook's `position`; the notebook projects them with plane "xy") and are decimated to ~100 Hz (`OUTPUT_RATE_HZ`) for the SD card and the upload.
The board must be still during the first 0.2 s and held in the same orientation while writing. **IMU facts:** ±2 g (16384 LSB/g); the accelerometer reads +g on the axis pointing up at rest and is *not* gravity-compensated.

## SD card
`/AW/<session>/c<i>_pos.csv`: header `x_m,y_m,z_m`, one decimated position per line (metres, sensor frame). A character is "saved" once the file is written and closed.

## Server API (`MLES_server_app`, `POST /api/v1/air_writing`)
One JSON request per character: `{"request_id":"0a1b2c3d","packet_index":i,"total_packets":n,"x":"x0,y0,z0,x1,y1,z1,..."}` (0-based index; `x` = metres, world frame Z up).
The server builds a 2D image per request (stored in `.tmp/buffers/{request_id}/`); the reply to the last one (`packet_index == total_packets-1`) is `{"data":"<OCR text>"}`.
Details, error codes and idempotency: `MLES_server_app/README.md`. The firmware reads `c<i>_pos.csv` back and sends it as the `x` string (`build_trajectory()` in `letter_recognition_test.cpp`).
The server projects the 3D trajectory in `services/air_writing.py` (`project_to_2d(points3d, ...)`); the plane there has to match the notebook's "xy" for the sensor-frame positions sent by the firmware.

## Verified / not verified
Verified: `src/position_estimator.cpp` against the notebook's `estimate_trajectory()` on recordings of `lab_0_1.cpp` (`make -C test/host test`, max difference < 0.1 µm); the firmware builds (`letter_recognition_test`).
**Not verified (no hardware run):** the achieved sampling rate and buffer capacity, UI screens, SD failures, Wi-Fi/HTTP timeouts and retries.
