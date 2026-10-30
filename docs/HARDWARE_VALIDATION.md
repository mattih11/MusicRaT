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

## Clean A/B Release Build and EFI Smoke Test

On 2026-10-19, the MusicRaT A/B image, signed update bundle, and attended
installer were rebuilt from clean, upstream-matching RaTOS
`294dd751850cbe05a1d619c1f50845a261c7c3c1`, with the same pinned runtime
dependencies listed above. The image and installer passed all 383 and 491 build
tasks respectively, reusing cached tasks. Both root filesystems record
`BUILD_ID="v0.3.4-9-g294dd75"`; the target image UUID is
`8d0a31c6-d7b5-4162-a084-e326177d2ab3`. This is not a bit-reproducibility claim.

| Artifact | SHA256 |
| --- | --- |
| A/B `.wic.gz` | `ca3bbc9be1d4663754f69254edc4f3ce7ba674440a9b8cdbef6c3e862faffc76` |
| Normal `.swu` | `9178dcb254789ffd4eff67865bd685307428b589ed900fdb21e5ac13822a90ee` |
| Installer `.wic.gz` | `60b71c0acc9fc5a1bb4f62898a81e29e3eb8d49a9e59f76f783ffa90c3465753` |

Compression integrity, the actual GPT layout, five layout regressions, and
CMS signature verification passed. The payload extracted from the actual
installer disk matched the target `.wic.zst` byte-for-byte, and its decompressed
content matched the clean factory disk.

A disposable factory disk booted through OVMF and EFI Boot Guard into SquashFS
slot A with persistent `/var` and systemd servicing the emulated one-minute
i6300ESB watchdog. A separate previous-development-image guest accepted the
clean signed bundle into B, booted the new UUID/build ID, preserved a `/var`
marker, passed health checks, and was confirmed. B survived a further reboot;
same-image reinstallation was rejected without switching slots.

Both clean factory and update boots passed the three installed EVL sanity
tests and ran the installed oscillator -> gain -> meter -> WAV graph with the
EVL router active. Output was nonzero mono 48 kHz, 16-bit PCM, with 21120 and
24480 frames respectively. No source/build-tree or extra runtime-library
dependencies were deployed. Only ephemeral SSH access was seeded in the
disposable `/var` overlays; VMs were shut down and disks/private keys removed.

The clean smoke test did not repeat the earlier attended virtual-NVMe write
or watchdog-triggered rollback. Signing still uses development certificates.
The physical H4 was not accessed, flashed, or rebooted during this stage.

## Physical H4 A/B Installation Smoke Test (2026-10-19)

The operator installed the validated USB image and booted the H4. SSH inspection
verified build ID `v0.3.4-9-g294dd75`, image UUID
`8d0a31c6-d7b5-4162-a084-e326177d2ab3`, and EVL kernel `6.12.69-cip17`.
The Lexar NM790 NVMe now has the six expected A/B partitions, with SquashFS
slot A (`/dev/nvme0n1p4`) mounted read-only and the 512 MiB ext4 `/var` mounted
writable. EFI Boot Guard reports BOOT0 revision 2 / OK; B is initially empty.

The EVL router was active and no systemd units were failed. Systemd reported
servicing the physical `iTCO_wdt` watchdog (version 6) with a one-minute timeout.
This proves driver detection/servicing setup, not watchdog reset or fallback
recovery. The guest journal's wall-clock date was not synchronized with the
development machine; the date above records the host-side observation.

The three installed EVL sanity tests passed. The installed oscillator -> gain
-> meter -> WAV graph produced nonzero mono 48 kHz, 16-bit PCM with 24480 frames.
Physical ALSA playback, LVGL/touch, signed slot updates and rollback have not
yet been qualified on this installation. No remote update, confirmation, or
reboot was initiated during these checks.

The installed `h4_tone_to_device_lvgl.json` graph was subsequently launched
using only `/usr/bin` modules and installed descriptors/manifests. The operator
confirmed the display, audible oscillator, and both gain/pan touch controls
worked. HDMI-A-1 was connected; touch used the
`usb-wch.cn_USB2IIC_CTP_CONTROL-event-if00` device. Physical ALSA reported stereo
48 kHz S32_LE, MMAP_INTERLEAVED, a 1024-frame period and 16384-frame buffer.
This completes the installed interactive smoke test, not sustained playback,
latency, clock-skew, or dropout qualification. The demo remains running;
physical signed updates and rollback remain pending.

A subsequent `h4-lab-2` signed bundle was prepared with image UUID
`536ee542-ef62-4224-82ea-6471bf74e3a9` to populate B in a later approved
operation. Bundle SHA256:
`e1682f0aa2cfde0406963f6b814da04ef1012dd6e6a3e807e1bf57ebed780900`.
It is based on RaTOS `294dd75` plus documentation edits and a reflect-cpp pin
to the factory revision `eb82ef6d1814b7907f467cf5e2ee8421bd77135c`;
the rootfs correctly reports a dirty build ID. A floating recipe initially
selected a newer dependency; that attempt was stopped before image generation.
The completed bundle's package/version manifest matches the factory image.

The bundle signature, payload hashes, image identity, compatibility and signing
certificate were verified. Its certificate matches the live H4's trust
certificate. In a disposable EFI QEMU guest booted from the clean factory disk,
this exact bundle installed into B, passed EVL and installed audio graph health
checks, preserved `/var` state, and survived a confirmed reboot. Nonzero mono
48 kHz, 16-bit WAV output contained 24960 frames. The VM was shut down; the
physical H4 has not received this update and its installed demo remains running.

Reinstallation changed the SSH host key, which the operator verified on the
physical console. Ordinary `ssh-copy-id` failed because `/root` is read-only;
the operator installed the development machine's public key under the
persistent `/etc` overlay and reloaded SSH. Key-authenticated access then
succeeded. No private key was deployed.

## Physical H4 Slot-B Update (2026-10-19)

The H4 started on confirmed factory slot A
(`8d0a31c6-d7b5-4162-a084-e326177d2ab3`) with the interactive gain/pan graph
running. After the operator approved the update, the graph was stopped
gracefully. The exact lab SWU was checked against SHA256
`e1682f0aa2cfde0406963f6b814da04ef1012dd6e6a3e807e1bf57ebed780900`; its
detached CMS signature and payloads had already been verified, and its
development signing certificate was matched to the certificate on the target.
The file was copied to `/var/tmp`, checked again on-device, and SWUpdate
reported success writing inactive slot B. The temporary bundle was removed
after confirmation.

After reboot, EFI Boot Guard entered slot B in `TESTING`, with UUID
`536ee542-ef62-4224-82ea-6471bf74e3a9` mounted from `/dev/nvme0n1p5`. Slot A
remained `OK` at revision 2; B was revision 3. The EVL router was active, no
systemd units had failed, and systemd reported servicing the physical iTCO
watchdog at 60 seconds. The three installed EVL sanity tests and the installed
oscillator -> gain -> meter -> WAV graph passed; output was nonzero mono
48 kHz, 16-bit PCM with 24480 frames.

The installed gain/pan touchscreen graph was relaunched on B. The operator
confirmed the display, audible oscillator, and both touch controls worked.
Only after these checks, `bg_setenv -c` succeeded and EFI Boot Guard reported B
`OK` at revision 3. Final inspection confirmed B remained mounted, the demo was
still running, the SWUpdate socket and progress service remained enabled, and
`/var` had 436 MiB free. No reboot was made after confirmation.

The confirmed B slot was rebooted once on the H4 after the update trial. The
boot ID changed, the system returned on `/dev/nvme0n1p5` with the expected UUID,
and EFI Boot Guard continued to report A revision 2 / OK and B revision 3 / OK.
The EVL router was active, systemd reported the one-minute physical iTCO
watchdog, no units were failed, and the update bundle was absent from `/var`.
The gain/pan demo was restarted and its ALSA PCM reported `RUNNING`. The
operator confirmed it was running successfully after the reboot.

This was an on-device update trial using development certificates, not a
production trust configuration. No backup was made, as explicitly declined by
the operator.

## Physical H4 Audio Under Load (2026-10-19)

The operator approved stopping the interactive demo for bounded physical ALSA
testing. A temporary SDK-built EVL probe (SHA256
`e1d6da7ace1d4f6d8b7488af743ee4c8641a378a1f7955d468cd9c46142e77a4`) ran from
`/tmp` against `plughw:0,0`; the probe was removed afterward. Its SDK
`libevl.so.6` and `libasound.so.2` SHA256 values exactly matched the H4
libraries. The probe requests 48 kHz stereo, 480-frame blocks, S16_LE output.
The physical ALSA device negotiated RW_INTERLEAVED, 48 kHz stereo, S16_LE,
240-frame periods and a 960-frame buffer.

