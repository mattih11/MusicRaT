# EVL and RaTOS Integration

MusicRaT targets both the standard Linux CommRaT backend and the Xenomai 4/EVL backend shipped by RaTOS. EVL compatibility is a cross-cutting constraint, not a port to perform after the audio graph is complete.

This document separates checks that can run on every development host from checks that require the RaTOS SDK, an EVL kernel in QEMU, or physical audio hardware.

## Verified Upstream Foundation

CommRaT currently provides two complementary CMake presets:

- `evl-cross` cross-compiles against the RaTOS ISAR SDK.
- `evl` builds natively inside a RaTOS QEMU guest.

CommRaT's `scripts/evl-dev.sh` can acquire or use local SDK and image artifacts, cross-compile, copy binaries into a QEMU guest, and run tests under the EVL kernel. Complete module descriptors are generated during the build by metadata-only inspectors that do not load `libevl` or add inspection code to runtime binaries.

Build the container image used for MusicRaT development with:

```bash
cd /path/to/RaTOS
scripts/build-musicrat-image.sh
```

The resulting ext4, kernel, and initrd are under
`build/tmp/deploy/images/container-amd64/`.

Build the Odroid H4 disk image and attended USB installer with:

```bash
scripts/build-musicrat-image.sh odroid-h4
scripts/build-musicrat-installer.sh
```

The installer outputs are under `build/tmp/deploy/images/odroid-h4/`. Write
`isar-image-installer-ratos-odroid-h4.wic` to a USB drive. The installer
excludes its own boot device, prompts for the target disk, and requires
confirmation before overwriting a nonempty disk. Verify the destination device
before writing the USB image or installing to internal storage.

The installed H4 image starts `systemd-networkd`, requests an IPv4 address over
DHCP on each wired Ethernet interface, regenerates its SSH host keys, and starts
OpenSSH. Find the assigned address in the router's DHCP leases or run
`ip -4 -br address` on the local console, then connect with:

```bash
ssh root@<device-ip>
```

Development images use the upstream default root password `root`. Change it
with `passwd` before connecting the device to an untrusted network.

The image also runs Avahi for Zeroconf discovery. On hosts with mDNS support,
connect directly with `ssh root@ratos-musicrat.local`. DNS-SD browsers expose
the advertised `_ssh._tcp` service as `MusicRaT on ratos-musicrat`, with
`role=musicrat` and `os=RaTOS` metadata.

Generate a local cross-compilation SDK and build MusicRaT against it with:

```bash
cd /path/to/RaTOS
scripts/build-ratos-sdk.sh

cd /path/to/MusicRaT
scripts/evl-cross.sh \
  /path/to/RaTOS/build/tmp/deploy/images/container-amd64/\
ratos-dev-image-sdk-ratos-container-amd64.tar.xz
```

The packaged stack is:

```text
EVL -> SeRTial/CoreRaT -> CommRaT -> MusicRaT -> RatGUI -> production image
```

## Validation Layers

### 1. Host Tests

Run on the standard Linux backend for every change:

- Protocol serialization and capacity invariants
- DSP numerical and boundary tests
- Module metadata and lifecycle tests
- Launcher and short graph smoke tests
- Sanitizers and checks for allocation in processing paths

These tests are fast and deterministic, but they do not prove EVL ABI compatibility or real-time scheduling behavior.

The `native-validation` configure/build/test preset enables the native modules
and tests in `build-validation`. Select **Native MusicRaT validation** as the
configure preset in VS Code CMake Tools. It uses the installed dependency stack
and retains existing optional-backend/cache selections. LVGL tests additionally
require `MUSICRAT_LVGL_SOURCE_DIR` to point to a compatible LVGL source tree.
The **Native LVGL validation** preset uses the separate `build-lvgl-validation`
cache, preserving its configured LVGL source and backend choices. Configure the
LVGL source path in that cache before using the preset on a new checkout.

### 2. EVL Cross-Build

MusicRaT provides an `evl-cross` preset using the same RaTOS SDK toolchain as
CommRaT. This stage must compile all public headers, modules, tests, and the
launcher against the SDK and preserve one generated audio-policy ABI across
every binary.

Cross-building proves SDK and dependency compatibility. Descriptor generation runs separate build-only inspectors; cross-compiled runtime module executables are never executed on the host.

### 3. EVL QEMU Runtime

