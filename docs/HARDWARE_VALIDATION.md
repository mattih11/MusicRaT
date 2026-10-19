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

## Pinned SDK Validation

The published stabilization stack was built from clean, upstream-matching
worktrees:

- CoreRaT: `db1107e837172e3668b1cca7656652e02f25187b`.
- CommRaT: `9aeea45c70099045dea17f55f905b324da1285ff`.
- MusicRaT: `f2d4688d2378bd6178c8a5ede476f6d3e59352e6`.
- RaTOS: `8e5eb6a6fab05e1a4729d2806094a0b7bba12a05`.

The regenerated `ratos-dev-image-sdk-ratos-container-amd64.tar.xz` passed all
142 SDK build tasks. Archive SHA256:
`207c7d8aae46adf2b775dd50e9ee1ba47a0a6cbf65f5ff70d8ec7af15abd0000`.
It contains matching CoreRaT/CommRaT development packages and descriptor
tooling, including the absolute-deadline helper, plus ALSA headers, pkg-config
metadata, and libraries (`libasound2-dev` version `1.2.14-1+deb13u1`).

The complete ALSA MusicRaT test/module build passed using only this SDK, with
no local header, library, or descriptor-tooling overlays. In a separate QEMU
guest running the cached `6.12.69-cip17` EVL kernel:

- The regenerated-SDK runtime gate passed upstream `clock-timer-periodic`,
  `simple-clone`, and `detach-self`, plus pre-roll, ALSA backend, and audio sink
  regressions.
- The 10-block null-device probe reported the `evl` platform and zero rejected
  blocks, underruns, missing frames, or stream failures.
- The oscillator -> gain -> level meter -> WAV graph passed its bounded
  integration test, checking WAV format, frame bounds, and nonzero signal.

The guest used the SDK's matching `libevl` and `libasound` in an isolated
runtime library directory; its base image and kernel were not rebuilt by this
SDK check. This proves SDK compatibility and the EVL runtime path, not a newly
installed image, physical audio performance, or sustained clock stability.

## Pinned H4 Image Build and QEMU Smoke Test

The same pinned stack passed all 170 H4 image build tasks. Its manifest records
`musicrat`, `musicrat-examples`, and `libmusicrat-dev` version `1.0.1+git1`,
`libcommrat-dev` version `1.1.4+git1`, and `libcorerat-dev` version `0.1.8`.

Fresh deployment artifact:
`ratos-musicrat-image-ratos-odroid-h4.wic.gz` (306765505 bytes), SHA256:
`3e4919e9f5cc33e6a24a319c290d3debeb8721538b30f15e1a4cc77c26bf54b0`.
Gzip integrity validation passed. An older `.wic.zst` remains in the deployment
directory; it is not an artifact of this build and must not be substituted.

A copy of the new image's root partition booted in QEMU with its packaged
`6.12.69-cip17` EVL kernel and initramfs. Only an ephemeral SSH public key was
injected into that copy. The installed EVL router was active, the three upstream
EVL sanity tests passed, and the installed graph launched without source-tree,
build-tree, or extra runtime-library dependencies:

```bash
musicrat_launcher \
  /usr/share/musicrat/examples/tone_through_processors_to_wav.json \
  --descriptor-dir /usr/share/musicrat/modules --duration-ms 300
```

Its output was verified as mono 48 kHz, 16-bit PCM WAV containing 24960 frames
and nonzero signal. The guest was shut down after validation. This direct-kernel
QEMU check does not exercise EFI boot, physical ALSA playback, DRM/touch input,
or the installed gain/pan demo on H4. The physical board was not changed or
rebooted; its temporary interactive demo remained running.

## Qualification Still Required

- MusicRaT A/B SWUpdate and attended-installer integration now passes
  development EFI QEMU tests, including signed slot updates, confirmation,
  watchdog-triggered rollback, persistent state, and installation/update on a
  disposable virtual NVMe. The RaTOS deployment guide records artifact hashes
  and test limits. The tested artifacts were built from a dirty development
  worktree; clean release artifact builds, deployment-owned signing
  credentials, and physical H4 qualification remain separate gates.
- Deployment of the rebuilt pinned H4 image, reboot, and an installed gain/pan demo without
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