All runs reported zero rejected blocks, underruns, missing frames, and stream
failures:

| Run | Blocks / duration | Load | Max producer lateness |
| --- | --- | --- | --- |
| Baseline | 3,000 / 29.990 s | None | 18,757 ns |
| CPU | 3,000 / 29.990 s | One low-priority `yes` worker pinned to CPU 0 | 3,868 ns |
| I/O | 3,000 / 29.990 s | 67 x 8 MiB direct writes with `fdatasync` (562,036,736 bytes) | 19,492 ns |
| CPU + I/O | 3,000 / 29.990 s | Same one-core load and 67 x 8 MiB direct writes | 6,282 ns |
| CPU + I/O + stalls | 30,000 / 299.990 s | One-core load; 592 x 4 MiB direct writes with `fdatasync` (2,483,027,968 bytes); injected 16 ms producer stall every 37 blocks | 16,010,959 ns |

While PCM was running, `/proc/asound/card0/pcm0p/sub0/status` was sampled at
about 10 Hz for the short runs and 1 Hz during the five-minute run. Its reported
`delay` ranged from 527 to 1,005 frames in the short CPU/I/O tests and 671 to
1,002 frames during the five-minute run; reported `avail` ranged from 0 to 464
frames. These kernel PCM delay/availability values include device-side
accounting and are not the MusicRaT producer ring's occupancy. The probe used
for these results did not expose internal queue depth. The backend and probe
now report end-of-producer depth, enqueue-side high-water depth, and capacity;
the follow-up physical measurements are recorded in the queue-occupancy section
below. The injected stall explains the approximately 16 ms worst producer
lateness in the final run; it did not produce reported underruns or missing
frames. This test did not measure queue depth and is not a clock-skew, latency,
or dropout-resistance qualification.

After testing, all temporary probe/load files were removed and the gain/pan
demo was restarted. Final inspection found confirmed slot B, both slots `OK`,
the EVL router active, no failed systemd units, and the ALSA PCM `RUNNING`.

## Physical H4 Queue Occupancy Probe (2026-10-19)

After building and testing the occupancy instrumentation on native ALSA and
cross-building it with the RaTOS EVL SDK, the operator approved one brief
physical run. Probe SHA256 was
`e25c1acc4022215cc921dbf7f690834e31b171234660800722d4f7a564a9fca3`; its EVL
and ALSA libraries matched the H4 runtime copies by SHA256. The probe requested
`plughw:0,0`, 48 kHz stereo, 480-frame blocks, and injected 16 ms producer
stalls every 37 blocks for 3,000 blocks (29.990 s).

The result reported zero rejected blocks, underruns, missing frames, and stream
failures. Maximum producer lateness was 16,011,109 ns. The internal ring
high-water was 2,344 frames (about 48.8 ms), end-of-producer occupancy was
2,104 frames (about 43.8 ms), and configured ring capacity was 16,384 frames
(about 341.3 ms). High-water is the producer's enqueue-side estimate and can
conservatively overstate instantaneous occupancy when the consumer advances
concurrently. This short jitter run establishes a first physical ring-depth
sample, not bounded long-duration behavior under combined CPU/I/O load or
independent clock drift.

The probe was removed after the run and the gain/pan demo was restarted on
confirmed slot B. The launcher and audio sink are active, the PCM reports
`RUNNING`, the EVL router is active, and no systemd units are failed.

With approval to repeat the test under load, a five-minute run used the same
48 kHz stereo, 480-frame probe and 16 ms injected producer stalls every 37
blocks for 30,000 blocks (299.990 s). One low-priority `yes` worker was pinned
to CPU 0. An idle-I/O-priority workload repeatedly overwrote one 4 MiB
`/var/tmp` file using direct writes and `fdatasync`, with a 0.5 s interval
between writes. The run again reported zero rejected blocks, underruns, missing
frames, and stream failures. Maximum producer lateness was 16,008,196 ns;
ring high-water was 1,920 frames (40 ms), end-of-producer occupancy 1,144
frames (about 23.8 ms), and capacity remained 16,384 frames (about 341.3 ms).
The measured high-water was 11.7% of capacity. As above, it is an
enqueue-side estimate that may conservatively overstate instantaneous depth.
This demonstrates bounded queue occupancy during this five-minute load profile,
not multi-hour operation or independent producer/device clock-drift handling.

The stress services were stopped and the probe and I/O file were removed. The
gain/pan demo was restarted on confirmed slot B; its launcher and sink are
active, PCM is `RUNNING`, the EVL router is active, and no systemd units are
failed.

With adaptive resampling enabled, the five-minute CPU/I/O/jitter test was
repeated. It used the same 30,000 blocks, 16 ms producer stall every 37 blocks,
one low-priority CPU worker pinned to CPU 0, and idle-priority repeated 4 MiB
direct writes with `fdatasync` to one reused file. The probe completed in
299.990 s and reported zero rejected blocks, underruns, missing frames, or
stream failures. Maximum producer lateness was 16,008,110 ns. Queue high-water
was 2,271 frames (about 47.3 ms), end-of-producer occupancy was 2,032 frames
(about 42.3 ms), and capacity was 16,384 frames (about 341.3 ms). The reported
current rate correction was -92 ppm; maximum absolute correction was 479 ppm,
near the configured +/-500 ppm clamp. This indicates the controller sometimes
approached its bound under the injected-stall/load profile, although the queue
remained bounded and no playback health errors were counted. The result is not
a clock-offset or listening-quality qualification.

The stress workers, 4 MiB file, and probe were removed. The gain/pan demo was
restarted on confirmed slot B and verified with PCM `RUNNING`, active EVL
router, and no failed systemd units.

## Physical H4 Producer Clock-Offset Tests (2026-10-19)

The operator approved stopping the demo for two five-minute adaptive-probe
runs against the H4's physical ALSA device. Both requested 48 kHz stereo,
480-frame producer blocks, and 30,000 blocks; no external CPU/I/O load or
injected scheduler stalls were used. The new `--clock-offset-ppm` setting
changes only the probe's absolute producer deadlines, leaving the device clock
unchanged.

| Producer offset | Elapsed | Max producer lateness | Rejected / underruns / missing frames / failures | Queue high-water / end / capacity | Max correction |
| --- | --- | --- | --- | --- | --- |
| +100 ppm | 299.960 s | 17,200 ns | 0 / 0 / 0 / 0 | 2,518 / 2,257 / 16,384 frames | 315 ppm |
| -100 ppm | 300.020 s | 12,666 ns | 0 / 0 / 0 / 0 | 2,360 / 1,574 / 16,384 frames | 262 ppm |

The output also reported current correction of -35 ppm after the +100 ppm
producer run and -206 ppm after the -100 ppm run. That value is read after the
producer exits while the backend worker continues to drain; maximum absolute
correction is the more useful run-wide figure. Both five-minute runs completed
without the correction approaching its configured +/-500 ppm limit or any
counted playback health error. These are controlled producer-offset tests, not
an independently measured oscillator-clock offset or subjective listening
assessment.

The probe was removed and the gain/pan demo restarted on confirmed slot B. The
launcher and sink are active, PCM is `RUNNING`, the EVL router is active, and
there are no failed systemd units.

## One-Hour Physical H4 Producer Clock-Offset Tests (2026-10-19)

With approval to pause the gain/pan demo, the ALSA/EVL probe ran for one hour
at each requested producer offset against the H4 ALC897 analog output
(`plughw:0,0`). Both runs requested 48 kHz stereo and 480-frame producer blocks,
with no external CPU/I/O load or injected scheduler stalls. The physical PCM
negotiated S16_LE, 48 kHz stereo, a 240-frame period, and a 960-frame buffer.

| Producer offset | Elapsed | Max producer lateness | Rejected / underruns / missing frames / failures | Queue high-water / end / capacity | Max correction |
| --- | --- | --- | --- | --- | --- |
| +100 ppm | 3599.630 s | 23,348 ns | 0 / 3 / 720 / 0 | 5,836 / 2,483 / 16,384 frames | 500 ppm |
| -100 ppm | 3600.350 s | 22,336 ns | 0 / 1 / 240 / 0 | 5,591 / 1,676 / 16,384 frames | 500 ppm |