Deploy the cross-built tree to `ratos-commrat-image` and run under its EVL kernel. This stage must:

- Run protocol, DSP, module, and headless integration tests
- Verify the build-generated descriptors against installed module binaries
- Launch the controlled source -> processor -> sink graph for a bounded duration
- Verify clean lifecycle shutdown and report dropped, late, or discontinuous blocks
- Run the upstream EVL sanity tests before attributing a failure to MusicRaT

QEMU proves the EVL code path and target runtime ABI. It is not an audio-latency benchmark.

### 4. RaTOS Packaging

RaTOS integration must package a pinned MusicRaT revision and install its runtime binaries, descriptors, example application descriptions, public development files, and required shared libraries. A `ratos-musicrat-image` should extend the dependency stack conceptually while remaining a self-contained ISAR image recipe, matching the existing RaTOS image convention.

The package/image test must boot in QEMU and launch an installed example without relying on a source checkout or build-tree-relative descriptor paths.

### 5. Physical Target Tests

Run on each supported RaTOS board with the selected audio backend and representative hardware:

- Sustained full-duplex audio at supported rates and periods
- Underrun, overrun, discontinuity, and recovery behavior
- Worst-case processing time, scheduling latency, and jitter
- CPU and memory pressure with bounded graph sizes
- Device disconnect/reconnect and orderly shutdown

Publish the board, kernel, image revision, audio interface, sample rate, period, duration, and observed maxima with every result. Hardware thresholds belong in a versioned compatibility matrix once the first backend is selected.

For console-only Linux targets such as Odroid images, prefer the LVGL DRM/KMS
backend with software-rendered dumb buffers. It writes directly to a connected
KMS output and does not require X11, Wayland, Mesa, or a desktop environment.
The image must include libdrm, grant the MusicRaT service access to
`/dev/dri/card*`, and leave the selected connector available. Use fbdev with
`/dev/fb0` only on kernels that still expose the legacy framebuffer API.
Display initialization and rendering stay on the non-real-time LVGL thread;
the CommRaT callback only writes to the bounded queue.

## Real-Time Portability Rules

Code reachable from `process()` or an audio backend callback must use only APIs whose EVL behavior is understood. In addition to the general no-allocation and no-blocking rules:

- Keep DSP kernels independent of CommRaT, EVL, device APIs, and operating-system services.
- Resolve files, devices, descriptors, and configuration before entering real-time execution.
- Do not log, throw, acquire non-real-time locks, or trigger lazy initialization in the processing path.
- Pre-fault and preallocate runtime storage where the owning backend requires it.
- Treat standard-Linux success as necessary but insufficient; the EVL QEMU gate is required for changes to module execution, messaging, lifecycle, or backend code.
- Require physical-target evidence for claims about latency, jitter, dropout resistance, or supported hardware.

## Planned Automation

MusicRaT CI should grow in this order:

1. Standard Linux configure, build, CTest, launcher smoke test, and sanitizers.
2. RaTOS SDK cross-compile, cached by the pinned RaTOS artifact version.
3. EVL QEMU CTest, installed-descriptor validation, and controlled-graph smoke test.
4. RaTOS recipe and image build triggered by pinned MusicRaT releases.
5. Scheduled or release-gated physical-board stress and latency tests.

EVL cross-build and QEMU failures block changes that affect runtime code. Physical tests gate supported-hardware and real-time performance claims rather than routine DSP-only development.

## Repeatable Audio Validation

Cross-build the ALSA backend and its validation tools from a complete pinned SDK:

```bash
bash scripts/evl-cross.sh /absolute/path/to/sdk \
  -DBUILD_TESTING=ON -DMUSICRAT_AUDIO_BACKEND=ALSA
```

Additional arguments are passed to CMake after the preset defaults. Target
pkg-config discovery is restricted to the SDK, so a missing target ALSA package
fails configuration rather than silently using host libraries. Archive inputs
are extracted into a fresh cache directory without replacing an existing SDK.
The default preset still builds the headless `NONE` backend unless overridden.

For targeted native validation, configure an ALSA build with `BUILD_TESTING=ON`,
build `musicrat_audio_output_probe`, `musicrat_playback_preroll_test`,
`musicrat_playback_diagnostics_test`,
`musicrat_audio_clock_drift_test`,
`musicrat_audio_output_backend_alsa_test`, and `musicrat_audio_device_sink_test`,
then run:

