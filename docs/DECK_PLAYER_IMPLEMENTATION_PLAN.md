# LVGL Performance Deck Implementation Plan

## Goal and Status

Deliver a MusicRaT performance player with the workflow expected from a CDJ/XDJ:
find a track, load it safely, see its structure and position, cue and mix it,
and operate loops, tempo, sync, and performance controls with predictable audio.
Use original MusicRaT styling and terminology rather than copying proprietary
assets or claiming compatibility with another vendor's media database.

This is a proposed implementation plan, not an implemented feature contract.
The inventory below is based on repository inspection at commit `bce384a`.
Implementation and validation progress is recorded in section 11; the inventory
above that section describes the starting point, not completed milestones.

Keep the existing architecture: independently launchable CommRaT modules,
renderer-neutral surfaces, typed controls and telemetry, and LVGL for runtime
operation. The Application Designer remains the pre-launch editor. RatGUI work
is deferred.

Related architecture:

- [Media playback](MEDIA_PLAYBACK.md)
- [Signals and ports](SIGNALS_AND_PORTS.md)
- [Application Designer and surfaces](APPLICATION_DESIGNER.md)
- [Project layout](PROJECT_LAYOUT.md)
- [EVL and target validation](EVL_AND_RATOS.md)

## 1. Existing Foundation

| Area | Available now | Limit for a performance deck |
| --- | --- | --- |
| File playback | Codec-neutral `AudioFilePlayer`, PCM WAV and optional FLAC/MP3/AAC/Opus backends, worker-side loading and decoding, generation-safe seeks | No integrated runtime track browser/library |
| Audio rendering | Bounded decode-ahead, varispeed, explicit starvation/discontinuity status | Four decode chunks in the current module; not a general hot-cue, loop, or scratch cache |
| Tempo and pitch | Rate changes and ramps; optional Rubber Band pitch lock | Runtime master-tempo switching, pitch ranges, bend gestures, and performance quality need explicit contracts and validation |
| Basic deck actions | Play/pause, set/return/clear cue, loop in/out and enable/disable, immediate/beat/bar quantization | No full cue-button state machine, cue banks, beat-loop sizes, slip, or jog semantics |
| Cue and loop execution | Allocation-free renderer operations over resident PCM | Nonresident cue returns/loops are rejected; normal multi-second DJ loops cannot be assumed to fit |
| Transport | Beat-grid representation/mapping, transport source, tempo/beat/bar following | No automatic track analysis or finished deck-master selection workflow |
| Playback telemetry | Media position, duration, rate, source rate, generation, worker/error state, buffer envelope, sync/pitch state | No complete cue/loop coordinates, track identity, rich metadata, or per-action results |
| Control routing | Semantic events, action mapper, descriptor actions/observations, Designer export | Action mapper ignores release edges; simultaneous touchscreen/hardware sources need aggregation |
| LVGL | Headless, SDL, DRM/KMS and framebuffer backends, touch input, basic widgets and keyboard, widget update queues | No waveform, virtualized browser, deck composite, or track-resource transport |
| Observation export | Level-meter and parameter-state adapters, bounded widget-update merger | Playback observation descriptors exist, but no implemented playback-to-LVGL adapter at the inspected baseline |
| Validation | Playback kernel/module tests, codec-to-WAV and control-action integration tests, LVGL tests | No complete interactive deck or dual-deck performance validation |

Primary implementation references:

- [Player module](../include/musicrat/modules/audio_file_player.hpp)
- [Deck events](../include/musicrat/protocol/deck_control.hpp)
- [Load/seek commands](../include/musicrat/protocol/file_player.hpp)
- [Playback status](../include/musicrat/protocol/playback_status.hpp)
- [Action mapper](../include/musicrat/dsp/action_mapper.hpp)
- [LVGL module](../include/musicrat/modules/lvgl_widget_sink.hpp)
- [LVGL renderer](../include/musicrat/backends/lvgl_widget_renderer.hpp)
- [Designer compiler](../tools/application-designer/src/control-compiler.ts)

## 2. User Experience

### Performance page

The user-provided deck-screen reference defines the intended information hierarchy,
not a pixel-copy requirement. The final performance page should resemble that
class of player, not the current diagnostic feedback window:

- A compact header for source/deck, artwork, title/artist, duration, original
  BPM/key and analysis/readiness.
- Most vertical space dedicated to a scrolling, zoomable detail waveform with
  a fixed central playhead, beat/downbeat grid and visible cue/loop boundaries.
- A compact beat/phase context strip above the detail view when supported.
- A lower status band with large elapsed/remaining time, effective tempo/pitch
  and range, BPM, key/master-tempo and quantize/sync state.
- A whole-track overview at the bottom with absolute-position playhead,
  colored/named cue markers, loop shading and guarded touch-to-seek.
- Browsing on a separate page, preserving playback and performance-page state;
  performance controls stay reachable without permanently covering waveforms.

Use MusicRaT-owned artwork/icons/fonts and original styling. At 800x480,
prioritize waveform, time and tempo legibility; validate the denser reference
hierarchy at 1280x720 rather than squeezing every control into the small layout.
Colored waveform bands need a documented frequency-analysis mapping, not
arbitrary colors or unlabelled amplitude envelopes. Musical key, BPM, beat/phrase
markers, artwork and hot cues must be unavailable until backed by metadata,
analysis or authoritative deck state; never populate plausible demo values.
The early browse/load prototype is a functional stepping stone, not final
visual acceptance. M2 must add screenshot/geometry acceptance at both sizes,
including paused, loading, missing analysis, active loop and end-of-track views.

- Track title, artist, artwork when available, codec, and load/analysis state.
- Large elapsed/remaining time toggle, end-of-track warning, and media position.
- Full-track overview with playhead, cue/hot-cue markers, loop shading, and seek.
- Scrolling detail waveform with fixed playhead, zoom, beat/downbeat lines,
  phrase markers when available, and separate visual treatment of the active loop.
- Original/effective BPM, pitch percentage and range, master-tempo state,
  quantize, sync/master state, and beat/phase indicators.
- Play/pause, CDJ-style cue behavior, search/seek, and tempo reset.
- Loop in/out, reloop/exit, beat-loop selection, halve/double, and beat jump.
- Hot-cue slots with set/trigger/delete, names/colors, and pending-action feedback.
- Level/clip meter and clear device, media, buffer, and error indicators.
- Optional jog position/ring display; physical jogging remains semantic control.

### Browse page

- Sources and folders first; search, playlists/crates, sorting, and filtering later.
- Virtualized rows showing title, artist, duration, BPM, key, and analysis status.
- Touch and encoder navigation, back, select, and explicit load-to-deck.
- The current track continues playing while browsing.
- Loading a playing deck requires the configured protection/confirmation policy.
- Unsupported/corrupt files, removed media, permission failures, and unavailable
  optional decoders produce actionable errors rather than a silent empty list.

### Settings and preparation

- Waveform zoom/color mode, time mode, pitch range, quantization, cue mode,
  load protection, and end-of-track behavior.
- Beat-grid adjustment and saving, hot-cue/memory-cue editing, and loop recall.
- Capability-dependent controls: do not show unsupported reverse, scratch,
  pitch-lock, or sync operations as if they were ready.

Start with one deck on an 800x480 reference layout, then validate a denser
1280x720 layout and dual-deck presentation. These are proposed reference sizes,
not fixed assumptions about the final physical display.

## 3. Ownership and Runtime Architecture

```text
Media library / analysis service (in-band)
  -> paged track records, prepared analysis, waveform tiles, request results
  -> prepared load request -> AudioFilePlayer worker

Touchscreen / hardware adapter
  -> semantic control streams
  -> bounded control aggregation and deck action mapping
  -> AudioFilePlayer real-time renderer
  -> audio processors / mixer / hardware output

AudioFilePlayer
  -> authoritative playback/deck state
  -> presentation adapter -> LVGL scalar updates
  -> waveform viewport aligned to that state
```