Both runs reached the configured +/-500 ppm maximum correction and exited
nonzero because ALSA reported underruns. Queue occupancy remained well below
capacity and no producer blocks were rejected, but neither run meets the
zero-underrun stability criterion. The runs do not identify whether the
correction saturation or an independent scheduling/device event caused each
underrun; the probe does not retain a time series of queue depth and correction.
These injected producer offsets are not a direct measurement of the physical
oscillator-clock difference.

The probe was built from MusicRaT `817e49fac384808995ddc106f12cb47d6efd37ba`
with the EVL ALSA backend, using CoreRaT
`db1107e837172e3668b1cca7656652e02f25187b` and CommRaT
`9aeea45c70099045dea17f55f905b324da1285ff`. The target reported Debian 13
RaTOS image build ID `v0.3.4-9-g294dd75-dirty` and kernel
`6.12.69-cip17`. The probe/backend sources were unchanged from the MusicRaT
revision; unrelated Designer and documentation changes made the worktree
dirty. After each run, the gain/pan demo was relaunched and verified active
with PCM `RUNNING`; the EVL router remained active. No installed binary,
configuration, or image was changed.

## Telemetry Follow-up to One-Hour Clock-Offset Tests (2026-10-20)

After adding bounded one-second queue/correction/counter samples, the operator
approved repeating both one-hour H4 producer-offset tests. The EVL/ALSA probe
was built from MusicRaT `c343ffa`; it requested `plughw:0,0`, 48 kHz stereo,
480-frame blocks, and 360,000 blocks, with no external load or injected
jitter. The physical ALSA negotiated period and buffer sizes were not captured
during these repeat runs. The telemetry produced 3,600 samples for +100 ppm
and 3,601 for -100 ppm, with monotonic timestamps and no sample overflow.

| Producer offset | Elapsed | Max producer lateness | Rejected / underruns / missing frames / failures | Queue high-water / end / capacity | Max correction | Final correction |
| --- | --- | --- | --- | --- | --- | --- |
| +100 ppm | 3599.630 s | 17,764 ns | 0 / 0 / 0 / 0 | 2,670 / 2,295 / 16,384 frames | 313 ppm | -26 ppm |
| -100 ppm | 3600.350 s | 12,570 ns | 0 / 0 / 0 / 0 | 1,920 / 1,470 / 16,384 frames | 353 ppm | -232 ppm |

Neither run reached the +/-500 ppm correction limit. The sampled corrections
ranged from -311 to +65 ppm during the +100 ppm run and -350 to 0 ppm during
the -100 ppm run. No sample reported a rejected block, underrun, missing frame,
or stream failure. Thus, the earlier one-hour result with three underruns and
correction saturation at both offsets was not reproduced. The available
telemetry does not explain the difference, and these clean repeats do not erase
the earlier failures or establish sustained clock stability. Preserve the
bounded correction policy and investigate the discrepancy before closing the
qualification gate.

The gain/pan demo was stopped for each run and restored after it. After the
second run, the demo service was active and the PCM reported `RUNNING`. The
temporary probe and result files were removed from H4; no installed binary,
configuration, or image was changed. The machine-readable timelines were
retained in the session validation artifacts. These controlled producer-offset
tests are not measurements of the physical oscillator-clock difference or
subjective audio quality.

### Controlled Repeats with PCM and Scheduling Capture (2026-10-20)

Both offsets were repeated for 360,000 blocks using the exact same EVL/ALSA
telemetry binary (SHA256
`0a0ff642cbb8519011c99d17ee2dd3a7fd37ee749113ef686d6d60525bbd9885`),
without changing the backend or correction policy. A ten-second physical smoke
test first verified capture and demo restoration. Hardware parameters captured
from the PCM owned by each probe confirmed RW_INTERLEAVED, S16_LE, 48 kHz
stereo, 240-frame periods, and a 960-frame buffer, matching the original
failing runs. There was no injected jitter or external stress workload.
A separate observer captured PCM status, Linux thread scheduling statistics,
and load approximately every 30 seconds; this observation adds some load and
is not an uninstrumented control.

| Producer offset | Elapsed | Max producer lateness | Rejected / underruns / missing frames / failures | Queue high-water / end / capacity | Max correction | Final correction |
| --- | --- | --- | --- | --- | --- | --- |
| +100 ppm | 3599.630 s | 21,707 ns | 0 / 0 / 0 / 0 | 5,266 / 2,306 / 16,384 frames | 500 ppm | -23 ppm |
| -100 ppm | 3600.350 s | 23,406 ns | 0 / 0 / 0 / 0 | 1,920 / 1,687 / 16,384 frames | 346 ppm | -178 ppm |

Both runs exited zero, with 3,600 and 3,601 monotonic telemetry samples and no
telemetry overflow. The +100 ppm run reproduced correction saturation without
reproducing an underrun: 34 snapshots reported +500 ppm, between approximately
4 and 38 seconds. Sampled queue depth rose from 1,199 frames at 3.010 seconds
to 5,022 frames at 4.010 seconds, then recovered toward the usual range.
This approximately 3,800-frame step is far larger than the 4.8 frames/second
introduced by a +100 ppm producer offset at 48 kHz. It is evidence of a
transient queue imbalance, not evidence that steady injected skew alone
required maximum correction. The one-second telemetry and 30-second scheduler
snapshots cannot identify the responsible consumer/device event. Low producer
wake-up lateness does not measure backend-worker stalls.

The -100 ppm sampled corrections ranged from -345 to 0 ppm and did not
saturate. These results establish that correction saturation and a counted
ALSA underrun need not occur together. They do not explain or invalidate the
earlier underruns. The next diagnostic should distinguish backend-worker
progress, ALSA wait/write timing, and device progress around a queue excursion,
using bounded event capture rather than increasing the correction limit or
assuming Linux scheduling snapshots measure EVL out-of-band execution.

The PCM software-parameter snapshots were taken at different startup stages:
+100 ppm was already `RUNNING` with `start_threshold=960`, whereas -100 ppm
was still `SETUP` with `start_threshold=1`. These are not evidence of a stable
per-offset configuration difference; subsequent -100 ppm status samples show
`RUNNING`.

Both probe JSON checksums matched the retrieved copies. After each run the
gain/pan demo was restored; final checks confirmed its service and PCM were
active, the EVL router was present, and no systemd units had failed. No installed
binary, configuration, or image was changed. JSON results, PCM parameters,
context, and scheduling observations are retained with the session evidence.

### Bounded Worker Diagnostic Validation (2026-10-21)

