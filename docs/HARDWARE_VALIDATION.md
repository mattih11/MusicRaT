# Hardware Validation Matrix

Only physical measurements support hardware performance claims. A development
test is not qualification of an installed, pinned release image. Record exact
CoreRaT, CommRaT, MusicRaT, and RaTOS revisions, dirty state, SDK/image identity,
kernel, device, negotiated PCM geometry, duration, load, and counters for each
qualification run.

See [EVL_AND_RATOS.md](EVL_AND_RATOS.md#repeatable-audio-validation) for
repeatable SDK and runtime checks. Null-device validation passes on standard
Linux, EVL QEMU, and H4 without taking ownership of the live demo's physical PCM.

## Development Evidence

The following observations were made during the 2026-10-18 H4 investigation.
The CommRaT and MusicRaT timing/buffering changes were uncommitted, and SDK
validation required current CoreRaT headers and a private target ALSA library
overlay. These rows deliberately do not declare a supported release combination.

Local source bases for this development session (not proof of the installed
image's recipe pins):

- CoreRaT: `db1107e837172e3668b1cca7656652e02f25187b`, with an unrelated
  untracked build directory.
- CommRaT: `f2eaaf9001b402b068b5cd17ca5cd8b0169b3093` plus the deadline fix.
- MusicRaT: `a6c50c19bcb40df5ad8df46c6f37e686879862d3` plus the buffering fix.
- RaTOS: `5fa005eacd101d691b1bd5bf86213b56ed04dfdf`.

| Target | Kernel/backend | Format and negotiated PCM geometry | Exercise | Observed result | Qualification |
| --- | --- | --- | --- | --- | --- |
| ODROID-H4, ALC897 analog (`plughw:0,0`) | 6.12.69-cip17 EVL / in-band ALSA worker | 48 kHz, stereo, S16_LE, 240-frame period, 960-frame buffer | 3000 blocks of 480 frames, approximately 30 s, periodic 16 ms producer delays; producer uses CoreRaT EVL thread/clock | 0 rejected blocks, 0 underruns, 0 reported missing frames, 0 stream failures | Development only; no CPU/I/O stress, latency, or long-duration drift qualification |
| ODROID-H4, default PCM | 6.12.69-cip17 EVL / ALSA | 48 kHz, stereo; physical PCM MMAP_INTERLEAVED/S32_LE, 1024-frame period, 16384-frame buffer | Interactive LVGL gain/pan oscillator with updated audio/control binaries and installed LVGL adapter | User confirmed working audio and controls | Mixed development/installed deployment; exact display model and image identity still to be recorded |
| Linux development host / PipeWire | Standard Linux / PipeWire | Requested 48 kHz stereo, 480-frame producer blocks | 3000 blocks, periodic 16 ms producer delays | 0 rejected blocks, 0 underruns, 0 reported missing frames, 0 stream failures | Development only; negotiated callback geometry and full host identity not captured |

## Qualification Still Required

- A clean, published dependency stack and SDK containing matching development
  headers, libraries, descriptor tooling, and ALSA development files.
- Rebuilt pinned H4 image, reboot, and an installed gain/pan demo without
  temporary deployment directories or source/build-tree dependencies.
- Recorded display connector/model and touchscreen identity.
- Sustained playback under CPU and I/O load with health counters and queue
  occupancy, followed by positive/negative producer-to-device clock-skew tests.
- Physical end-to-end latency and jitter measurements with documented methods.
- Disconnect/reconnect and orderly shutdown/restart tests.

The bounded probe described in [EVL_AND_RATOS.md](EVL_AND_RATOS.md) reports
backend counters and producer wake-up lateness. It does not measure hardware
latency, queue occupancy, or OOB residency, and it does not correct clock drift.
Its exit status is nonzero for rejected blocks, underruns, stream failures,
invalid configuration, or failed EVL producer attachment.
