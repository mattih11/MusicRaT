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

The null sink consumes audio blocks without producing output. It counts received and invalid blocks with relaxed atomics and provides a bounded headless graph terminus for launcher tests. Publishing those counters through a telemetry output remains future work.

## PCM16 WAV Sink

`MusicRaTWavSink` consumes `AudioBlock` and writes a configured fixed-rate, fixed-channel PCM16 WAV stream. It opens and writes the initial header during `on_enable()`, packs each complete interleaved block into fixed storage, and submits it through `corerat::File`. It rejects malformed blocks and runtime format changes without throwing from `process()`.

During `on_disable()`, the sink patches RIFF/data sizes, syncs, and closes the file outside the real-time path. On EVL, CoreRaT routes writes through its file proxy. The STD backend is direct POSIX I/O and is therefore intended for deterministic offline rendering and tests rather than a hard-real-time recording claim.

The sink is configured with a bounded path, sample rate, and channel count. As an outputless module it also requires `module_address` in application JSON for a unique lifecycle and WORK-mailbox identity.

The companion `WavReader` provides non-real-time PCM16 RIFF parsing for the future file-player worker. It validates format and chunk bounds, ignores correctly padded unknown chunks, decodes into caller-owned bounded blocks, and marks the first block after a frame seek as discontinuous. Decode-ahead and the launchable player remain separate work.

The CommRaT-independent `VarispeedRenderer` consumes prepared decoded chunk views. It tracks absolute media-frame position and generation, performs forward linear interpolation with sample-rate conversion and smoothed rate changes, and always emits the requested bounded graph quantum. Missing or stale chunks produce flagged silence without advancing media position; no decoder or file API is called from the renderer.

## Level Meter Telemetry

`LevelMeterBlock` is a bounded type-specific telemetry snapshot. Its fixed arrays carry per-channel sample peak, RMS, and clipping state; metadata carries channel count, timestamp, sequence number, and validity flags.

`MusicRaTLevelMeter` validates and passes through its audio input while publishing one meter snapshot per block. Descriptor output 0 is `AudioBlock`; output 1 is `LevelMeterBlock`. Non-finite samples produce invalid empty audio and telemetry outputs. True-peak measurement and configurable publication decimation remain future work.

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