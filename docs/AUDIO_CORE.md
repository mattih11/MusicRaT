# Audio Core

This document records the initial MusicRaT audio contract and the decisions behind it.

See [Signals, Ports, and Graph Architecture](SIGNALS_AND_PORTS.md) for the normative graph, control, timing, launcher, and RatGUI contracts.
See [Project Layout and File Conventions](PROJECT_LAYOUT.md) for code ownership and public include paths.
See [Media Playback, Recording, and Device I/O](MEDIA_PLAYBACK.md) for codec, file-sink, and live-device real-time boundaries.

## Application-Wide Build Policy

Application-wide settings are CMake cache variables. CMake validates them and generates `musicrat/config.hpp`; `include/musicrat/musicrat.hpp` includes that generated header so every module in one build uses exactly the same policy.

| CMake variable | Default | Generated C++ name | Purpose |
| --- | --- | --- |
| `MUSICRAT_SAMPLE_TYPE` | `float` | `musicrat::config::sample_type` | Audio sample representation; supported values are `float` and `double` |
| `MUSICRAT_MAX_AUDIO_CHANNELS` | `8` | `musicrat::config::max_audio_channels` | Maximum channels in one block |
| `MUSICRAT_MAX_AUDIO_FRAMES` | `4096` | `musicrat::config::max_audio_frames` | Maximum frames per channel and block |
| `MUSICRAT_MAX_PARAMETER_EVENTS` | `256` | `musicrat::config::max_parameter_events` | Maximum timestamped parameter events in one block |
| `MUSICRAT_FILE_PROXY_BYTES` | `262144` | `musicrat::config::file_proxy_bytes` | CoreRaT file-proxy capacity; must hold one maximum interleaved PCM16 block |
| `MUSICRAT_DEFAULT_SAMPLE_RATE_HZ` | `48000.0` | `musicrat::config::default_sample_rate_hz` | Default module sample rate |
| `MUSICRAT_DEFAULT_BLOCK_PERIOD_MS` | `10` | `musicrat::config::default_block_period_ms` | Default timer-driven processing period |

Storage remains planar: each channel is contiguous for channel-oriented DSP and SIMD.

These values are compile-time storage limits, not the active stream format. The active channel count, frame count, and sample rate travel in every `AudioBlock`.

Changing the sample type or either capacity changes the `AudioBlock` C++ type layout and serialized capacity. All module binaries in one application must therefore come from the same configured build. Do not mix descriptors or binaries built with different policies.

Configure a non-default policy with cache arguments:

```bash
cmake -S . -B build-custom \
  -DBUILD_MUSICRAT=ON \
  -DBUILD_TESTING=ON \
  -DMUSICRAT_SAMPLE_TYPE=double \
  -DMUSICRAT_MAX_AUDIO_CHANNELS=2 \
  -DMUSICRAT_MAX_AUDIO_FRAMES=512 \
  -DMUSICRAT_DEFAULT_SAMPLE_RATE_HZ=44100.0 \
  -DMUSICRAT_DEFAULT_BLOCK_PERIOD_MS=5
```

CMake rejects unsupported sample types, non-positive values, and channel/frame capacities that cannot fit their wire metadata fields. The configure summary prints the effective policy.

## AudioBlock Contract

`CommRaT::Messages::AudioBlock` contains:

- A configured number of bounded planar channel vectors
- `sample_rate_hz`, describing the active stream rate
- `timestamp_ns`, identifying the first frame on the monotonic audio timeline
- `sequence_number`, increasing once per produced block
- `frame_count`, the valid frame count in every active channel
- `channel_count`, the number of valid channels
- `flags`, reserved for discontinuity, silence, underrun, and end-of-stream semantics

Only channels in `[0, channel_count)` are active. Every active channel must contain exactly `frame_count` samples. Inactive channels must be empty. Producers must validate capacity before entering their sample loop because `fixed_vector::push_back()` throws on overflow.

`musicrat::validate_audio_block()` is the shared deterministic validator. It distinguishes invalid sample rate, channel/frame capacity overflow, active-channel size mismatch, and non-empty inactive channels without allocating or throwing. Processors use the same helper before entering sample loops.

The first implementation deliberately stores metadata in the payload even though TiMS also timestamps messages. Payload metadata survives recording, offline processing, and graph hops where the transport header may change.

The currently defined flags are `AUDIO_BLOCK_SILENCE`, `AUDIO_BLOCK_END_OF_STREAM`, `AUDIO_BLOCK_UNDERRUN`, `AUDIO_BLOCK_DISCONTINUITY`, and `AUDIO_BLOCK_INVALID`. Processors preserve incoming flags unless they deliberately change the represented stream state. A processor that rejects malformed metadata emits an empty block with `AUDIO_BLOCK_INVALID` set.