### Audio engine owns playback truth

The renderer owns actual position, play state, rate, cue/loop execution, and
applied action sequence. Its worker owns codec state and prepared PCM resources.
LVGL never implements an independent transport or infers success from a click.

Extend the existing player and renderer rather than creating another playback
engine inside a GUI module. Keep persistent defaults in typed parameters;
sample-timed performance changes use events; resource preparation uses commands.

### Execution domains: EVL OOB versus Linux in-band

**OOB** means execution on an EVL-attached, platform-managed real-time thread
under the EVL kernel. **In-band (no EVL execution)** means an ordinary Linux
thread that is not attached to EVL, even when it lives in an EVL-linked process.
A module/process is not uniformly real-time: its audio loop, command handlers,
decoder workers, and display thread have different ownership.

| Responsibility | Required domain | Rules |
| --- | --- | --- |
| PCM rendering, interpolation, DSP, mixing, sample-timed cue/loop actions | EVL OOB on RaTOS; same bounded kernels on standard Linux | Preallocated memory, deterministic loops, no syscalls, allocation, locks, logging, or exceptions |
| Ready-region acquire/release, event scheduling/quantization, transport following, playback-state capture | EVL OOB | Fixed-capacity queues/snapshots and lock-free atomics; never wait for workers |
| Bounded scalar status projection and widget-update handoff | OOB-safe bridge when run by CommRaT on EVL | No LVGL calls, locale formatting, filesystem access, RPC, or resource loading; bounded fixed-digit time conversion is permitted |
| File open/probe/decode/seek, performance-cache preparation and eviction | In-band, ordinary worker | Publish ready resources atomically; OOB consumes only completed resources |
| Library enumeration, search/index, removable-media monitoring, metadata/artwork | In-band | Paged/bounded results; cancellation and errors are explicit |
| Waveform pyramid, BPM/key/phrase analysis and cache persistence | In-band | Separate decoder; limit CPU/I/O so live decode retains priority |
| LVGL object creation/drawing, waveform tessellation, list virtualization, touch polling | In-band, one LVGL owner thread | Never attach this thread to EVL; drawing/DRM/SDL/input APIs may block or allocate |
| Browser/load command orchestration, localized labels, settings/session writes | In-band worker/UI service | UI callbacks enqueue requests rather than block on replies |
| MIDI/HID/USB/GPIO polling and device enumeration | In-band unless a specific EVL driver is proven | Cross into OOB using bounded semantic events; physical controls are not LVGL devices |
| Ordinary ALSA PCM calls | In-band device worker | Linux ALSA is not OOB-safe; use a bounded audio handoff |
| PipeWire callbacks | PipeWire-owned thread | Respect its scheduling and negotiated geometry; do not attach it to EVL |
| Native/SDK startup, descriptor/config parsing, allocation, shutdown | In-band initialization/teardown | Prepared before OOB rendering; lifecycle context must be audited rather than assumed in-band |

Using `corerat::Thread` in an EVL build attaches the thread to EVL; a low
priority or `realtime: false` is not proof of an unattached Linux thread.
Workers that must stay in-band use ordinary `std::jthread`/Linux threads and
ordinary waits. EVL-capable producer scheduling uses platform time/thread
abstractions, checked attachment, and absolute deadlines instead. Do not call
EVL waits from unattached workers or migrate the ALSA worker to OOB to reuse a
clock abstraction.

Cross-domain handoffs must have named owners and capacities:

- In-band -> OOB: prepared PCM/resource handles and timestamped semantic events.
  Never pointers to resizable worker storage; reclaim only after OOB releases.
- OOB -> in-band: coherent playback snapshots and bounded audio/telemetry queues.
  A slow display may coalesce snapshots; it must never stall rendering.
- Requests/results: correlated asynchronous transactions. Resource preparation
  and command waits do not execute inside `process()`.
- Overflow: observable counters/errors and an explicit policy. Droppable visual
  snapshots and loss-sensitive play/cue/release edges need different policies.
- Failure: missing PCM produces the defined underrun behavior, never a fallback
  decode/read in OOB. Loss of EVL attachment is not a successful real-time run.

The existing player decoder worker and LVGL drawing thread are ordinary
`std::jthread`s. Existing CommRaT command/lifecycle callbacks must still be
audited for blocking synchronization before claiming the complete deck OOB-safe.
Optional pitch-lock library calls also need target-side allocation and domain
switch evidence; the configuration alone is not certification.

Validation records must distinguish native functional correctness, SDK ABI
compatibility, EVL QEMU execution/domain-switch evidence, and physical-target
latency/dropout measurements. Add this domain ownership to each implementation
milestone's tests and review.

### Library and analysis are in-band services

Implement a launchable media-library service with command-associated output(s)
and asynchronous workers. Start with filesystem browsing, then add a persistent
searchable index if required. Public requests/replies and notifications remain
bounded even when the on-disk library is large.

Responsibilities:

- Managed media roots, pagination, sorting, cancellation, and source changes.
- Stable track identity distinct from path and file modification revision.
- Metadata and bounded artwork thumbnails.
- Analysis scheduling, progress, cache lookup/invalidation, and persistence.
- Prepared waveform/beat-grid/cue resources shared by deck instances.

Use the existing codec abstraction, but give analysis its own decoder instance.
Never scan a whole track through the live player's decode queue. Limit worker
CPU and storage bandwidth so analysis and browsing cannot starve playback.

### Presentation stays renderer-neutral

Add reusable waveform, track-list, and deck presentation models to the existing
surface architecture. Extend the LVGL renderer and adapter boundary rather than
launching a second owner for the same display.

The current LVGL module has one `WidgetUpdateBlock` synchronized input and an
existing bounded widget-update merger for scalar observation streams. Reuse it
for playback state plus meters. Waveform resources and browser results still
need explicit resource ingress; do not pack large assets into scalar updates.

Each added launchable service/adapter needs generated descriptors, stable port
IDs, parameters, examples, launcher validation, and matching tests.

## 4. Contracts to Define Before Building the Screen

### Track and analysis identity

Keep these identifiers separate:

- Track ID: persisted identity used for cues and library records.
- Asset revision: changes when the underlying media changes.
- Analysis revision: changes with algorithm/settings or edited analysis.
- Playback generation: changes with load/seek and rejects stale PCM/state.
- Request ID: correlates acceptance, progress, completion, rejection, and cancel.

A seek must not invalidate an unchanged whole-track waveform cache. Conversely,
late waveform tiles or load replies for a previous track must never repaint the
new track.

### Deck state and action results

Extend playback telemetry or introduce a companion bounded deck-state snapshot:

- Loaded track/revision, load request, preparation/readiness and error details.
- Position anchor, source sample rate, effective rate, play state, and clock.
- Main cue, bounded hot/memory cues, loop start/end/enabled/prepared state.
- Track beat grid/revision, effective BPM, quantize, sync/master and phase state.
- Pending and last applied/rejected action IDs with reason and execution time.
- Capability flags, buffer health, underrun counters, and latency information.

Publish coherent snapshots and decimate presentation traffic. Preserve schema
compatibility through an explicit migration/version policy.

### Control semantics

Specify and test state machines, not just buttons:

- Cue while paused sets/returns to cue; cue press can preview; release returns;
  play during preview latches playback according to the chosen mode.
- Hot cue can set or trigger a prepared slot; deleting is a separate gesture.
- Jog touch, movement, release, pitch bend, and stop behavior are distinct.
- Quantized actions report pending/applied/rejected, including queue overflow.
- Track replacement cancels track-scoped pending actions and held gestures.

