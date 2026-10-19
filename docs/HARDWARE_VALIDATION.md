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
480-sample ramps (10 ms at 48 kHz) for the next listening check; this setting
has not been deployed to the H4.

On 2026-10-19, the H4 demo was restarted with a temporary copy of its installed
config, changing only the gain and pan smoothing values to 480 samples. The
operator quickly moved both controls and reported both sounded clean or clearly
improved. The demo remains active on confirmed slot B with ALSA PCM `RUNNING`;
the temporary runtime config is under `/tmp`, and no image or installed config
was changed. This is a single subjective listening check, not a claim that all
possible control-motion artifacts are eliminated.

## Qualification Still Required

- Exercise physical watchdog-triggered fallback and recovery; no backup was
  made, so establish an attended recovery plan first.
- Verify queue occupancy over multi-hour operation and more extreme clock
  offsets.
- Evolve the simple oscillator into a synth voice with note input and an
  integrated ADSR envelope, then repeat the cold-start listening check; the
  intermittent demo onset click remains unresolved.
- Listen for rapid gain/pan slider artifacts with the 10 ms demo ramps on the
  H4 under longer use before deciding whether further smoothing changes are
  needed.
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