The next diagnostic adds opt-in, caller-preallocated ALSA worker events through
`--backend-events 1`. It records periodic device availability/delay snapshots,
worker gaps, slow writes/waits, errors, and correction-limit transitions.
The queue-feedback policy, correction bound, pre-roll, and recovery decisions
are unchanged. See [the event contract](EVL_AND_RATOS.md#opt-in-alsa-worker-events)
for timestamp correlation, capture limits, and observer effects.

Native recorder tests cover event selection, correction-limit transitions,
full/empty buffers, and explicit dropped-event accounting. Native ALSA probe
tests check enabled/disabled JSON, geometry, monotonic event timestamps and
cumulative frame counts. PipeWire and NONE probe/sink builds and targeted
tests also pass; event capture is explicitly rejected outside ALSA.

An isolated QEMU guest booted the cached `6.12.69-cip17` EVL kernel and passed
upstream `clock-timer-periodic`, `simple-clone`, and `detach-self`, followed by
SDK-built pre-roll, diagnostic-recorder, clock-drift, ALSA-backend, and sink
regressions. Default and diagnostic-enabled null-PCM probes passed. The
diagnostic JSON contained bounded monotonic worker events, negotiated geometry,
and zero dropped events, status-query failures, or playback health errors.
The guest used matching SDK `libevl` and `libasound` from an isolated runtime
directory. This validates the EVL runtime path, not physical timing or the
cause of the H4 queue excursion.

### Physical Worker-Event Captures (2026-10-21)

With operator approval, the diagnostic-enabled EVL/ALSA probe ran for one hour
at each offset, with 360,000 stereo 480-frame blocks at 48 kHz and no injected
jitter or external stress workload. The development probe SHA256 was
`d137db5d13c37ad9f30d2883113bf8dafc84c7ba016e2722bec03b1e4892f35e`;
its changes were uncommitted. Both the backend and physical PCM observations
confirmed 240-frame periods and a 960-frame buffer. Hardware access was
RW_INTERLEAVED/S16_LE; both captured running-state software configurations
reported `start_threshold=960`.

| Producer offset | Elapsed | Max producer lateness | Rejected / underruns / missing frames / failures | Queue high-water / end / capacity | Max correction | Worker events |
| --- | --- | --- | --- | --- | --- | --- |
| +100 ppm | 3599.630 s | 23,889 ns | 0 / 0 / 0 / 0 | 2,671 / 2,312 / 16,384 frames | 315 ppm | 3,592 |
| -100 ppm | 3600.350 s | 21,162 ns | 0 / 0 / 0 / 0 | 1,920 / 1,653 / 16,384 frames | 354 ppm | 3,595 |

Both probes exited zero without correction saturation, telemetry overflow,
dropped worker events, or status-query failures. Their 3,600/3,601 producer
samples and worker-event timestamps were monotonic, as were cumulative worker
source-consumption and device-submission counts. Worker snapshots are scheduled
relative to the preceding selected periodic snapshot, so their counts need not
match the producer's aligned one-second samples.

The +100 ppm capture identifies a specific slow ALSA wait at producer-relative
time 4.676741 seconds: `snd_pcm_writei` returned `-EAGAIN` and the subsequent
`snd_pcm_wait` took 29.082339 ms before returning 1 (ready). During that
iteration queue depth rose from 671 to 2,111 frames while source-consumption and
device-submission counts did not advance. The 1,440-frame increase is exactly
three producer blocks. The next iteration began after a 29.122803 ms gap and
successfully submitted 440 frames. The device status after the wait reported
`RUNNING`, 432 available frames, and 569 delay frames; there was no counted
underrun. This localizes this particular queue buildup to time spent inside
the wait, including any preemption, rather than a producer wake-up delay.
It does not distinguish scheduler delay from ALSA/device readiness behavior,
nor establish the cause of the earlier, larger excursion or underruns.

The -100 ppm capture selected no threshold-exceeding gaps/writes/waits, write
errors, wait errors/timeouts, or correction-limit transitions. Its periodic
snapshots do not establish run-wide sub-threshold timing maxima. Instrumentation
remains an observer effect, so clean captures do not qualify the uninstrumented
path or erase previous failures.

Both result hashes matched the retrieved evidence. The demo was restored after
each run; final checks confirmed its service active, PCM `RUNNING`, the EVL
router present, and no failed systemd units. Temporary experiment files were
removed from H4 after retaining the JSON, PCM, context, and scheduling evidence
in the session. No installed binary, configuration, or image was changed.

### Wait Scheduling-Accounting Diagnostics (2026-10-21)

Read-only inspection found no tracefs scheduler-event interface or installed
`perf`/`trace-cmd` on the H4 kernel, but its per-thread `schedstat` counters are
available. A separate `--wait-scheduling 1` option now brackets each ALSA wait
with bounded reads of the in-band worker's CPU-runtime, runnable-delay, and
timeslice counters. It requires backend-event capture and reports counter-read
windows separately from actual wait duration. This does not change kernel
accounting settings or the playback policy; it does add diagnostic file-I/O
overhead and cannot provide definitive scheduler/IRQ attribution.

Nine targeted native audio regressions passed, including scheduling-counter
parsing/real reads and enabled/disabled probe modes. The EVL cross-build and
isolated QEMU runtime gate passed upstream EVL sanity checks, audio regressions,
and the scheduling-enabled null probe with no capture errors. PipeWire/NONE
compatibility checks passed. A null PCM does not normally exercise the
`-EAGAIN` wait path, so physical capture remains required to validate useful
around-wait accounting evidence.

### Physical Wait Scheduling-Accounting Captures (2026-10-21)

With operator approval, the same diagnostic probe additionally enabled
`--wait-scheduling 1` for both one-hour physical producer-offset runs. The
probe and PCM geometry matched the previous physical captures: 48 kHz stereo,
480-frame producer blocks, S16_LE, 240-frame device periods, and a 960-frame
device buffer. Both used `start_threshold=960`; no injected jitter or external
load was used.

| Producer offset | Elapsed | Max producer lateness | Rejected / underruns / missing frames / failures | Queue high-water / end / capacity | Max correction | Captured events |
| --- | --- | --- | --- | --- | --- | --- |
| +100 ppm | 3599.630 s | 21,766 ns | 0 / 0 / 0 / 0 | 4,288 / 2,285 / 16,384 frames | 500 ppm | 4,725 |
| -100 ppm | 3600.350 s | 20,895 ns | 0 / 0 / 0 / 0 | 1,920 / 1,664 / 16,384 frames | 348 ppm | 3,595 |

Both runs exited zero with monotonic producer and worker timelines, and no
telemetry/event overflow, dropped events, counter-read failures, or ALSA status
query failures. The +100 ppm capture retained three slow waits near the initial
queue excursion, at producer-relative times 8.096430, 8.126277, and 8.167427
seconds. Each ALSA write first returned `-EAGAIN`; the following waits lasted
29.483543, 25.622074, and 29.312958 ms before returning ready. Their queue
depths rose by exactly 1,440 frames each: 717 -> 2,157, 1,685 -> 3,125, and
2,589 -> 4,029. No source frames were consumed or device frames submitted
during each wait. The periodic producer sample at 8 seconds showed 2,129
queued frames, consistent with the middle of this short burst.

Across those same wait intervals, the worker's cumulative scheduler-accounting
deltas reported **zero runnable-delay nanoseconds**, 150,013-450,744 ns of CPU
runtime, and one timeslice per wait. Counter reads bracketed the wait with
29.491257, 25.630249, and 29.318628 ms elapsed, so nearly all observed wall
time was in the wait interval, not counter-read overhead. The ALSA call returned
ready and subsequent device status was `RUNNING`; status samples reported
432-456 available frames and 548-731 delay frames. This evidence supports the
worker being asleep/blocked within the ALSA wait rather than runnable and
waiting for ordinary Linux CPU scheduling during these events. It does not
distinguish ALSA plugin wait semantics, driver/interrupt progress, or device
readiness, and scheduler-accounting granularity remains a limitation.

The +100 ppm run did reach the configured +500 ppm correction limit at some
other time, but the three slow-wait events themselves had corrections of
-300, -58, and +167 ppm. In the -100 ppm run, no wait exceeded the 20 ms event
threshold; the maximum retained `-EAGAIN` wait was 9.764 ms, with zero reported
runnable delay. There were no underruns in either run. Thus, the captures
associate a transient queue surge with long ALSA waits without reproducing the
historical XRUNs, and show that correction saturation is a separate event.
They do not resolve why the earlier one-hour runs reported underruns.

Results were hash-verified and retained in the session evidence. The demo was
restored after each run; final checks confirmed its service active, PCM
`RUNNING`, the EVL router present, and no failed systemd units. Temporary probe,
scripts, and raw copies were removed from H4. No installed binary,
configuration, or image was changed.

### Trace-Correlated H4 Wait Capture (2026-10-21)

A temporary trace-capable EVL kernel was installed as revision 4 in the
previously inactive A slot (trial image UUID
`bfe1a3f9-013e-46d8-a1eb-6f7b63c12cb0`). The signed update bundle was verified
before installation, and A was not confirmed. Runtime tracefs was mounted only
for capture. Scheduler events were filtered to the ALSA worker thread and used
the global `mono` trace clock.

The current EVL/ALSA probe (SHA256
`27f09e8a28f24b074e7c9ca5ee67f1253d422f2ccbab7f62ce24826aa565b196`) ran
60,000 stereo 480-frame blocks at 48 kHz with a +100 ppm producer offset for
599.930 seconds. The physical PCM reported RW_INTERLEAVED/S16_LE, 240-frame
periods, a 960-frame buffer, and `start_threshold=960`. The probe retained 600
producer samples and 4,189 worker events, including 124 `-EAGAIN` waits.
There were three waits above the 20 ms diagnostic threshold:

| Producer-relative wait start | ALSA wait | Queue before -> after | `sched_wakeup` -> worker run | Runnable delay | ALSA status |
| --- | --- | --- | --- | --- | --- |
| 44.325947 s | 31.816 ms | 1,356 -> 2,796 (+1,440) | 6 us | 0 ns | RUNNING; 568 available / 435 delay frames |
| 44.441678 s | 28.494 ms | 2,044 -> 3,484 (+1,440) | 5 us | 0 ns | RUNNING; 416 available / 591 delay frames |
| 320.720425 s | 49.637 ms | 1,963 -> 4,363 (+2,400) | 1 us | 0 ns | RUNNING; 472 available / 533 delay frames |

For each event, the worker switched out in a sleeping state about 4 us after
the measured wait start, was woken only after the long wait interval, and ran
within 1-6 us of `sched_wakeup`. The trace and EVL monotonic timestamps agreed
at these intervals. This directly rules out a long ordinary Linux run-queue
delay as the explanation for these three waits and supports the worker
remaining blocked in the ALSA wait until readiness was reported. It does not
identify whether the pre-wakeup delay came from ALSA/plugin behavior, the
driver/interrupt path, or device readiness; no IRQ trace was collected.

The probe exited successfully with zero rejected blocks, underruns, missing
frames, stream failures, dropped diagnostic events, scheduling-counter
failures, or ALSA status failures. Queue high-water was 4,363 frames and the
maximum correction reached 500 ppm. Tracefs recorded 120,876 worker
`sched_switch` events and 60,438 each of `sched_waking` and `sched_wakeup`.
Every CPU reported zero trace overruns and dropped events. The raw trace and
probe JSON were checksum-verified and retained with the session evidence
(SHA256 `e00a3d1a28b21020f98c2ef0e8393b8451d3695f56bfa0d83819e4d1dca8a3c7`
and `4ff7c315d8b958f4982218ab0f0bd5b775cfef383764640349740547c3c7ed05`,
respectively). This is timing and playback-health evidence, not a listening
assessment or a reproduction of the historical underruns.

After capture, trace events were disabled, tracefs unmounted, and the temporary
probe and capture files removed from H4. Rebooting without confirming A
triggered the expected fallback: A is now marked FAILED and confirmed B
(revision 3, image UUID `536ee542-ef62-4224-82ea-6471bf74e3a9`) is running from
`/dev/nvme0n1p5`. The EVL router is active, the PCM is closed, and no systemd
units failed. No trace settings or probe binary remain on the H4.

### HDA IRQ Correlation Capture (2026-10-23)

Read-only inspection of confirmed B showed that it has no tracefs or
`/sys/kernel/debug/tracing` event interface. Its HDA controller is IRQ 138.
The same kernel source used for the A-slot trace image defines ALSA PCM
`hwptr`/`xrun` tracepoints, HDA `azx_get_position`/`azx_pcm_trigger`
tracepoints, and generic IRQ pipeline/handler events. On the trial image,
the HDA and IRQ events were available, but `snd_pcm:*` events were absent:
the PCM tracepoints require `CONFIG_SND_PCM_XRUN_DEBUG`, which depends on
`CONFIG_SND_DEBUG` and was not enabled. Therefore this capture has no direct
ALSA XRUN tracepoint.

The same signed trace image was installed again in inactive A and left
unconfirmed. The EVL/ALSA probe (SHA256
`27f09e8a28f24b074e7c9ca5ee67f1253d422f2ccbab7f62ce24826aa565b196`) ran
360,000 stereo 480-frame blocks at 48 kHz and +100 ppm for 3,599.630 seconds.
The PCM was S16_LE, RW_INTERLEAVED, with a 240-frame period, a 960-frame
buffer, and `start_threshold=960`. Trace events covered the ALSA worker,
HDA controller position, and IRQ 138 pipeline and handler. The trace was
streamed off-board; all four CPU buffers reported zero overruns and dropped
events.

The probe reported 3,600 producer samples and 3,593 worker events, with zero
rejected blocks, underruns, missing frames, stream failures, dropped events,
scheduling failures, or ALSA status failures. Queue high-water was 3,383 frames,
end depth 2,324 frames, and maximum correction 365 ppm. It retained one wait
above the 20 ms threshold, at producer-relative time 10.480904 seconds:
`snd_pcm_wait` lasted 49.753 ms after `snd_pcm_writei` returned `-EAGAIN`.
Queue depth rose from 983 to 3,383 frames (+2,400); the wait returned ready,
and subsequent ALSA status was `RUNNING` with 472 available and 532 delay
frames. Scheduling counters showed zero runnable delay and 155,064 ns of
runtime. The worker switched to sleep about 5 us after wait start, was woken
at 49.733 ms, and ran at 49.736 ms.

The HDA IRQ trace places two long IRQ 138 intervals inside that wait: pipeline
entries were 19.875 ms and 20.005 ms apart, versus the usual 5 ms. In trace
event order, the corresponding HDA handler entries followed those first two
pipeline entries by 15.515 ms and 17.699 ms; the next two were serviced
immediately. The complete capture had 719,877 pipeline entries and handler
entries; `/proc/interrupts` increased by 719,901. Only two pipeline gaps
exceeded 10 ms, and the maximum was 20.005 ms. This correlates the observed
queue buildup with a delay between IRQ pipeline processing and in-band HDA
handler service, rather than ordinary Linux run-queue delay for the audio
worker. It does not establish why the in-band service was delayed, nor prove
that it explains the historical underruns; this run itself had none.

Interpret the tracepoints as distinct stages: `irq_pipeline_entry`/`exit`
bracket the initial IRQ-pipeline pass, while `irq_handler_entry` marks the
ordinary in-band handler. That handler can be deferred until in-band
synchronization, so the pipeline-to-handler timestamp gap is not, by itself,
evidence that the physical IRQ arrived late.

The `/proc/interrupts` snapshots and trace stream do not have identical start
and end boundaries, so their 24-event count difference is not a trace-loss
measurement.

The trace and probe JSON were retained with the session evidence (SHA256
`8938213de86831da14368090bc90dce1cc81ec8c3688e471e94186b23b8201c0` and
`2395971e77795d047d09ae002d74e10cf4fd0203973fb4787d2409b46c04ad44`,
respectively). Trace events were disabled and temporary H4 files removed.
Rebooting without confirming A returned the board to confirmed B
(`/dev/nvme0n1p5`, image UUID `536ee542-ef62-4224-82ea-6471bf74e3a9`);
A is marked FAILED. The EVL router is active, the PCM is closed, and no
systemd units failed.

### EVL Context and IRQ-Off Follow-up (2026-10-23)

A second one-hour +100 ppm run used the same 48 kHz stereo, 240-frame
S16_LE/960-frame-buffer setup and an HDA/IRQ trace limited to CPU0. It completed
360,000 blocks in 3,599.630 seconds with no rejected blocks, underruns, missing
frames, or stream failures. It had one 49.704 ms ALSA wait at producer-relative
672.786 seconds; queue depth rose from 1,930 to 4,330 frames. The worker had
zero runnable delay. Its CPU0 trace had 719,819 of each IRQ 138 pipeline and
handler event, with no trace overruns or dropped events. Two pipeline gaps
over 10 ms occurred during the wait (20.063 ms and 20.469 ms); the corresponding
in-band HDA handler entries followed their pipeline entries by 16.128 ms and
17.714 ms. The capture still did not reveal why synchronization was delayed.
Its CPU0 mask excluded the EVL producer.

A corrected one-hour capture streamed HDA and EVL events across all CPUs. It
completed 360,000 blocks in 3,599.630 seconds with no rejected blocks,
underruns, missing frames, or stream failures. It had one 45.729 ms ALSA wait
at producer-relative 835.551 seconds; queue depth rose from 1,923 to 4,323
frames and runnable delay was zero. Two IRQ 138 pipeline gaps overlapped the
wait: their intervals were 19.971 ms and 20.002 ms. The corresponding in-band
HDA handler entries followed their pipeline entries by 19.607 ms and 16.059 ms;
the second handler ran about 20 us before the ALSA wait returned.
There were 719,913 pipeline entries and handler entries, and each CPU reported
zero trace overruns and dropped events. `/proc/interrupts` increased by
719,925; its snapshots have different boundaries from the trace and the
12-event difference is not a trace-loss measurement.

The same all-CPU trace placed the `corerat-unnamed` EVL producer on CPU2 for
359,995 short context runs, lasting 46-100 us (median 88 us). A separate
15-second all-CPU smoke trace had placed it on CPU3, so its CPU varied between
runs. During the delayed HDA handler intervals in the one-hour trace, the
producer continued its short periodic runs on CPU2 while IRQ 138 was handled
on CPU0. The capture therefore does not support direct same-CPU OOB occupancy
as the cause; temporal overlap across CPUs does not establish a cross-core
cause either.

An `irqsoff` smoke trace recorded a 7.586 ms maximum from `default_idle_call`
to `sync_current_irq_stage`. That span includes CPU idle and IRQ-stage
synchronization; it does not demonstrate a 7.586 ms locked critical section.
A later one-hour run with a 5 ms IRQ-off threshold emitted no maximum-latency
snapshot, and its probe had no wait above 20 ms. However, the CPU0 trace ring
reported 213,417,253 overruns while HDA events were enabled without a streaming
reader. That capture is not treated as a lossless event trace or as a
conclusive explanation/exclusion of the earlier IRQ-to-handler gaps.

The one-hour HDA/IRQ stream and probe JSON were retained with the session
evidence (SHA256 `a07773a40bc92fc8a8073eb7d1acb9e0f911be8f684d99c76f765d4f4509cb68`
and `ff45178b223ff30e80e529c0a46b455812b2e69e0a4c57f2b2bf8b890007ed31`).
The corrected all-CPU HDA/EVL trace and probe JSON have SHA256
`3cd5e71c0ba4558a82f97bd1edb73e54f56286c4dfc06d2d92dbad171d4c7580` and
`a5e9959c8463728206c129f2dc0b3ca24884c878dfc383dae2dd69d3a97bb5a3`.
After diagnostics, trace settings and filters were restored, temporary files
and the probe were removed, and the unconfirmed A slot was allowed to fail
back to confirmed B. The board is healthy on B: the EVL router is active, the
PCM is closed, and no systemd units failed.

### Filtered In-Band Re-enable Follow-up (2026-10-24)

The trace kernel was installed to unconfirmed A again. A one-hour +100 ppm
probe ran 360,000 stereo 480-frame blocks at 48 kHz for 3,599.630 seconds.
Unlike the earlier clean repeats, it reported 34 rejected blocks, 8
underruns/1,920 missing frames, and no stream failures; the probe exited with
status 1. Queue high-water reached 16,383 of 16,384 frames and the correction
hit its +500 ppm limit; the final queue held 2,621 frames and the current
correction was +55 ppm. Maximum producer lateness was 29,317 ns. The run
therefore does not qualify the physical output path and leaves the
clock-stability gate open.

An all-CPU HDA/EVL trace with CPU0 `sched_switch` events filtered to
transitions involving `kauditd` ran alongside the latter part of the probe.
A filtered kprobe on `__inband_irq_enable` recorded one useful sequence at
trace time 2,120.762288 s: `kauditd` re-enabled in-band IRQs on CPU0, and the
ordinary HDA IRQ 138 handler entered 4 microseconds later. The surrounding
re-enable burst included `rcu_do_batch`, `put_pid`, `security_task_free`,
`__put_task_struct`, and `rcu_core` caller symbols. This is consistent with a
pending in-band HDA IRQ being dispatched as the stage was re-enabled during
RCU callback processing. It does not show what held the stage stalled, prove
that RCU caused a long gap, or tie this event to an ALSA underrun. The kprobe
profile counted 41,418,659 hits, despite its event-record filter; that
high-rate instrumentation may itself have perturbed the workload.

All four per-CPU trace buffers reported zero overruns and dropped events.
However, the probe-side backend event recorder retained only 11,794 events
and reported 16,196 dropped events, with one ALSA status failure. Its retained
timestamps ended at 1,354.425 s, before the streamed trace began at
1,944.852 s. Thus the trace contains no matching per-wait diagnostics for the
later part of the probe.
The retained window included 12 waits longer than 20 ms; its longest was
47.799 ms, and one 29.764 ms wait had 20.509 ms of runnable delay. These waits
all precede the streamed trace.

The no-kprobe capture below addresses the recorder overflow by increasing its
preallocated capacity, and repeats the run without high-rate kprobe
instrumentation. It still does not explain the XRUNs or establish the
in-band-stage stall's root cause.

The trace and probe JSON were retained with the session evidence (SHA256
`ac79692519650682be375bbe3ab6ff3daec3194c8eed9641d42b462e1b8c8a36` and
`fe44d6915854be9a90e2d24f464068e3cd0cd646d193ea7351f6767ba4b853e6`,
respectively). Tracing was stopped, its kprobe removed, and named temporary
files cleaned from H4. Rebooting without confirming A returned the board to
confirmed B (root `/dev/nvme0n1p5`, image UUID
`536ee542-ef62-4224-82ea-6471bf74e3a9`). The EVL router is active, the PCM is
closed, there are no failed systemd units, and no probe process or temporary
capture files remain on H4.

### No-Kprobe Correlated H4 Capture (2026-10-24)

To separate the previous high-rate kprobe's observer effect from the recurring
waits, the same trace kernel was installed to unconfirmed A and the probe was
run again without `--wait-scheduling` or any kprobe. The probe used the
expanded preallocated diagnostic buffer (36,370 events for this one-hour run).
It completed 360,000 stereo 480-frame blocks at 48 kHz and +100 ppm in
3,599.630 seconds. The result was 0 rejected blocks, 2 underruns/480 missing
frames, and 0 stream failures. Queue high-water was 9,831 of 16,384 frames,
end depth was 3,582 frames, maximum correction reached +500 ppm, and maximum
producer lateness was 27,558 ns. The probe returned status 1 because of the
underruns. Its diagnostic recorder retained 31,908 events with no dropped
events and one ALSA status-query failure.

The simultaneous mono-clock trace streamed HDA IRQ 138 pipeline and handler
events, probe-worker wakeup events, CPU0 scheduler transitions involving the
worker or `kauditd`, and EVL context switches for the probe producer. All four
trace buffers reported zero overruns and dropped events. There were 719,797
entries and exits for both the IRQ-pipeline and ordinary IRQ-handler events.
The trace contained 33 pipeline-entry intervals longer than 10 ms (maximum
47.604 ms) and 30 ordered pipeline-to-handler intervals longer than 10 ms
(maximum 31.610 ms). Of the 27 ALSA waits longer than 20 ms, 26 overlapped at
least one such pipeline gap. This repeats the timing correlation without the
kprobe, but does not by itself prove whether physical IRQ delivery, in-band
stage synchronization, or another HDA/ALSA readiness condition caused each
gap.

The first XRUN was recorded at producer-relative 1,265.356 s as an ALSA
`-EPIPE` write with 3,034 frames queued. About 5 ms later, a CPU0 HDA pipeline
entry was followed by its in-band handler 24.406 ms later. `kauditd` ran on
CPU0 across that handler interval for 29.325 ms, switching out about 67 us
after the handler entered. This is a close temporal association, not proof
that the IRQ delay caused that XRUN. The second `-EPIPE`, at 1,556.538 s,
followed a 31.515 ms wait that itself returned `-EPIPE`; the trace did not
place a greater-than-10-ms HDA pipeline gap in that wait interval. The two
XRUNs may therefore not share the same immediate trigger.

The scheduler-switch filter included the ALSA worker only on CPU0, while its
wake events showed it being directed to CPUs 0-3. The all-CPU follow-up below
addresses that coverage gap.

The trace and probe JSON are retained with the session evidence (SHA256
`66ac89c39ed89d85d8678c7276eb9cd5223d5acd1cfe1001aebd26cc00b29eee` and
`0ee112de59ea354c1f2e8e1875be68c62819d61807e9e42641ea8db4f4b72792`,
respectively). Tracing was stopped, all event filters and clock settings were
restored, the extra tracefs mount and named temporary H4 files were removed,
and A was not confirmed. The H4 returned to confirmed B (root
`/dev/nvme0n1p5`, image UUID `536ee542-ef62-4224-82ea-6471bf74e3a9`); its EVL
router is active, PCM is closed, and no systemd units failed.

### All-CPU Worker Scheduler Follow-up (2026-10-24)

The same trace kernel was installed to unconfirmed A for a one-hour +100 ppm
probe with kprobes and `--wait-scheduling` disabled. The scheduler-switch
filter captured the ALSA worker on every CPU while retaining `kauditd`
switches only on CPU0. The EVL producer's context switches and HDA IRQ 138
pipeline/handler events were captured on the same mono clock. The probe
completed 360,000 stereo 480-frame blocks in 3,599.630 seconds with 0 rejected
blocks, underruns, missing frames, stream failures, dropped diagnostic events,
or status/scheduling failures. Queue high-water was 6,350 of 16,384 frames,
end depth was 2,828 frames, correction peaked at +500 ppm, and maximum
producer lateness was 29,192 ns. It exited successfully.

The trace began 21.281 seconds after producer start and continued to the end
of the run. It contained 715,589 entries/exits for both the IRQ-pipeline and
ordinary HDA-handler events, with no overruns or drops on any CPU. There were
21 pipeline-entry intervals longer than 10 ms (maximum 30.446 ms) and 18
pipeline-to-handler intervals longer than 10 ms (maximum 29.975 ms). All 16
ALSA waits longer than 20 ms overlapped at least one long pipeline interval.
This again correlates slow ALSA waits with delayed HDA pipeline/handler
service; it does not identify the physical IRQ arrival time or prove why
in-band handling was deferred.

Unlike the previous CPU0-only scheduler trace, this run captured 360,367
worker wakeups and subsequent switches back to the worker on CPUs 0-3. The
wakeup-to-run median was 2-11 us and the per-CPU 99th percentile was 3-20 us.
The largest observed delays were 624 us on CPU0, 62 us on CPU1, 43 us on
CPU2, and 1.570 ms on CPU3. Thus ordinary Linux wake-to-run scheduling did
not account for the 25-56 ms ALSA waits in this run.

The scheduler trace also found that all 18 long pipeline-to-handler intervals
overlapped `kauditd` execution on CPU0; 23 of its 24 observed CPU0 execution
slices exceeded 10 ms. This strengthens the temporal association previously
seen in the filtered `__inband_irq_enable` kprobe capture, but it is still not
causal evidence by itself. The successful one-hour probe had no underruns,
while the preceding no-kprobe run had two; the intermittent XRUN remained
unexplained. The filtered preemptirq follow-up below was selected to capture
in-band IRQ-state transitions alongside the probe and HDA events.

The trace and probe JSON are retained with the session evidence (SHA256
`19c0f5ad4f97661a1c4eddc42b3591e78954657fdb053f0bf13328ec577c88a0` and
`9b706f884522142f58f3b9d87fad6d287fd0ab3916b72be2d1a438c252efcf4e`,
respectively). Tracing was stopped, event filters and settings were restored,
task-specific files were removed from H4, and A was not confirmed. The H4
returned to confirmed B (root `/dev/nvme0n1p5`, image UUID
`536ee542-ef62-4224-82ea-6471bf74e3a9`); the EVL router is active, PCM is
closed, and no systemd units failed.

### Filtered Preemptirq and Console Correlation

A follow-up one-hour +100 ppm run used a 36,370-event preallocated probe
recorder and streamed CPU0 HDA IRQ 138 pipeline/handler events plus filtered
`preemptirq:irq_disable` and `irq_enable` events. The trace reader was pinned
to CPU1. It read 2,881,086 events with zero trace overruns or drops; the probe
retained 25,633 events with zero recorder drops. The probe completed 360,000
stereo 480-frame blocks at 48 kHz in 3,599.630 seconds, with 0 rejected
blocks, 2 underruns/480 missing frames, 0 stream failures, and 2 ALSA status
failures. The maximum producer lateness was 22.317 us, and queue high-water
was 9,609 frames. The run therefore does not qualify the output path.

The filtered trace contained 50 paired IRQ-off spans attributed to `kauditd`
in `console_flush_all`; each pair had matching `console_flush_all` callsites.
The two probe waits that returned `-EPIPE` both aligned with these spans and
HDA handler deferral:

- At producer-relative 463.896746 s, the ALSA wait lasted 22.364 ms. A
  `kauditd` span began at 463.896546 s and lasted 22.519 ms. HDA IRQ 138
  entered the pipeline at 463.900539 s; its in-band handler followed 18.532 ms
  later, about 6 us after the IRQ-off span ended.
- At producer-relative 2015.716406 s, the ALSA wait lasted 41.255 ms. A
  `kauditd` span began at 2015.725581 s and lasted 32.038 ms. HDA IRQ 138
  entered the pipeline at 2015.725717 s; its in-band handler followed
  31.909 ms later, about 7 us after the span ended.

All 19 ALSA waits longer than 20 ms overlapped at least one paired
`kauditd` console-flush span and a long HDA pipeline/handler interval. Pairing
was restricted to spans whose disable and enable events were both observed
under `kauditd` with matching callsites. Because the event filter omitted
transitions attributed to other tasks, events were not paired across task
switches.

Source review gives this correlation a plausible mechanism. The Xenomai IRQ
pipeline reports in-band stage stall transitions through the hard-IRQ tracing
hooks; this does not establish that the physical interrupt controller masked
the HDA line. In the captured legacy printk path,
`console_emit_next_record()` saves IRQ state across the console driver's
`write()` call. The 8250 serial-console writer holds an IRQ-saving UART lock
while sending and waiting for the transmitter to empty. The trial's active
consoles were `ttyS0 tty0`, with `ttyS0` configured at 115200 baud and console
log level 7. The audit source also shows `kauditd` can fall back to `pr_notice`
when audit records cannot be delivered to userspace; no userspace `auditd`
process was running during this trial. This is a strong candidate explanation
for the long in-band stalls and both observed XRUNs, but the trace did not
capture the exact printk call or test an intervention. It does not establish
that the operator's audible oscillator clicks have the same cause.

After collection, trace settings were restored and A was not confirmed. The
H4 was rebooted to confirmed B (`/dev/nvme0n1p5`); the EVL router is active,
all ALSA PCM substreams are closed, no probe process remains, and no systemd
units failed. The trace and probe JSON are retained in session storage under
`h4-hda-trace-plus100-2026-10-23`.

### Console-Threshold Causality Trial (Interrupted 2026-10-25)

To test whether NOTICE/WARNING console output was involved, the verified
trace-kernel image was installed to unconfirmed A (revision 4); confirmed B
(revision 3) was left untouched. A 300-block +100 ppm probe smoke test passed
with no underruns or dropped diagnostic events. The one-hour +100 ppm capture
then started with printk console level reduced from 7 to 3. Kernel log-buffer
and audit settings were unchanged. CPU0 HDA IRQ 138 and filtered
`kauditd` preemptirq events were streamed to the host, with the trace reader
pinned to CPU1.

This comparison is **incomplete**. The attached SSH/capture session was
interrupted before the one-hour probe finished. The retained gzip trace is
truncated and fails integrity checking; it contains only a partial timeline.
The probe JSON was written under `/run` on A and was lost when the unconfirmed
trial slot failed back to B. No final underrun, queue, or trace-loss totals
survived, so this attempt cannot be compared with the normal-console run and
does not test or refute the console-output hypothesis.

After fallback, confirmed B was verified booted with the default console level
7, EVL router active, PCM closed, and no failed systemd units. The interactive
gain/pan demo was subsequently started on B; its service is active and its
PCM reports `RUNNING`. The causal comparison and audible listening check are
deferred; no further H4 changes are part of the current DSP/module work.

## Listening Check (2026-10-19)

The operator approved three 30-second probe segments: nominal cadence, +100 ppm,
and -100 ppm. Each produced 3,000 blocks at 48 kHz stereo, 480 frames per
block. All three reported zero rejected blocks, underruns, missing frames, or
stream failures. The +100 ppm segment was heard as clean and steady. The
operator could not listen to the nominal segment. The -100 ppm segment was
heard as steady except for one click shortly after tone onset.

To distinguish a startup transient from a repeatable in-stream click, the
probe was updated with 20 ms linear fades at the beginning and end and the
-100 ppm segment was repeated. That run reported zero playback errors,
1,920-frame queue high-water, 1,374 end frames, and maximum absolute
correction 337 ppm. The operator reported the faded repeat sounded clean and
steady. This is encouraging subjective evidence for the faded test tone, but
does not prove the original click was a one-off or qualify every startup path
or the unmodified interactive demo. The gain/pan demo was restarted after the
test and its PCM is `RUNNING`.

The normal interactive demo was then cold-restarted three times for listening.
The operator reported no click on starts one and two, and one onset click on
start three. Backend-level fades were considered but not retained: an
oscillator amplitude envelope is the more appropriate place to smooth an
intentional signal start, while an output sink should not conceal source
discontinuities.

During subsequent operation, the operator also reported audible artifacts when
moving either the gain or pan slider quickly. The live demo config used
64-sample ramps (about 1.3 ms at 48 kHz). Both demo config sources now use
480-sample ramps (10 ms at 48 kHz); this setting was tested from a temporary
runtime config and has not been written to the installed H4 image.

On 2026-10-19, the H4 demo was restarted with a temporary copy of its installed
config, changing only the gain and pan smoothing values to 480 samples. In two
quick listening checks, the operator moved both controls and reported them
clean or clearly improved. The demo remains active on confirmed slot B with
ALSA PCM `RUNNING`; the temporary runtime config is under `/tmp`, and no image
or installed config was changed. The operator considers gain and pan movement
solved for now; this is subjective evidence and not a guarantee for every
possible control motion.

The gain/pan screen was then extended with peak, RMS, and clipping widgets,
using the existing level-meter and widget-update modules. The first temporary
launch used the installed, older LVGL manifest, so the new widgets were absent.
After staging the updated manifest under `/tmp` and restarting the graph, the
operator confirmed the peak and RMS bars were visible and moving with the audio,
and the clipping indicator was visible. The meter-enabled graph remains active
on confirmed slot B with ALSA PCM `RUNNING`; its config and manifest are
temporary files, and the installed image and config remain unchanged.

The authoritative panner-state feedback path was then exercised in EVL QEMU
and on H4. Three assertion-enabled DSP tests passed under QEMU; a routed
oscillator/parameter-source/panner graph produced nonzero 48 kHz stereo PCM
with a right-to-left RMS ratio of 3.08 at pan `0.6`. On H4, a 10-second
temporary graph started and stopped the updated panner, parameter-state
adapter, widget-update merger, LVGL sink, and ALSA sink. The LVGL sink also
logged 1000 ms data-loop overruns against its 10 ms period, so this is runtime
startup/routing evidence rather than a measurement of interactive display
latency or a touch-driven state change. Temporary files were removed and the
existing meter demo was restored on slot B with ALSA PCM `RUNNING`; no installed
image or config was changed.

## Qualification Still Required

- Exercise physical watchdog-triggered fallback and recovery; no backup was
  made, so establish an attended recovery plan first.
- Investigate transient backend/device progress around queue excursions:
  controlled repeats confirmed matching PCM geometry and reproduced correction
  saturation without underruns, but the original underruns remain unexplained.
  Keep the clock-stability gate open.
- Evolve the simple oscillator into a synth voice with note input and an
  integrated ADSR envelope, then repeat the cold-start listening check; the
  intermittent demo onset click remains unresolved.
- Measure physical end-to-end latency and jitter with documented methods.
- Test disconnect/reconnect and orderly shutdown/restart.

## Deterministic Clock-Skew Model

The `musicrat.backends.audio_clock_drift` regression uses the actual
playback-preroll helper with a deterministic 48 kHz producer/consumer model,
480-frame producer blocks, 240-frame consumer periods, 960-frame device buffer,
and the configured bounded ring capacity. It verifies frame accounting,
capacity limits, underflow/rebuffer, and recovery after the producer returns to
nominal rate. The native test passes; the EVL cross-built test was also run on
the H4 while the gain/pan demo remained active. This is model evidence, not a
physical clock measurement.

At +100 ppm over simulated one-hour playback, the ring reached 16,320 of 16,384
frames and rejected 7 producer blocks; there were no underruns. At -100 ppm over
10 minutes, the ring rebuffered after one underrun (240 missing frames), with
1,680 frames of prefill silence. After the producer returned to nominal rate
for two simulated minutes, playback was running at 480 queued frames without
another underrun. This demonstrates the FIFO/preroll path eventually loses
blocks or stalls under sustained independent clocks and motivates the bounded
correction policy below.

## Bounded Adaptive Output Resampling

The selected output-sink policy is bounded linear interpolation, controlled by
ring-depth error without blocking the module graph. A correction of +1 ppm per
four frames of positive queue error increases source frames consumed per output
frame; negative error reduces it. The applied ratio is clamped to +/-500 ppm
(at most 0.05% rate change). ALSA and PipeWire retain a fractional read cursor,
interpolate between adjacent queued frames, and publish current and maximum
absolute corrections in probe JSON. The NONE backend reports zero correction.

The deterministic model exercises this controller with the same queue geometry
as the baseline comparison. At +100 ppm for one simulated hour, high-water was
2,573 of 16,384 frames, the final correction was +103 ppm (163 ppm maximum
transient), and there were no rejects or underruns. At -100 ppm for 10 minutes,
high-water was 2,040 frames, the correction was -103 ppm before returning the
producer to nominal rate; after a further 10 simulated minutes, it was running
without rejects or underruns, at 1,560 queued frames with a final correction of
-30 ppm. This model validates bounded response and recovery behavior only; it
does not establish the best correction gain, audible quality, real-device
stability, or the physical clock offset.

The EVL-built adaptive ALSA probe was then run on H4 after the operator
approved pausing the demo. It requested `plughw:0,0`, 48 kHz stereo, and
480-frame producer blocks for 3,000 blocks (29.990 s), without injected jitter
or external load. It reported zero rejected blocks, underruns, missing frames,
or stream failures; maximum producer lateness was 7,328 ns. The queue
high-water was 1,920 of 16,384 frames, end-of-producer occupancy was 1,576
frames, current correction was -206 ppm, and maximum absolute correction was
313 ppm. The probe and runtime libraries were hash-verified; the probe was
removed and the gain/pan demo was restarted on confirmed slot B with PCM
`RUNNING`. This is an initial physical integration check, not a measurement of
the true hardware clock offset, an injected-skew test, or an audio-quality
qualification.

The probe accepts `--clock-offset-ppm` from -1000 to +1000 to alter only its
absolute producer deadline cadence while leaving the audio device clock
unchanged. Physical positive/negative offset results have not yet been
collected.

The bounded probe described in [EVL_AND_RATOS.md](EVL_AND_RATOS.md) reports
backend counters, queue occupancy, rate-correction measurements, and producer
wake-up lateness. It does not measure hardware latency, physical clock offset,
or OOB residency. Its exit status is nonzero for rejected blocks, underruns,
stream failures, invalid configuration, impossible queue metrics, or failed
EVL producer attachment.

## Live Three-Oscillator H4 Startup (2026-10-27)

Started the keyboard and 23-control ThreeOscillator project as the transient
`musicrat-three-oscillator-live.service`, using development binaries from the
dirty MusicRaT `c42db4f` worktree cross-built against the RaTOS SDK. Binaries
are staged under `/tmp/musicrat-three-oscillator-live`; the configuration and
manifest are under `/var/tmp/musicrat-three-oscillator-live`. Installed binaries,
the image, and the existing EVL router service were not changed.

The display uses the 1024x600 framebuffer and the USB touchscreen event device.
The SDK lacks DRM development headers, so this run uses framebuffer rather than
DRM. Service hooks switch the console to graphics mode and restore text mode
on stop. A framebuffer capture verified the keyboard and controls were rendered.
The LVGL C-library allocator fix is included.

ALSA uses `plughw:0,0`; the actual PCM reports stereo S16_LE at 48 kHz,
240-frame periods and a 960-frame buffer. All five modules remained running at
the post-start check and PCM state was `RUNNING`. This is a startup/display
smoke check only: touch-to-note behavior, audible output, sustained operation,
and dropout counters were not qualified in this run. The target clock reported
April 13, so its journal timestamps do not match this host-side test date.
The transient service is not enabled for reboot.

### Multitouch Follow-Up

The operator reported that only one piano key could be held. The controller's
HID descriptor contains multiple finger records, but the running image bound
USB `1a86:e5e3` to `hid-generic`, exposing only single-pointer axes. The installed
kernel had no available `hid_multitouch` module. RaTOS now requests
`CONFIG_HID_MULTITOUCH=m` in the H4 kernel fragment for subsequent image builds.

With explicit approval, a temporary `hid-multitouch.ko` was loaded without a
reboot or installed-image changes. The retained kernel build ID matched the
running kernel; the module was built from exact source
`45dc6e6c8b183e31bb0862bdab37253b8fa0e54d` using the original headers/config,
with all 44 imported symbol CRCs verified. Module SHA256:
`a09d6fcd3e1897e809c90bc7eca183c9b9ecd8c04b9e89bd0ab0b67d6134a47c`.
Loading it automatically rebound the controller to `hid-multitouch`; an
additional explicit generic-driver unbind was unnecessary and returned
`No such device`. The kernel reports an out-of-tree-module taint for this
development build.

The updated LVGL backend detected six independent touch contacts after the
demo was restarted. All five modules remained active and ALSA PCM was
`RUNNING`. Four focused native tests and the same four SDK-built tests under
EVL QEMU passed, including three simultaneous keys, independent releases,
two fingers holding one key, tracking-ID reuse, coordinate scaling, event-loss
cancellation, and cleanup. The disposable QEMU guest was terminated after
its poweroff stalled; the tests had already completed successfully.

Actual multi-finger playing and audible chord output remain operator checks,
not inferred from the capability query or synthetic tests. The loaded driver
and staged UI are temporary; a reboot returns to the installed image until a
new image includes the driver and updated application.