The current action mapper discards nonpositive gate/boolean/trigger values.
Release-sensitive cue and jog behavior therefore needs explicit edge support.
Add provenance/action IDs and deterministic ordering for simultaneous hardware
and touch controls; audit freshness, deduplication, and delivery guarantees so
edge events cannot disappear through snapshot-style synchronized delivery.

### Commands and failure policy

Keep load, arbitrary seek, analysis, directory queries, and resource preparation
off the real-time thread. Acceptance is not completion. Specify bounded queue
limits, busy/rejected reasons, stale-result handling, retry policy, and timeouts.

Choose and implement track-replacement semantics explicitly. Recommended:
prepare the next track without destroying the current playable track, then
commit atomically when ready; retain current playback on preparation failure.
This required additional resource ownership beyond the inspected baseline;
section 11 records the implemented prepared-slot engine and remaining worker
scheduling/selection-service work.

## 5. Waveform and Analysis Pipeline

1. Decode on an analysis worker into bounded chunks.
2. Build multiresolution min/max and energy envelopes in source-frame space.
   Preserve transients; averaging alone is insufficient.
3. Persist an indexed waveform pyramid with track/asset/analysis revisions,
   sample rate, channel policy, level resolutions, and checksums/version.
4. Serve bounded overview/detail tiles at the resolution needed for the viewport.
5. Cache only a bounded working set in the presentation process.
6. Draw waveform, grid, cues, and loop as separate layers; invalidate only the
   viewport/overlays that changed.

Start with a monochrome amplitude waveform. Add independently computed
low/mid/high energy coloring after the waveform and timing contracts work.
Do not advertise spectral color or phrase analysis before implementing it.

Overview and detail share source-frame coordinates. The detail window follows
the authoritative player anchor and effective rate; visual interpolation is
bounded and snaps on seek, loop wrap, pause, discontinuity, or stale telemetry.
Do not extrapolate indefinitely after state delivery stops.

Define whether the anchor describes frame zero or the end of the rendered
block. Account separately for pitch-lock delay, downstream DSP delay, and
negotiated device buffering when aligning the display to audible playback.
Do not subtract latency twice or assume current telemetry is already an
audible-time cursor.

Beat/BPM/downbeat analysis runs off-thread and carries confidence plus manual
override. Respect the current 32-segment beat-grid bound: validate, simplify
within an explicit error limit, or revise the protocol; never silently truncate.
Key detection, phrase labels, and sophisticated variable-tempo analysis are
later preparation features. Manual/imported grids can unblock early sync tests.

## 6. Engine Work for Real Deck Behavior

### Prepared cue and loop storage

Replace reliance on the rolling four-chunk window with a separately budgeted
performance cache. Worker-prepared cue windows and loop regions transfer through
generation-safe handles; the real-time side only acquires ready bounded storage.

Size the budget against actual requirements. A four-beat loop at 120 BPM lasts
two seconds; stereo float PCM at 48 kHz occupies approximately 768 kB, before
overhead. Multiple cue windows, longer loops, and two decks multiply this cost.
Current availability must be measured from actual residency, not the buffered
envelope, which can include gaps.

Prepare nonresident cue/loop audio asynchronously. Display preparing/rejected
states, and execute only when the requested region is ready. Set maximum loop
length, number of pinned regions, eviction policy, and preparation deadlines.
Use click-safe transitions without violating the intended cue/loop boundary.

### Performance features

- Beat loops, loop resize/move/reloop, beat jump, and memory cue recall.
- Temporary pitch bend independent of the persistent tempo target.
- Explicit runtime pitch-lock/sync controls and supported ranges.
- Reverse-capable decode windows before enabling reverse or scratching.
- Jog acceleration/touch semantics and low-latency bidirectional resampling.
- Slip mode with a second virtual timeline and a prepared return transition.
- Defined end-of-track, emergency stop, device loss, and recovery behavior.

Scratch/reverse are a substantial cache/rendering milestone, not a negative-rate
slider. Preserve the existing varispeed and pitch-lock behavior while adding
these modes, and measure algorithmic latency and transition quality.

### Clock and dual-deck behavior

The current player is timer-driven. Progressively wire playback to the graph
audio-clock/process-context contract so independent decks and the device do not
silently run on unrelated clocks. Pre-roll is not a sustained drift solution.

Define one transport master, deck follower rules, manual tempo override, and
explicit master transfer. A dual-deck product also needs a mixer, crossfader,
headphone prelisten, cue/master routing, headroom/limiting policy, and suitable
multi-output hardware. These are not supplied by drawing two deck screens.

## 7. Designer and LVGL Work

- Add versioned surface capabilities for waveform, track list, transport/time,
  cue/loop overlays, and reusable deck layouts.
- Use stable module/port/track references, not hard-coded player instance names.
- Extend the shared validated target catalog for both graph wiring and dropdowns.
- Compile deck actions, playback observations, command remotes, resource routes,
  and required control/update buses into native CommRaT application JSON.
- Support one screen containing two decks, or one display per deck.
- Keep waveform DSP/analysis outside LVGL and React.
- Use LVGL custom drawing for dense waveforms, not one LVGL object per sample.
- Virtualize browser rows and bound artwork/tile caches.
- Keep all LVGL access on its owning in-band thread; do not perform RPC, decode,
  directory scans, or large resource reads in UI callbacks.
- Make unsupported widgets/routes fail validation explicitly.
- Persist user settings and track annotations without writing from audio code.

## 8. Delivery Milestones and Acceptance

| Milestone | Work | Acceptance gate |
| --- | --- | --- |
| M0: Contracts and performance budget | Finalize deck state/action/load semantics, first hardware/display profile, cache and clock plan | Protocol/state-machine tests; explicit capability table and resource limits |
| M1: First operable LVGL deck | Playback-state adapter, time/rate/status, play/pause/basic cue, folder browser and load workflow, control/update aggregation | SDL and headless example: browse, load, play, pause, seek; confirmed state and visible errors; existing playback tests preserved |
| M2: Waveform deck | Track identity/cache, analysis worker, overview/detail widgets, seek gestures, cue/loop overlays | Deterministic waveform fixtures, stale-result tests, zoom alignment, no live-decoder interference |
| M3: Performance cues and loops | Prepared PCM regions, cue-button behavior, hot/memory cues, beat loops/jump, quantization feedback and persistence | Nonresident recalls and multi-second loops work across supported codecs; transitions and exhaustion behavior tested |
| M4: Musical preparation and sync | BPM/downbeat analysis, grid editor, runtime tempo/pitch-lock/sync, phase display and master policy | Known-grid fixtures and two-deck sync tests; clear low-confidence/unsupported states |
| M5: Jog and advanced playback | Bend, reverse cache, scratch, slip, quality/latency tuning | Gesture replay tests, bounded cache misses, measured audible response on target hardware |
| M6: Complete player workflow | Search/crates, artwork/key, source hotplug, dual-deck mixer/headphone workflows, session recovery | Full browse-to-mix scenario and failure/recovery tests under concurrent analysis |
| M7: Product validation | Native/SDK/QEMU/installed-image gates, hardware soak and UX tuning | Versioned physical-target results meet the agreed performance and reliability budget |

M1 establishes the control/load loop; M2 establishes visual trust; M3 establishes
usable performance recall. M4 depends on reliable grids and runtime controls.
M5 depends on the M3 cache and explicit clock/latency design. Library enrichment
can proceed independently once M0 identities and requests are stable. Target
validation runs at every milestone that changes module execution, not only M7.

## 9. Validation and Initial Targets

These are proposed acceptance targets to ratify for the selected hardware:

- Detail waveform: target 60 Hz, sustained minimum 30 Hz under the declared
  dual-deck workload; record frame-time percentiles and missed frame budgets.
- Control-to-authoritative-visual-feedback: target p95 <= 50 ms; measure audible
  action latency separately, including the actual negotiated audio buffers.
- Display/audio cursor alignment: target absolute error <= 20 ms after explicit
  latency calibration; test rate changes, pitch lock, seeks, and loop wraps.