```bash
bash scripts/validate-audio-runtime.sh std build-alsa-validation \
  tests/fixtures/alsa_null.conf
```

For an already booted EVL QEMU guest, copy those six SDK-built executables,
`scripts/validate-audio-runtime.sh`, and `tests/fixtures/alsa_null.conf` to a
separate guest directory and run the same script with `evl` instead of `std`.
Provide the matching target runtime libraries through the image/package, not
host libraries. The script checks upstream EVL timing/thread sanity tests,
prints executable checksums and kernel identity, runs the deterministic
regressions, and checks that the probe was compiled for the requested platform.
It fails on any failed command. QEMU boot/deploy/teardown orchestration and the
controlled graph smoke test are separate remaining automation work.

The null PCM requires no physical audio device and proves neither hardware
pacing nor audio performance. For an attended physical test, first ensure no
other graph owns the selected PCM, then run, for example:

```bash
./build/evl-cross/musicrat_audio_output_probe \
  --device plughw:0,0 --sample-rate 48000 --channels 2 \
  --period-frames 480 --blocks 3000 --jitter-ms 16 --jitter-every 37
```

For controlled positive/negative producer clock-offset experiments, add
`--clock-offset-ppm 100` or `--clock-offset-ppm -100` (bounded to +/-1000 ppm).
This changes only the probe producer's absolute block deadline interval; the
audio device remains on its own clock. The JSON reports the requested offset.

This emits a quiet 440 Hz sine with 20 ms linear start/end fades and a JSON
health result. The producer uses
CoreRaT absolute deadlines on a platform-managed thread; EVL attachment failure
is fatal. Format and jitter options are validated before opening the device.
The result includes `queued_frames_at_end`, the ring depth sampled after the
producer has finished, `queue_high_water_frames`, the largest producer-side
admission depth observed during successful writes, and `queue_capacity_frames`.
The concurrent consumer may advance between snapshots, so the admission-side
high-water value can conservatively overstate instantaneous occupancy; it is
bounded by ring capacity. The probe stops after the requested block count and
drops any final queued pre-roll when closing; it is not an end-of-stream drain
test. `telemetry_samples` contains bounded, approximately one-second snapshots
with elapsed nanoseconds, producer block sequence, queue depth, current rate
correction, and cumulative rejected-block, underrun, missing-frame, and stream
failure counters. Sampling occurs on the producer thread after a block write;
timestamps are observational and do not pinpoint an ALSA event more precisely
than the sampling interval. `current_rate_correction_ppm` and
`maximum_rate_correction_ppm` report the backend's queue-feedback rate
adjustment; it is bounded to +/-500 ppm. The probe rejects durations exceeding
24 hours, accounting for the requested producer clock offset and injected
jitter, so its telemetry storage remains bounded. Unexpected runtime delays
that exhaust the preallocated telemetry samples set `telemetry_overflow` and
fail the probe. A nonzero exit status means invalid input, backend/producer
failure, rejected blocks, underruns, telemetry overflow, or impossible queue
metrics. Producer lateness includes deliberately injected jitter and is not
hardware latency. Record negotiated PCM geometry separately from requested
values, for example from `/proc/asound/card*/pcm*p/sub*/hw_params` while
running.

### Opt-in ALSA Worker Events

Add `--backend-events 1` to enable bounded worker diagnostics; the default is
`0`. Other backends reject this probe option. Ordinary module playback does not
enable diagnostics or add timing/status calls. Capture adds platform-clock
reads to the in-band ALSA worker and `snd_pcm_avail_delay`/state queries on
selected events, so compare instrumented results cautiously: it is an observer,
not an unchanged timing environment.

The probe preallocates its one-second telemetry capacity plus 32,768 event
slots before opening the backend, so longer probes have bounded but
duration-scaled capture. The worker never allocates or prints events.
`backend_diagnostics` is `null` when disabled; otherwise it includes negotiated
device sample rate, period/buffer frames, capacity, dropped-event count, status
query failures, and `events`. Full storage retains the first events, counts
subsequent selected events as `dropped_events`, and makes the probe exit nonzero.
ALSA status-query errors are preserved per event and counted, not silently
converted to valid device metrics. They do not independently fail the probe:
for example a status query can fail while ALSA is in an error state.