## Parameter Events

`ParameterEventBlock` is a bounded control stream containing events with a source endpoint ID, stable numeric parameter ID, in-block sample offset, and numeric value. The block also carries a monotonic timestamp and sequence number.

Events must be ordered by nondecreasing sample offset, and every offset must be less than the target audio block's frame count. The current gain module ignores the entire event block when these invariants are violated, ignores unknown parameter IDs and non-finite values, and consumes fresh synchronized blocks only. Producers must prove `max_parameter_events` capacity before appending because bounded insertion may throw on overflow.

The initial control source emits one event at sample offset zero per configured period. It is a deterministic graph/test source, not the final external-control adapter.

## Gain and Null Sink

The gain module is driven by its primary `AudioBlock` input and reads `ParameterEventBlock` as a synchronized secondary input. Its transport-independent `GainProcessor` owns block validation, metadata propagation, event ordering checks, and sample processing. The scalar `Gain` kernel remains independent from message containers.

Gain, mute, and polarity inversion share a configurable linear ramp measured in samples. One ramp state advances per frame and is shared by every active channel, so mono and multichannel streams have identical smoothing time. Invalid input produces an empty block with the original metadata and flags plus `AUDIO_BLOCK_INVALID`.

## Stereo Panner

`MusicRaTStereoPanner` converts one mono `AudioBlock` input into a two-channel output. It supports linear and equal-power pan laws over the normalized range `[-1, 1]`. Pan changes from persistent parameters or synchronized `ParameterEventBlock` events ramp the left and right gains independently without allocation or blocking work in the sample loop.

The panner is deliberately a source-position processor, not a stereo balance control. Stereo balance and width belong to the planned channel-strip utility. Invalid blocks and non-mono inputs produce an empty block marked `AUDIO_BLOCK_INVALID`.

Its descriptor declares a fixed one-channel input and two-channel output while preserving sample rate and clock domain. Format preflight therefore validates downstream stereo sinks and still propagates sample-rate mismatches across the channel transform.

## Channel Mapper

`MusicRaTChannelMapper` applies a precompiled, fixed-capacity matrix to each
audio block. Presets cover identity routing, mono-to-stereo duplication,
stereo-to-mono averaging, stereo swap, and copying either stereo channel to
both outputs. Custom row-major matrices support any configured input and output
counts within the application channel capacity. Configuration is validated
before processing; malformed matrices and runtime channel mismatches emit an
invalid empty block. The descriptor resolves input and output channel counts
from module parameters so launcher preflight validates format-changing routes.

The null sink consumes audio blocks without producing output. It counts received and invalid blocks with relaxed atomics and provides a bounded headless graph terminus for launcher tests. Publishing those counters through a telemetry output remains future work.

## PCM16 WAV Sink

`MusicRaTWavSink` consumes `AudioBlock` and writes a configured fixed-rate, fixed-channel PCM16 WAV stream. It opens and writes the initial header during `on_enable()`, packs each complete interleaved block into fixed storage, and submits it through `corerat::File`. It rejects malformed blocks and runtime format changes without throwing from `process()`.

During `on_disable()`, the sink patches RIFF/data sizes, syncs, and closes the file outside the real-time path. On EVL, CoreRaT routes writes through its file proxy. The STD backend is direct POSIX I/O and is therefore intended for deterministic offline rendering and tests rather than a hard-real-time recording claim.

The sink is configured with a bounded path, sample rate, and channel count. As an outputless module it also requires `module_address` in application JSON for a unique lifecycle and WORK-mailbox identity.

The companion `WavReader` provides non-real-time PCM16 RIFF parsing for the future file-player worker. It validates format and chunk bounds, ignores correctly padded unknown chunks, decodes into caller-owned bounded blocks, and marks the first block after a frame seek as discontinuous. Decode-ahead and the launchable player remain separate work.

## Audio Device Sink

`MusicRaTAudioDeviceSink` is the backend-neutral live playback module. Select
one implementation for the whole build with `MUSICRAT_AUDIO_BACKEND`: `AUTO`
prefers PipeWire and then ALSA when their development packages are available,
`PIPEWIRE` and `ALSA` require their respective backend explicitly, and `NONE`
keeps descriptors and tests available without a live device. `EVL_TINYALSA`
remains reserved for a strict OOB backend and fails configuration because the
current 6.12 kernel has no EVL-enabled ALSA PCM path.