- Memory and queues: explicit per-deck PCM budget, tile/artwork limits, library
  page sizes, maximum cues/loops, event rates, and saturation counters.
- Audio reliability: no new underruns attributable to browsing, painting, or
  analysis during a declared target soak; report all counters, not just startup.

Tests must cover:

- Protocol roundtrips, capacities, identities, request cancellation, action
  ordering, release edges, and generation transitions.
- Waveform impulses/silence/antiphase stereo, pyramid boundaries, and tile levels.
- Position/time conversion across source rates, output rates, playback rates,
  unknown durations, EOF, latency, and discontinuities.
- Cue/loop/hot-cue state-machine tables, preparation failure, removed media,
  malformed files, cache exhaustion, and retained playback on failed replacement.
- Routed touch + hardware control, telemetry reconnection, slow UI consumers,
  resource/control queue overflow, and two deck instances sharing analysis.
- Headless LVGL geometry/render tests and SDL interaction/screenshot fixtures.
- Allocation/blocking checks for audio paths and continued codec integration.
- RaTOS SDK cross-build, EVL QEMU runtime, installed descriptors/examples, and
  physical DRM/touch/audio measurements. QEMU is not a latency benchmark.

Physical deployment must not overwrite installed binaries or interrupt an
existing graph without approval.

## 10. Scope Decisions Before Implementation

Recommended starting scope:

1. One local-file deck, SDL preview plus the existing DRM/touch target.
2. Folder browsing first; persistent indexed library behind the same contract.
3. Original MusicRaT deck layout with waveform + browse pages.
4. Manual or prepared beat grids before automatic analysis is production-ready.
5. Explicit bounded performance-cache limits rather than unlimited loops/cues.
6. No dependency on proprietary library formats or network-link protocols.

Confirm the physical screen/resolution, audio backend, control hardware,
single/dual-deck priority, maximum loop length/hot-cue count, media sizes,
desired scratch capability, and acceptable load/preparation times before fixing
resource budgets. Codec and Rubber Band licensing/distribution choices must be
reviewed for the intended product.

The first implementation slice should be M0 + M1, followed immediately by M2.
Do not start with a decorative waveform disconnected from the real player:
load outcomes, timing anchors, and authoritative feedback must work end to end.

## 11. Implementation Progress

Status checked against the working tree on 2026-10-30. These changes remain
uncommitted relative to the inspected baseline. A component being implemented
does not imply its milestone acceptance gate has passed.

| Area | Status | Evidence / remaining work |
| --- | --- | --- |
| M0 contracts and budgets | Partial | Execution domains, bounded browser pages, player-local load/track IDs and prepared replacement are defined; durable media identity, hardware budgets, and calibrated clock contracts remain open |
| M1 playback feedback | Implemented, native-tested | Bounded projection, Designer export, unavailable widgets, actual LVGL widget tests, and unloaded routed smoke example |
| M1 browser backend | Implemented, native-tested | Root-relative bounded snapshots, pagination, explicit failures, ordinary-thread worker and lifecycle tests |
| M1 browser presentation | Launchable workflow prototype, native-tested | Reused LVGL rows and validated selection connect to the player; separate Performance/Browse pages preserve selection; composite Designer surfaces remain missing |
| M1 player load workflow | Native remote path implemented | Ordinary load worker calls typed Remote<AudioBlock> LoadMedia RPC and consumes authoritative status; protected/failed/paused-commit paths tested |
| M1 time and guarded seek | Implemented prototype, native-tested | Elapsed/remaining time, paused overview click/outline drag and detail drag, one seek on release, shared load/seek admission and observed position; not atomic multi-client completion |
| Pitch fader | Implemented prototype, native-tested | Vertical ±16% control sends bounded rate events and follows player status; pitch-lock mode is displayed but not toggled |
| Track/current BPM | Partial, native-tested | Configured or matching imported ANLZ grid tempo, effective-rate BPM and signed offset; estimator is known-incorrect, while grid editing remains open |
| Beat/downbeat presentation | Partial, native-tested | Configured/imported variable-tempo lines in both views and bounded bar thinning; known-incorrect estimated lines are hidden; grid editing remains open |
| Rekordbox USB catalog | Partial, native-tested | Bounded classic `export.pdb` track/artist indexing, virtual flat browser, path confinement and file-identity revalidation; playlists/folders and `exportExt.pdb` remain open |
| M1 operable deck acceptance | Partial, not met | Native browse/load/play/pause/seek and authoritative basic cue/loop presentation exist; cue editing/recall, complete interactive SDL scenario and SDK/QEMU gates remain open |
| M2 waveform presentation | Partial, native-tested | Independent decoder worker, bounded overview/detail pyramid, stable colored scrolling/zoom, time ruler, viewport outline, authoritative cue/loop overlays, configured/imported beat lines and display-only imported cues; disk cache/tiles and target gates remain open |
| M3-M6 | Planned | No prepared performance cache, complete cue behavior, jog/scratch, or dual-deck workflow delivered by these slices |
| EVL/target validation | Pending | No SDK/QEMU or physical-target evidence for these new paths |

### Playback feedback and synchronous browser foundation

The first slice adds the execution-domain ownership above and a
`PlaybackStatusUiAdapter` with bounded scalar/time projection, descriptor,
launchable wrapper, native headless/SDL example configurations, and tests.
The Designer now generates playback observation adapters using its shared
Graph/UI target catalog and existing scalar widget-update merger. Unavailable
values have an explicit widget value kind and disable the affected LVGL
widget until valid state returns.

