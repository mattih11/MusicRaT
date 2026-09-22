# EVL and RaTOS Integration

MusicRaT targets both the standard Linux CommRaT backend and the Xenomai 4/EVL backend shipped by RaTOS. EVL compatibility is a cross-cutting constraint, not a port to perform after the audio graph is complete.

This document separates checks that can run on every development host from checks that require the RaTOS SDK, an EVL kernel in QEMU, or physical audio hardware.

## Verified Upstream Foundation

CommRaT currently provides two complementary CMake presets:

- `evl-cross` cross-compiles against the RaTOS ISAR SDK.
- `evl` builds natively inside a RaTOS QEMU guest.

CommRaT's `scripts/evl-dev.sh` can acquire or use local SDK and image artifacts, cross-compile, copy binaries into a QEMU guest, run tests under the EVL kernel, and regenerate module descriptors in the guest. MusicRaT should adopt this workflow rather than maintain a separate VM protocol.

RaTOS currently provides `ratos-commrat-image` as the smallest image containing MusicRaT's runtime dependencies. Build its container-amd64 variant with:

```bash
cd /path/to/RaTOS
kas-container --isar build \
  kas.yaml:kas/board/container-amd64.yaml:kas/target/commrat.yaml
```

The resulting ext4, kernel, and initrd are under `build/tmp/deploy/images/container-amd64/`. They are suitable as the base guest for MusicRaT development because CommRaT is installed while MusicRaT is not.

RaTOS does not yet contain a MusicRaT package or image recipe. Adding `musicrat_git.bb`, a `ratos-musicrat-image` image, and a matching KAS target is future integration work. The intended stack is:

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

MusicRaT must gain an `evl-cross` preset using the same RaTOS SDK toolchain as CommRaT. This stage must compile all public headers, modules, tests, and the launcher against the SDK and preserve one generated audio-policy ABI across every binary.

Cross-building proves SDK and dependency compatibility. Cross-compiled module executables must not be run on the host merely to generate descriptors.

### 3. EVL QEMU Runtime

Deploy the cross-built tree to `ratos-commrat-image` and run under its EVL kernel. This stage must:

- Run protocol, DSP, module, and headless integration tests
- Generate complete module descriptors using `--commrat-inspect` in the guest
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
3. EVL QEMU CTest, in-guest descriptor generation, and controlled-graph smoke test.
4. RaTOS recipe and image build triggered by pinned MusicRaT releases.
5. Scheduled or release-gated physical-board stress and latency tests.

EVL cross-build and QEMU failures block changes that affect runtime code. Physical tests gate supported-hardware and real-time performance claims rather than routine DSP-only development.