The PipeWire implementation requests interleaved float audio at the configured
sample rate and channel count. A preallocated bounded SPSC queue separates the
CommRaT producer from PipeWire's hardware-clocked callback. Queue overflow
rejects the complete incoming block; callback starvation emits silence and
counts both underrun callbacks and missing frames. Stream errors and disconnects
immediately stop queue acceptance and increment a separate stream-failure
counter. Channel order follows standard speaker layouts: mono; FL/FR; FL/FR/FC;
quad FL/FR/RL/RR; 5.0 FL/FR/FC/RL/RR; 5.1 FL/FR/FC/LFE/RL/RR; 6.1
FL/FR/FC/LFE/RC/SL/SR; and 7.1 FL/FR/FC/LFE/RL/RR/SL/SR. PipeWire
configurations above eight channels are rejected until an explicit layout
contract exists. Device discovery,
published counter telemetry, and hot-plug recovery remain pending.

The ALSA implementation opens the configured PCM directly, defaulting to
`default`, and requests interleaved PCM16. Its preallocated bounded SPSC queue
keeps all ALSA calls on a dedicated worker thread; the CommRaT producer only
validates, converts, copies, and publishes complete blocks. Queue overflow
rejects the complete incoming block. ALSA underruns and unrecoverable stream
failures are exposed through the same counters as the PipeWire backend.

Run the host example after building with PipeWire support:

```bash
./build-validation/musicrat_launcher \
  ./examples/configs/tone_to_device.json \
  --duration-ms 3000
```

When LVGL is built with SDL support, the generated
`tone_to_device_lvgl_sdl.json` adds an on-screen slider mapped through
`MusicRaTControlMapper` to the gain parameter:

```bash
./build-lvgl/musicrat_launcher \
  ./build-lvgl/tone_to_device_lvgl_sdl.json \
  --duration-ms 300000
```

The generated `interactive_gain_pan_sdl.json` exercises a small stereo channel
strip: an LVGL fader controls gain, an LVGL knob controls mono-to-stereo pan,
and the stereo result plays through the selected audio-device backend:

```bash
./build-lvgl-sdl-validation/musicrat_launcher \
  ./build-lvgl-sdl-validation/interactive_gain_pan_sdl.json \
  --duration-ms 300000
```

The CommRaT-independent `VarispeedRenderer` consumes prepared decoded chunk views. It tracks absolute media-frame position and generation, performs forward linear interpolation with sample-rate conversion and smoothed rate changes, and always emits the requested bounded graph quantum. Missing or stale chunks produce flagged silence without advancing media position; no decoder or file API is called from the renderer.

## Level Meter Telemetry

`LevelMeterBlock` is a bounded type-specific telemetry snapshot. Its fixed arrays carry per-channel sample peak, RMS, and clipping state; metadata carries channel count, timestamp, sequence number, and validity flags.

`MusicRaTLevelMeter` validates and passes through its audio input while publishing one meter snapshot per block. Descriptor output 0 is `AudioBlock`; output 1 is `LevelMeterBlock`. Non-finite samples produce invalid empty audio and telemetry outputs. True-peak measurement and configurable publication decimation remain future work.

`MusicRaTLevelMeterUiAdapter` converts descriptor-selected peak, RMS, and clip
values into bounded `WidgetUpdateBlock` events carrying stable binding,
surface, widget, and property IDs. `examples/configs/level_meter_to_ui.json`
exercises the typed launcher route. The block is a renderer-neutral runtime
boundary; RatGUI and LVGL remain responsible for subscription and drawing.

The optional `musicrat_lvgl_widgets` backend creates meter, toggle, text,
slider, knob, and button widgets from the Designer's LVGL manifest. It applies
normalized value, active, and text updates on the caller's LVGL thread. Enable it by setting
`MUSICRAT_LVGL_SOURCE_DIR` to an LVGL source tree. Its bounded SPSC queue keeps
message callbacks separate from LVGL calls. The optional
`MusicRaTLvglWidgetSink` publishes `ControlEventBlock`, optionally subscribes
to synchronized `WidgetUpdateBlock`, loads a Designer manifest, and drains
updates on its lifecycle-owned LVGL thread. Slider, knob, toggle, and button
callbacks enqueue bounded semantic events for its periodic CommRaT callback;
programmatic widget updates are suppressed from that outbound path. The
`level_meter_to_lvgl` launcher test proves the complete typed route with a
headless display. Strict Designer export creates one sink per observed display
and chains bounded `MusicRaTWidgetUpdateMerger` modules when several typed
adapter streams feed that display. On desktop Linux, the SDL2 backend opens a
resizable preview window; its input devices are scoped to configured on-screen
widgets. The sink can instead own a DRM/KMS display
directly or use legacy fbdev on console-only systems; those modes require no
X11, Wayland, or desktop session.