This begins M1's authoritative feedback path; it does not complete M0 or M1.
At that point, load contracts/protection, integrated browse/load workflow, richer
deck state, action-edge delivery, decimation/interpolation, and waveform resources
remained planned; load-engine progress is recorded below. Displayed media time is not yet a latency-calibrated
audible position. See [media playback](MEDIA_PLAYBACK.md#playback-status-to-lvgl)
for selector semantics and the feedback-only example.

The browser foundation adds an in-band-only `DirectoryBrowser` backend and bounded
directory query/page contracts. It provides deterministic snapshot pagination,
root-relative paths, explicit skipped-entry counts, and typed errors. It did
not add a launchable library service, list widget, or load orchestration in that
initial slice. Presentation progress follows below; integrated service/load
orchestration is still M1 work. The native validation preset provides a discoverable
host build/test configuration alongside the existing EVL presets.

### Previously recorded foundation validation

- Native builds pass for the playback adapter, browser library, player, launcher,
  LVGL sink, and their focused tests.
- Browser boundary/pagination tests and bounded protocol serialization pass.
- Playback projection kernel/module tests, headless LVGL renderer/sink tests,
  and the routed playback-status-to-LVGL launcher smoke test pass.
- The renderer test projects populated playback state into actual LVGL widgets.
  The launcher smoke test intentionally starts unloaded; it does not demonstrate
  browsing, loading, audible playback, or waveform rendering.
- Designer validation passes: 94 tests, TypeScript/Vite build, and focused lint.
- The existing audio-file-player module test failed once at its `rendered_audio`
  assertion and passed an isolated rerun. That intermittent failure remains
  unresolved; a passing rerun is not evidence that the broader suite is stable.
- SDK cross-build, EVL QEMU execution, interactive SDL testing, and physical
  display/audio measurements remain pending. No real-time certification or
  latency claim follows from these native tests.

### Asynchronous browser and LVGL presentation slice

`DirectoryBrowserWorker` owns the synchronous backend on an ordinary
`std::jthread`, never a `corerat::Thread` or CommRaT processing thread. One
client may submit one outstanding bounded query/page request. A second request
is explicitly busy until the client consumes the retained result; requests and
results are not overwritten. Configuration and filesystem failures arrive as
typed result errors. Destruction wakes and joins the worker, including when a
result is undrained. Filesystem syscalls are not forcibly cancellable, so neither
shutdown nor request completion has a hard deadline.

`LvglDirectoryBrowser` is an owning-thread LVGL component using eight reused
row objects, not a row per directory entry. It polls bounded worker results,
disables stale interactions while requests are pending, and supports folder
entry, Up, Refresh, Previous/Next, file selection, and visible failure/recovery.
The selection handler must only enqueue a bounded intent; it must not perform
RPC, load, decode, or filesystem work inside the LVGL callback.

The explicit **Use selection** handoff originally carried the root-relative entry
and snapshot ID. It now validates on the ordinary worker first, as detailed
below. It is disabled without a handler and reports queued or rejected, never
loaded/ready. A snapshot ID is local to this browser, not a track identity or
durable file handle.

The standalone `musicrat_lvgl_browser_preview` is a development preview with
headless and optional SDL display, not another application graph format or a
launchable CommRaT library module. It has no player connected. It is not yet a
surface-manifest widget and is not exposed by the Designer.

Native validation of this slice covers worker busy/result retention, request
correlation, stable/stale snapshots, invalid roots and requests, scan limits,
idle/queued/undrained shutdown, real LVGL navigation and fixed-row reuse,
800x480 row geometry, empty/removed directories, selection rejection/acceptance,
and headless preview success/failure. Playback-feedback renderer, sink and
unloaded routed smoke tests are also rerun. This is native functional evidence,
not SDL interaction, EVL attachment, latency, or uninterrupted-audio evidence.

On 2026-10-30, 11 focused native tests pass (plus the routed test's router
fixtures). The three directly relevant Designer suites also pass: 34 tests for
control export, graph targets, and module-picker behavior. The earlier full
94-test/build/lint result above is historical, not a fresh full-suite result.

### Prepared replacement engine and correlated results

The player now wraps two preallocated decode/render slots. An ordinary worker
opens and initially decodes into the inactive slot. Failed preparation leaves
active resources/generation unchanged. The render thread adopts prepared PCM
at a block boundary and acknowledges retirement only after releasing old
renderer leases; the worker then closes/reuses the retired slot.

The user-selected policy is to **reject replacement on a playing deck by
default**. Persistent `protect_playing_loads` metadata exposes this policy, and
the player enforces it at adoption, not only at command acceptance or in UI.
Initial loading into an empty deck is allowed; startup/lifecycle restoration is
separate. Rate and play/pause state survive a successful commit; new track
position starts at zero and old cue/loop/queued controls are cleared.

`LoadMedia::Reply` adds a player-assigned request ID and explicit rejection
reason. Playback status adds the latest request's preparing/prepared/committed/
failed/protected/cancelled state and candidate decode error, separate from the
active track error. The active `track_instance_id` survives seeks; it is local
to this player lifetime, not a persisted media identity or content fingerprint.
The command/status wire shapes changed: rebuild communicating binaries together.

Load-state/error observations are available through the C++ adapter and shared
Designer selector map, including when no valid audio is loaded. The feedback
example now includes a load-result choice display. This is still feedback-only:
the browser's Use selection handoff is not connected to a player command.

This slice establishes **retained resources on failed preparation**, not
gapless preparation. Both slots share the current ordinary decode worker:
long candidate probing can delay active refill and cause explicit underrun.
The doubled pool/renderer footprint and optional pitch state need a target
budget. Separate preparation/refill scheduling, durable media identity, load
timeouts and path/handle revalidation remain open. The existing command
callback's ordinary mutex/CV ingress is not newly certified for EVL.

Native tests cover failed open, corrupt/empty/partially decoded candidates,
playing-deck protection (including play starting during preparation), paused
commit, preserved rate, cleared stale cues/controls, repeated slot reuse,
generation/track identity across seeks, lifecycle cancellation/unload, concurrent
worker/render handoffs, and allocation-/decoder-free varispeed adoption.
Protocol tests roundtrip the extended replies/status and the load selector
test keeps C++/Designer numeric IDs explicit. Existing player-to-WAV, action,
transport, quantized-start, pitch-lock and LVGL-feedback tests pass.

Validation for this slice: 21 focused native tests pass, plus their router
fixtures. This includes FLAC/MP3/AAC/Opus-to-WAV regressions. The extended
module test also passes after adding stop/restart recovery and explicit
protection-policy opt-out. Designer validation passes 35 focused tests,
TypeScript build-mode checking and focused lint. Runtime command behavior is
tested through direct module callbacks and protocol serialization, not yet
an integrated browser-to-remote-command roundtrip. SDK/QEMU, interactive SDL
and physical-target validation remain pending.

### Validated selection and load-correlation components

The ordinary directory worker now resolves only exact current-snapshot file
members. Snapshots retain POSIX device/inode/size and nanosecond mtime/ctime
beside each file entry. Selection checks the root identity, directory components
and current file identity; stale, missing, changed/replaced, symlink and non-file
targets fail explicitly. Validation returns a bounded resolved path, not an open
file lease. Concurrent path mutation between checks and the decoder's later
open remains possible; this is neither content hashing nor a security sandbox.

The actual LVGL **Use selection** path queues this validation, disables pending
interaction and invokes its enqueue-only handler only on success. Typed worker
polls cannot accidentally consume the other result kind. Changed media produces
a visible refresh/reselect error instead of entering the load handoff.

The renderer-neutral `control::SelectionLoad` component relates selection and
snapshot IDs to command acceptance and the player's request ID. It distinguishes
validation failure, rejection, preparing/prepared and matching terminal status;
only an authoritative committed result with matching active track ID is success.
Timeout/disconnection produces outcome-unknown and prevents another request.
Late responses can reconcile it; malformed replies block reuse until a confirmed
new session. The caller still owns ordinary-thread RPC and status subscription.

Tests exercise the selection-to-command-to-player-result path through the real
player's direct callbacks (protected while playing, committed while paused).
They do **not** exercise native remote RPC or a connected SDL browser/player.
The new SDL preset, reusable demo script and VS Code task start the existing
800x480 **MusicRaT Deck State** feedback graph with null audio output. Its window
was verified on the desktop, but it starts unloaded and has no browser, waveform
or interactive transport controls. M1 acceptance is still not met.

Native validation passes nine focused tests: browser, selection metadata/path
boundaries, worker retention/type isolation, selection/load correlation, bounded
protocol serialization, actual LVGL validation/error recovery, both preview
smoke cases and direct player load integration. The player integration also
passes a separate repeat. An initial run exposed an over-fast callback harness
and transient missing PCM during candidate work: polling now paces 10 ms blocks
instead of rendering them every 1 ms, and retention checks wait for recovered
PCM rather than asserting gapless candidate preparation. The shared-worker
underrun limitation remains. Builds retain existing descriptor-initializer and
LVGL deprecation warnings. Whitespace/script-syntax checks pass; no fresh
Designer, SDK/QEMU or physical-target validation was performed for this slice.

### Native RPC and connected LVGL workflow slice

`MusicRaTLvglDeck` is a launchable workflow prototype with a native descriptor,
flat startup parameter metadata, typed deck-control output, synchronized playback
status input and command-only player remote. The original native application example
uses the existing player and null sink (the later waveform slice switches the SDL
variant to a device sink); there is no GUI playback engine or
second graph format. It starts unloaded and its media root defaults to the
launcher's current directory, which the user may override in the application.

The UI validates browser selections on the directory worker, then queues one
bounded selection to `DeckLoadWorker`. Its separate ordinary `std::jthread`
owns the typed load RPC, 500 ms reply deadline, configurable completion timeout
(default 10 seconds) and matching status reconciliation. Neither filesystem
operations nor blocking RPC run in LVGL callbacks or CommRaT `process()`.
The periodic bridge only copies bounded status and drains semantic play/pause
events. Status-queue overflow is counted and displayed; current load/UI state
is retried if the worker-to-UI queue is full.

The view shows the committed filename, authoritative media time/rate/play state,
preparing/prepared/terminal load feedback, rejection/decode codes and stale
connection state. Playing-deck protection remains enforced in the player.
Acceptance is not displayed as loaded; failed/protected replacement preserves
the current title/track. Play/pause clicks enqueue semantic events and wait for
status confirmation rather than toggling a local transport.

No automatic resend follows a lost reply. Completion timeout with continuous
status can reconcile a late matching result. A status gap during an accepted
pending load, or detected sequence rollback, makes the player session uncertain:
do not match a potentially reused player-local ID after reconnection. New loads
remain blocked and the UI requires confirming the player and restarting the
deck. A durable player-session token and a richer reconnect protocol are still
needed. Decoder preparation/refill still share the player's worker.

Native tests now cover real CommRaT remote roundtrips and routed status/control
streams, including initial commit, playing protection, pause/commit, corrupt-file
failure retaining the track, play and lost-status detection. A headless launcher
smoke validates the complete example/descriptor preflight. Worker tests cover
busy admission, unavailable-before-send, rejection, missing/malformed replies,
RPC exceptions, completion timeout, late terminal reconciliation, uncertain
sessions, status overflow and bounded FIFO wraparound. These are native tests,
not EVL or physical latency evidence. The SDL prototype is visually checked at
800x480; it is intentionally not the final reference waveform page.

The new remote exposed a tightly coupled metadata-preflight issue: known audio
payload identity must not force a command-only remote's signal domain to audio.
Remote ports are now validated as command-domain targets; continuous audio
ports retain their existing payload-domain validation. Regression tests cover
both valid command remotes and rejected audio-domain remotes.

Validation for this slice: nine focused native tests pass, plus router fixtures.
They include both actual RPC and full launcher preflight/lifecycle, the extended
load worker/correlation tests, browser worker/LVGL selection regressions, protocol
serialization and selection path boundaries. Changed files have no editor
diagnostics; whitespace and demo-script syntax checks pass. The connected SDL
window was captured/inspected at 800x480 for readable rows, visible load state
and disabled unloaded transport, but manual loading of a user track is not
claimed. SDK/QEMU, physical audio, final visual-reference acceptance and Designer
composite export remain pending.

### Separate pages, time mode and guarded paused seeking

The launchable deck now uses a reusable `LvglDeckView` with persistent Performance
and Browse children. A shared header retains actual play state, committed name,
time/rate and operation feedback while navigation preserves browser selection.
Elapsed/remaining mode reuses the bounded playback-status adapter's time
formatting. It reports source-media time, not an audible or rate-adjusted ETA.
The performance page explicitly labels waveform/grid, BPM/key and cue/loop
analysis as unavailable; the slider is a position preview, not a waveform.

An explicit **Seek (paused)** action passes observed track/generation and target
to the ordinary worker. The worker revalidates fresh status and requires a paused
player, sync off, no loop/decoder/invalid/sync-pending state, a known source
rate/duration and an in-range target. Loads and seeks serialize through one
bounded admission slot. Preview changes are local; track/generation changes
invalidate them. Acceptance is distinct from observing the requested paused
position within one source frame in newer-sequence/newer-generation status.

Rejected, invalid, disconnected and decode-error paths are visible. Missing RPC
replies and completion timeouts retain an unknown outcome without automatic
retry. A safely matching late observation can reconcile a timeout; a status gap
or rollback during pending work instead makes the session uncertain and blocks
reuse. Existing `SeekMedia` lacks expected-track/generation and command-ID fields,
so these UI/worker guards are **not engine-side atomic protection** against
another controller or restart. Multi-client ownership and correlated seek results
remain work, not implied by the position-observed label.

Execution domains are unchanged: filesystem, RPC, timeout clocks, formatting and
LVGL run on ordinary in-band threads. Audio rendering and adoption of prepared
seek generations remain bounded render-path operations. Native tests are not
evidence of EVL certification.

Validation: eight focused native tests pass (plus router fixtures), covering
actual LVGL navigation/time/seek events, unavailable and stale-target behavior,
worker reply/timeout/disconnection cases, browser/load regressions and actual
player RPC seeking to source frame 480000 with unchanged track ID and increased
generation. The updated SDL window was inspected at 800x480; a rate-formatting
defect exposed by LVGL's float-disabled formatter was fixed using in-band
standard formatting and a displayed-rate regression assertion. Full interactive
SDL acceptance, 1280x720 visual acceptance and SDK/QEMU execution remain pending.

### Background overview and audible SDL demo

This subsection records the original revision-1 slice; the following subsection
supersedes its waveform feature and memory limits.

The first M2 slice provides a real whole-track waveform, not synthetic data or
a scrolling detail view. `WaveformWorker` owns an independent `MediaDecoder` on
an ordinary, non-EVL-attached `std::jthread`. It decodes configured-size blocks
into at most 1024 source-frame bins, preserving all-channel min/max transients
and computing RMS energy without opposite-polarity mono cancellation.
The LVGL Performance page draws peaks/RMS, actual player position (yellow) and
the selected paused-seek preview (orange). Frame coordinates, not wall-clock
extrapolation, drive the overlays. Waveform touch seeking is not added.

The load worker now retains the committed local path and its pre-RPC file
identity. Analysis checks that identity before/after independent decoding and
requires exact duration/source-rate/channel agreement with player telemetry.
Path/device/inode/size/nanosecond mtime/ctime, decoded shape and analysis revision
identify one in-memory cached asset. Newer requests replace pending work and
cancel older analysis between blocks; both worker publication and the view
reject stale results. A seek generation does not invalidate the asset.
Unavailable external paths, changing files, decoder/metadata failures and
progress are explicit, not success-shaped empty graphics.

This is a bounded overview/cache foundation only: no on-disk cache, checksummed
asset identity, multiresolution pyramid, detail tiles, spectral colors, beat grid,
or cue/loop coordinates are claimed. A completed envelope is approximately
12 KiB, independent of track duration, plus one configured decode block,
one in-progress envelope, one cached envelope and bounded UI/result references.
Independent decoding still consumes CPU/disk bandwidth; no dropout-resistance
claim follows. File-stat identity is not a content hash or an atomic association
with the player's open file, and no active file watcher is implemented.
Blocking filesystem/probe calls also prevent a hard cancellation deadline.

The generated **SDL** application now uses `MusicRaTAudioDeviceSink` on the default
device at the CMake-default rate/period with stereo channels. The **headless**
application retains `NullSink` for hardware-free CI. Mono/multichannel playback
requires an explicit matching sink/channel-map configuration. PipeWire callback
ownership is unchanged; neither analysis nor LVGL/device management moved into
OOB processing. The demo's PipeWire stereo ports were verified linked to the
host's default Speaker + Headphones output, and a committed MP3 with a ready
overview was observed in the 800x480 SDL window. This is connection/visual
evidence, not a physical audio, latency or dropout measurement.

Ten focused native tests pass (plus router fixtures). They cover deterministic
peaks/RMS, anti-phase transients, short and
non-divisible source-frame bins, 800/1280-column projection, uint64 boundaries,
cache reuse and asset changes, invalid files/metadata, supersession/unload,
LVGL pixel rendering and real routed player-load/analysis integration.
SDK cross-build, EVL QEMU, 1280x720 visual acceptance and physical measurements
remain pending. M1 and M2 are still partial.

### Scrolling detail, 3-band blend and RGB

The next M2 slice extends the same independently decoded asset to analysis
revision 2. A bounded multiresolution detail pyramid retains full-signal
min/max/RMS and low/mid/high RMS. Base bins span at least 64 frames and increase
with duration to cap the base at 65536 bins; at most 17 levels preserve
frame-weighted energy and signed extrema, including partial tail bins.
Peak records occupy approximately 3 MiB per maximum-sized pyramid, plus a
24 KiB overview and small metadata/container overhead. Candidate/cached
generations and decoder buffers require additional memory; hardware budgets
remain unvalidated.

The detail view scrolls under a centered player-position line, above the
whole-track overview. Zoom +/- selects 2/4/8/16/32 source-media seconds. Source
position changes and seeks snap both views without wall-clock extrapolation.
Track boundaries leave blank space; the available base-frame resolution is
displayed rather than claiming sample detail at arbitrary zoom.

Wave mode cycles Mono, 3-band and RGB on both views. The 3-band mode blends
overlapping red/green/blue RMS envelopes on one baseline (low/mid/high), not
three separate lanes. RGB continuously mixes those band proportions in one
signed peak/RMS waveform. Both use actual filter-derived data: per-channel
second-order Butterworth low-pass at 250 Hz, a 250 Hz high-pass / 2500 Hz low-pass
mid branch, and 2500 Hz high-pass. The branches overlap near their crossovers;
they are visualization estimates, not brick-wall bands or proprietary color
matching. Existing biquad kernels are reused without changing playback audio.
All analysis, allocation, cache lifetime, projection and LVGL drawing remain
ordinary in-band work; no OOB processing path gains filtering or allocation.

Mode/zoom navigation reuses the asset and survives page/track changes within
the view. New-track/session invalidation, stale-result suppression and
seek-generation cache reuse remain in force. No disk cache, viewport tile
protocol, beat/grid/cue/loop overlays, audible-time calibration, SDK/QEMU or
physical performance evidence is implied.

Native tests verify pure and mixed low/mid/high tones at 44.1/48/96 kHz,
anti-phase stereo, additive and RGB color mapping, pyramid energy/partial tails,
fixed memory ceilings, narrow transient projection, real LVGL colors and
zoom limits, centered cursor alignment through position changes, unchanged
command counts and routed player/decoder regressions. PCM tones also exposed
an MP3 probe accepting data inside valid WAV containers; strict WAV probing now
precedes permissive codecs and has a decoder regression test.

Ten distinct focused native tests pass (plus router fixtures): waveform analysis,
LVGL view, routed deck RPC, launcher smoke, codec dispatch, player, prepared load,
prepared coordinator, WAV coordinator and decode-ahead. The updated 800x480 SDL
layout was inspected and its ordinary device sink's stereo PipeWire links were
verified. This does not close interactive loaded-track/1280x720 acceptance,
SDK/QEMU validation or physical audio measurement gates. M1 and M2 remain partial.

### Stable scrolling and source-time navigation context

The reported RGB shimmer came from viewport-relative aggregation: fractional
scrolls changed the intersecting analysis bins and their per-band maxima, making
the envelope and hue change independently of track translation. Projection now
uses a source-frame pixel lattice tied to source zero, the current zoom and
display width. Its origin rounds to the nearest source pixel; moving by whole
pixels reproduces the same envelope/color cells at translated screen positions.
This applies to all modes. Peaks are still conservatively retained, without
temporal averaging or blurred seeks. The authoritative playhead stays centered;
waveform/ruler raster alignment differs by at most half a pixel.

The next M2 presentation slice adds a source-time ruler and an overview viewport
outline. Ruler ticks are source anchored at 0.5/1/2/4/8 second intervals for the
2/4/8/16/32 second zoom levels and clipped to the actual track. The overview
outline shows the visible track interval, including correct clipping at
start/end. Both overlays update on authoritative position, seek/generation and
zoom changes and disappear with unavailable/stale waveform data. They are
read-only presentation: no additional decoder work, transport events or seek
gestures. These are source-time coordinates, not fabricated beat/cue/loop data.
All projection, formatting and drawing remain ordinary in-band work.

Four focused native tests pass (waveform analysis, real LVGL view pixels, routed
deck RPC and launcher smoke, plus router fixtures). The regressions verify
fractional/integer scrolling of height and band colors at 776/800/1280 columns,
negative origins, and actual translated LVGL pixels in all three modes.
Ruler coordinates cover every zoom at 44.1/48/96 kHz; overview borders cover
zoom/movement, track boundaries and disconnection, without command side effects.
Full interactive target/display acceptance remains open; this slice does not
change the OOB renderer or claim latency/dropout validation.

At the end of this slice, cue/loop presentation still needed authoritative
validity and source-frame coordinates: status exposed only loop-enabled, and no
marker was guessed from UI intent or elapsed time. The later authoritative
cue/loop feedback slice below closes this telemetry/presentation gap, not editing
or prepared recall. M1 and M2 remain partial.

### Paused waveform gestures and track/current BPM

The position slider and Seek button are removed. A paused overview tap selects
an absolute track position, a drag inside the overview window preserves the grab
offset, and a detail drag moves the waveform beneath the fixed playhead. Preview
window/time are explicitly distinguished from authoritative position. Release
inside the originating surface sends one seek, never a stream of decoder seeks
while dragging. A detail tap alone does not seek. Leaving/cancelling, navigation,
zoom, play/pause action, track/generation/shape change, disconnection or loss of
eligibility drops the preview without a seek. Existing loop/sync/busy/error and
freshness guards, ordinary RPC ownership and observed-position reconciliation
remain; the engine's multi-client/atomic command limitations are unchanged.

Track BPM and Current BPM are displayed alongside signed tempo offset.
The player validates its configured grid once and publishes the local segment
tempo times effective rate (including pitch-lock playback). Grid association is
restricted to the configured startup path's actual committed track ID and matching
source sample rate. Failed replacement keeps it; replacement with another track
clears it. Per-track grid import/association for browser loads, editing, provenance
and automatic detection are not yet implemented; those loads correctly show `--`.
During a gesture, BPM remains explicitly at the player position until the new
segment is observed. No beat/downbeat lines are fabricated from BPM alone.

`PlaybackStatusBlock` adds a grid-track ID and track/current BPM scalars; all
communicating modules must be rebuilt together. Only bounded segment lookup and
scalar publication run on the render thread; gesture projection, seek RPC,
decoding, preview/time/tempo formatting and LVGL remain in-band.

Native regressions use an actual LVGL pointer device to verify tap, drag
directions, outline grab offset, preview-before-release, exactly one request,
endpoint clamping, leaving/re-entering, state changes, page/zoom cancellation and
busy rejection/retry. Player tests verify variable-tempo segment seeks, effective
rate changes, pitch lock, startup association, failed/successful replacement,
wrong sample rates and invalid/missing grids. Protocol round trips cover the new
fields. Eleven focused native tests pass (plus router fixtures): LVGL view,
waveform analysis, deck worker, file player/load, status serialization, status
adapter kernel/module, selection/load correlation, routed deck RPC and launcher
smoke. SDK/QEMU and physical/interactive acceptance remain open.

### Authoritative cue and loop feedback

The renderer's stored cue and loop boundaries now reach the deck through a
fixed-size `DeckMarkerState` and matching `marker_track_instance_id` in
`PlaybackStatusBlock`. Independent validity flags distinguish unset coordinates
from frame zero. Status reflects the actual post-control renderer state.
Only bounded state copying and set-bit maintenance are added to rendering;
projection, labels and shading remain ordinary in-band work.

Both waveform views show a purple C marker, loop I/O lines, amber stored ranges
and green active ranges. Boundaries are validated against source duration and
track identity; Out is exclusive and may equal duration. Partial boundaries are
visible independently, reversed/equal ranges are not shaded, offscreen markers
are omitted and intersecting shading clips to the view. Text explicitly calls
stored-loop readiness unknown. No marker is derived from GUI intent or a stale
track; active state must agree with the existing looping flag.

Successful replacement clears marker state, failed/protected replacement retains
it, and unload publishes no markers. Existing seek behavior retains coordinates
but disables looping. Existing implicit zero-In loop activation is preserved.
No cue/loop editing buttons, hot cues, prepared residency or per-action results
are claimed. At this point grid import/association, beat/downbeat lines and
operational cue/loop controls remained subsequent slices, with prepared storage
required for dependable nonresident recall. Configured-grid lines are added below.

Native coverage includes serialization, frame-zero/partial/invalid states,
renderer activation/clear, no render allocation, replacement/unload and actual
LVGL pixels in both views (including inactive-to-active redraw with a stationary
playhead). Rebuild all communicating modules for the extended wire shape.
SDK/QEMU and physical validation remain open; M1/M2 are not complete.

### Configured-grid beat/downbeat presentation

The next bounded slice publishes the existing validated startup grid as
`PlaybackStatusBlock.beat_grid`, using the same immutable mapper and matching
grid-track ID that supply BPM. Both waveform views draw dim beats and brighter
bar/downbeat lines. Piecewise source-frame projection supports variable tempo,
fractional tempo seams and nonzero grid origins; no pre-roll or end-of-asset
beats are fabricated. Meter comes from the grid, not a hard-coded 4/4 pattern.
Cue/loop markers and actual/preview playheads retain drawing priority.

The detail view uses the same source-anchored pixel alignment as the envelope.
Each view has a 128-line bound and at least 12-pixel separation; dense views thin
to bars or power-of-two multiples of bars, always anchored to beat zero. The
fastest segment determines thinning for the viewport span, preventing a
scroll-induced change when crossing a tempo seam. Projection does not affect
playback, beat scheduling, quantization or seeking.

The legend identifies configured/imported metadata, an explicitly unreliable
tempo estimate, or unavailable/invalid projection. Track, sample rate, validity,
connection and session guards suppress stale lines. The grid status snapshot
remains bounded to 32 segments repeated with each status (up to 768
segment-field bytes plus metadata); a lower-bandwidth metadata channel is not
claimed.

Real-time changes are limited to copying a prevalidated bounded snapshot.
Display validation, tick selection, formatting and LVGL remain in-band. Track
tempo estimation runs on an ordinary thread and publishes only bounded scalars/
grid snapshots; no decode/filesystem/RPC work is added to the render path.
Rebuild communicating modules together for the wire change. Native coverage now
includes a tempo-analysis worker test, maximum-capacity routed status and
serialization, module association/lifecycle, all detail zooms and long overview
thinning, variable tempo, fractional seams, several sample rates/meters/widths,
actual LVGL pixels, fractional-scroll stability, cue priority and stale/
disconnected/uncertain state. SDK/QEMU and physical acceptance remain open.

### Known-incorrect tempo estimate

The existing worker decodes up to 120 seconds, builds a hop-wise novelty
envelope, and uses bounded autocorrelation to publish a heuristic 4/4 grid
tagged `BEAT_GRID_FLAG_ANALYZED`. The user has reported this analysis is
incorrect. It remains in the code only as an explicitly warned diagnostic
estimate: no beat lines are drawn from it, and it is not suitable as a source of
truth for BPM, beat alignment, quantization or sync. The current tests establish
bounded structure and worker lifecycle, not analytical correctness. Replacing
or disabling this estimator remains required.

### Classic Rekordbox ANLZ metadata slice

The ordinary deck load worker now scans bounded `PIONEER/USBANLZ` files after
the selected track commits, matches the embedded `PPTH` path and imports
`PQTZ` beat timings and `PCO2` cues (using `PCOB` when no extended cues exist).
Imported grids take precedence over the known-incorrect estimate. Up to eight
cue positions and one loop range are drawn as display-only metadata; recall,
editing, and engine-authoritative cue/loop actions are not implemented by this
slice.

The native synthetic fixture covers path matching, variable tempo, cue/loop
timestamps, malformed sections, and out-of-root requests. A read-only
single-track smoke test imported a real mounted `PQTZ` grid; no non-empty cue
sections were found on that export, so cue decoding remains synthetic-fixture
tested.

### Classic Rekordbox PDB track catalog

The ordinary directory browser now adds a virtual **Rekordbox Library** entry
when the configured root contains a classic `PIONEER/rekordbox/export.pdb`.
The bounded reader traverses the linked track and artist tables and publishes a
flat artist/title listing for records with a media file present beneath the
configured root. Selection uses the existing root-constrained resolution and
file-identity revalidation, so database paths cannot directly load outside the
selected USB root. On the mounted read-only export the parser returned 236
records, and the browser resolved all 236 local files.

This slice does not yet decode playlist/folder tree tables, filter by playlist,
read `exportExt.pdb`, or show artwork/BPM/key columns. The parser is based on
the public [crate-digger export format documentation](https://github.com/Deep-Symmetry/crate-digger/blob/main/doc/modules/ROOT/pages/exports.adoc)
and [PDB Kaitai schema](https://github.com/Deep-Symmetry/crate-digger/blob/main/src/main/kaitai/rekordbox_pdb.ksy);
no third-party parser code was copied. Full-tree validation, SDK/QEMU execution
and physical-target validation remain pending.

### Remaining M1 integration

1. Pass the complete browse/load/play/pause/seek/cue scenario through actual UI
   interactions and validate on supported displays.
2. Expose the shared deck presentation and page composites through Designer
   surfaces/export; validate both target display sizes.
3. Add a player-session token/reconnect handshake and explicit multi-client
   ownership before supporting independent player restarts with pending loads.
4. Separate slow candidate preparation from active refill before claiming
   uninterrupted audio under concurrent preparation.
5. Pass the M1 browse/load/play/pause/seek acceptance scenario and SDK/QEMU
   execution gates before marking M1 complete.

### Deck presentation cleanup

The 800x480 deck page now has one symbol-based play/pause control and one
Browse/Deck navigation toggle. The redundant Performance navigation button and
simultaneous Play/Pause text buttons are removed. Zoom and waveform-color
controls are integrated into the detail waveform, while time, signed tempo
offset, Track BPM and Current BPM are presented in separate lower panels. The
pitch fader remains beside the waveform. LVGL drawing and interaction stay
in-band; this presentation change adds no real-time work or transport contract.

The focused deck-view test verifies transport/navigation controls, tempo/BPM
labels, waveform overlays and pixels, and existing seek/marker behavior. Native
visual acceptance at 1280x720, SDL review on the target display, and SDK/QEMU
validation remain open. The main-cue interaction is described in the completed
slice below; prepared audio residency still limits dependable return/preview.

### CDJ-style momentary main CUE button

The deck uses one momentary CUE button, separate from Play/Pause. While paused
away from the cue, a press stores/replaces the main cue at the current source
frame. Holding CUE while paused at that point auditions playback; release
returns to the cue and pauses. A short press while playing returns to the cue
and pauses; holding for 150 ms starts a preview from the cue until release,
which returns to the cue and pauses. Rekordbox hot cues remain display-only.

The CUE down/hold/up gesture is represented by bounded typed deck-control
events. The real-time renderer handles cue-frame changes, preview state, return
and marker publication without allocation or blocking. The 150 ms hold
threshold, queueing and status reconciliation remain in-band. A full control
queue preserves a pending release for delivery on the next available output
block, avoiding a stuck preview due only to queue saturation. The view confirms
set/return/preview completion from track-matched marker and playback status;
missing confirmation is explicit and never automatically retried. When the cue
chunk has already been released by decode-ahead, the renderer pauses and raises
a bounded cue-seek request (real-time); the player worker performs the decoder
seek (in-band), and the renderer keeps the held/preview gesture across the
resulting same-track generation.

Native tests cover paused set/overwrite, paused and playing hold/release
preview, short playing return, the one-button pointer interaction, evicted-cue
reseek in the renderer, and routed events played one second past the cue so the
cue audio is no longer resident. SDK/QEMU, physical/display acceptance, and a
pinned cue buffer for reseek-free return remain open.
