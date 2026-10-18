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
`musicrat_audio_output_backend_alsa_test`, and `musicrat_audio_device_sink_test`,
then run:

```bash
bash scripts/validate-audio-runtime.sh std build-alsa-validation \
  tests/fixtures/alsa_null.conf
```

For an already booted EVL QEMU guest, copy those four SDK-built executables,
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

This emits a quiet 440 Hz sine and a JSON health result. The producer uses
CoreRaT absolute deadlines on a platform-managed thread; EVL attachment failure
is fatal. Format and jitter options are validated before opening the device.
The probe stops after the requested block count and drops any final queued
pre-roll when closing; it is not an end-of-stream drain test. A nonzero exit
status means invalid input, backend/producer failure, rejected blocks, or
underruns. Producer lateness includes deliberately injected jitter and is not
hardware latency. Record negotiated PCM geometry separately from requested
values, for example from `/proc/asound/card*/pcm*p/sub*/hw_params` while running.

Do not interrupt a live demo for automated testing without approval. CPU/I/O
stress, queue-occupancy/skew measurement, installed-image validation, and
hardware latency measurements remain explicit gates in the
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