Events are selected approximately once per second, on correction-limit
entry/exit/sign changes, on write errors (other than ordinary `-EAGAIN`), on
wait errors/timeouts, and on slow worker iterations/writes/waits. The slow
threshold is the duration of the larger of four negotiated device periods and
two requested producer blocks (20 ms for the H4 240/480-frame setup).
The `reasons` bitmask is:

| Bit value | Meaning |
| --- | --- |
| 1 | Periodic snapshot |
| 2 | Worker iteration gap at or above the threshold |
| 4 | Slow ALSA write |
| 8 | Slow ALSA wait |
| 16 | Write error or unexpected zero write |
| 32 | ALSA wait returned an error or timed out |
| 64 | Entering, leaving, or changing sign at the correction limit |
| 128 | Wait scheduling-counter open/read/validation failure |

`timestamp_ns` is the worker iteration start on the CoreRaT platform monotonic
clock; `observation_timestamp_ns` is taken after write/wait/recovery, immediately
before the selected device-status query. Subtract the top-level
`producer_start_timestamp_ns` to correlate with producer telemetry; worker
startup events can precede the producer start. `worker_gap_ns` measures time
between iteration starts, not Linux scheduler latency. Write/wait durations
include preemption inside those calls. Queue depths are captured before and
after the operation; they are concurrent observations, not a synchronized
transaction.

`source_frames_consumed` and `device_frames_written` are cumulative successful
queue-consumption and ALSA-submission counts, not hardware playback counters.
`requested_frames`/`write_result` report the write request and signed ALSA
return; zero requested frames means that iteration did not write.
`wait_result` applies only to a `write_result` of `-EAGAIN`;
`recovery_result` applies to other unsuccessful writes. Recovery preserves the
existing playback policy. Device state uses ALSA's numeric state enum;
availability/delay are signed frame counts and valid only when
`device_status_result` is zero. Status is sampled after recovery, so a successful
recovery can hide the prior XRUN state, but not its write error. These
observations help distinguish worker stalls, ALSA backpressure, and device
progress; they do not establish a hardware clock offset or OOB residency.

The optional `AudioOutputConfig::diagnostics` pointer is caller-owned storage,
supported by ALSA only. Its event span and capture object must outlive
`close()`. Inspect them only after `close()` has joined the worker; they are not
atomic live telemetry. Reopening resets capture metadata without replacing
the caller's span.