`interactive_dual_gain.project.json` is the editable Designer document.
Its generated `interactive_dual_gain.json` runs a sine oscillator through two gain modules,
maps an on-screen slider and knob to their gain parameters, and returns the
final level meter to the same display. CMake generates a headless validation
config and, when SDL is enabled, `interactive_dual_gain_sdl.json` for desktop
interaction.

Enable direct-display support when configuring:

```bash
cmake -S . -B build-lvgl \
  -DBUILD_MUSICRAT=ON \
  -DMUSICRAT_LVGL_SOURCE_DIR=/path/to/lvgl \
  -DMUSICRAT_LVGL_ENABLE_SDL=ON \
  -DMUSICRAT_LVGL_ENABLE_DRM=ON \
  -DMUSICRAT_LVGL_ENABLE_FBDEV=ON
cmake --build build-lvgl --target musicrat_launcher musicrat_lvgl_widget_sink -j2
```

SDL requires the SDL2 development package and uses backend value `3`. CMake
generates `level_meter_to_sdl.json`, which can be launched on a desktop with:

```bash
corerat-router-tcp &
./build-lvgl/musicrat_launcher \
  ./build-lvgl/level_meter_to_sdl.json \
  --duration-ms 300000
```

DRM requires the `libdrm` development package at build time. Other backend
values are `0` for headless, `1` for DRM/KMS, and `2` for framebuffer. An empty
DRM `device_path` auto-selects a card and `connector_id: -1` selects the first
connected output. The fbdev default is `/dev/fb0`. CMake generates runnable
`level_meter_to_drm.json` and `level_meter_to_fbdev.json` examples in the build
directory. The service user must have access to the selected device, normally
through the `video` group; DRM also requires that another compositor or display
server is not holding the connector as DRM master.

## Oscillator Module

The oscillator uses the current CommRaT architecture:

- `MusicRaT::Module2` provides execution and lifecycle handling.
- `Output<AudioBlock>` publishes planar audio.
- `Period<Milliseconds(default_block_period_ms)>` declares the configured default processing cadence.
- `Params<Oscillator>` exposes frequency, amplitude, sample rate, phase offset, and enabled state through CommRaT parameter messages and module descriptors.
- `ResetPhase` is associated with the `AudioBlock` output through `DataWithCommands` because reset is an imperative action rather than persistent configuration.

Frame count is computed as:

$$
N = \operatorname{round}\left(f_s \frac{T_{ms}}{1000}\right)
$$

Construction fails when the period is non-positive, the sample rate is invalid, or the resulting frame count exceeds the compile-time block capacity. The processing loop clears all channel sizes, emits mono data in channel zero, and performs no allocation.

## Launcher Integration

`musicrat_oscillator` is built with `commrat_module()`. Its generated descriptor includes the audio output type, timer execution mode, default period, reset command endpoint, lifecycle endpoint, and oscillator parameter defaults.

`musicrat_launcher` is a thin `ProcessLauncher` executable. The initial configuration is `examples/configs/sine_oscillator.json`. Run it from the build directory so the launcher can discover `MusicRaTSineOscillator.module.json` beside the oscillator binary:

```bash
./build-validation/musicrat_launcher \
  ./examples/configs/sine_oscillator.json \
  --duration-ms 500
```

The source-only `sine_oscillator.json` remains a minimal descriptor/lifecycle example. `controlled_gain.json` runs `parameter source -> gain control` alongside `oscillator -> gain -> level meter -> null sink`, with meter snapshots routed to a telemetry sink. A bounded CTest launcher smoke test covers the complete graph.

## Validation

`musicrat_audio_block_test` serializes and deserializes an audio block and verifies format metadata, samples, and structural validation. `musicrat_gain_processor_test` covers stereo ramp timing, metadata and flag propagation, invalid-block handling, mute, and polarity inversion without starting CommRaT threads. The panner tests cover pan-law endpoints and center gains, smoothed parameter events, mono-to-stereo conversion, invalid input, format propagation, descriptor generation, and routed stereo WAV output. These tests intentionally use the generated policy so CTest can validate custom configurations.

Remaining contract work includes stream-format negotiation, richer parameter descriptors and value types, explicit late/duplicate event policy, endpoint identity, telemetry, and transport-size measurement under maximum block occupancy.