For deeper in-band wait diagnostics, add `--wait-scheduling 1` together with
`--backend-events 1`. This is disabled separately by default. On its own ALSA
worker, the backend opens `/proc/thread-self/schedstat` once and performs two
bounded reads around each `snd_pcm_wait`. This is diagnostic-only Linux
in-band file I/O, never producer/module-callback or EVL OOB work. It adds
overhead on every wait, including waits that do not produce a retained event.
The confirmed H4 B image exposes these counters but not tracefs scheduler
events; a temporary trace-capable A-slot trial is documented in the
[H4 hardware capture](HARDWARE_VALIDATION.md#trace-correlated-h4-wait-capture).

For retained events with a `write_result` of `-EAGAIN`, the extra fields are:

- `wait_start_timestamp_ns`, paired with `wait_duration_ns`, brackets the
  actual ALSA wait separately from counter-read overhead.
- `scheduling_before_timestamp_ns` and `scheduling_after_timestamp_ns` bracket
  both counter reads and the wait on the same CoreRaT clock.
- `scheduling_runtime_ns`, `scheduling_runnable_delay_ns`, and
  `scheduling_timeslices` are differences in the worker's Linux CPU-runtime,
  runnable-but-not-running delay, and timeslice counters.
- `scheduling_result` is zero on valid reads or a negative errno on failure.
  Failed reads/parses or decreasing counters emit reason 128 and increment
  `scheduling_failures`; any such failure makes the probe exit nonzero.

The counters are scheduler-accounting observations, not exact syscall time
partitions. Accounting can be updated at context switches, so runtime deltas
can include execution before the first snapshot, and accounting granularity
or disabled kernel accounting can produce zeros. Do not interpret a zero
run-queue delta alone as proof that scheduling played no role, or subtract
these counters from wall time and label the remainder hardware delay.
Repeated slow waits with substantial run-queue deltas support scheduling
contention. Repeated long waits with zero runnable delay and little CPU-runtime
support the worker being blocked/asleep inside the wait rather than runnable
and waiting for ordinary Linux CPU scheduling; they still do not distinguish
ALSA/plugin wait behavior, driver, interrupt, or device-readiness delays.
No kernel scheduling settings are changed by this probe option. Scheduler
trace events are needed for direct wakeup-to-run timing, as measured in the H4
trial, but do not by themselves identify driver/interrupt or device-readiness
delays. Interpret IRQ-pipeline tracepoints separately from ordinary
`irq_handler_entry`: pipeline entry/exit mark the initial IRQ pass, while an
in-band handler can be deferred until pipeline synchronization. Their timestamp
gap alone is not proof of late physical IRQ delivery; see the
[H4 trace findings](HARDWARE_VALIDATION.md#hda-irq-correlation-capture-2026-10-23).
A later filtered H4 capture associated one HDA handler with
`__inband_irq_enable` during `kauditd` RCU callback processing, but did not
identify what held the in-band stage stalled. Its probe-side backend recorder
dropped events before the streamed trace began, so the two timelines could not
be correlated for the later underruns; see the
[filtered in-band re-enable follow-up](HARDWARE_VALIDATION.md#filtered-in-band-re-enable-follow-up-2026-10-24).
A subsequent no-kprobe capture retained the complete probe event stream and
again correlated slow ALSA waits with long HDA pipeline intervals. Its
all-CPU scheduler follow-up measured median worker wake-to-run latency of
2-11 microseconds, with a 1.57 ms maximum; all long pipeline-to-handler
intervals overlapped `kauditd` execution on CPU0, but that remains correlation
rather than proof of cause. See the
[no-kprobe capture](HARDWARE_VALIDATION.md#no-kprobe-correlated-h4-capture-2026-10-24)
and its [all-CPU scheduler follow-up](HARDWARE_VALIDATION.md#all-cpu-worker-scheduler-follow-up-2026-10-24).
The filtered preemptirq follow-up captured 50 paired `kauditd` IRQ-off spans
in `console_flush_all`. All 19 waits longer than 20 ms overlapped one of these
spans and a long HDA pipeline/handler interval; both `-EPIPE` waits ended
within about 50 microseconds of a deferred HDA handler running immediately
after re-enable. Kernel-source review found that the legacy printk path calls
the console writer with IRQ state saved, and the 8250 serial-console writer
sends synchronously while holding an IRQ-saving UART lock. The H4 used
`ttyS0` at 115200 baud, and no userspace `auditd` process was present, making
audit fallback printing a plausible trigger. This substantially strengthens
the mechanism hypothesis, but no intervention has yet tested causality, and
the trace does not prove physical HDA IRQ masking or explain the audible
oscillator clicks. See the
[filtered preemptirq and console correlation](HARDWARE_VALIDATION.md#filtered-preemptirq-and-console-correlation).

The stability result varied across repeated runs (including both zero and two
underruns), so the output path is not qualified. A console-threshold
comparison was started on unconfirmed A with the printk console level lowered
to 3 while preserving kernel log-buffer and audit settings, but the
one-hour capture was interrupted before completion. Its trace is truncated
and the probe result was lost when A fell back to confirmed B; it yields no
valid comparison. See the
[interrupted console-threshold trial](HARDWARE_VALIDATION.md#console-threshold-causality-trial-interrupted-2026-10-25).
The causality test and an audible oscillator listening check are deferred;
the H4 is back on B with the interactive gain/pan demo running.

Do not interrupt a live demo for automated testing without approval. CPU/I/O
stress with the new queue metrics, producer/device clock-skew measurement,
installed-image validation, and hardware latency measurements remain explicit gates in the
[roadmap](../ROADMAP.md). Record results and qualification limits in the
[hardware validation matrix](HARDWARE_VALIDATION.md).

## Release Image Pinning

Build a release-candidate RaTOS image only after the dependency changes are
committed and pushed. Before starting the image build:

1. Require clean CoreRaT, CommRaT, MusicRaT, and RaTOS worktrees.
2. Confirm each local release branch matches its upstream branch.
3. Pin the CoreRaT, CommRaT, and MusicRaT recipes to those exact upstream commit
  IDs; release recipes must not use `${AUTOREV}` or a temporary feature branch.
4. Regenerate the RaTOS SDK so it contains the pinned development packages.
5. Configure and build MusicRaT with the regenerated SDK before building the
  `ratos-musicrat-image` KAS target.

Record all four repository commit IDs with the image artifact. A successful
build from a dirty source checkout or an unpinned recipe is development evidence,
not a reproducible release image.