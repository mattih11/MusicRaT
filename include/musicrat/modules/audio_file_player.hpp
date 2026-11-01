#pragma once

#include <musicrat/backends/media/media_decoder.hpp>
#include <musicrat/backends/media/prepared_playback_coordinator.hpp>
#include <musicrat/backends/media/tempo_grid_analysis.hpp>
#include <musicrat/launcher/audio_format_validation.hpp>
#include <musicrat/dsp/beat_grid.hpp>
#include <musicrat/dsp/deck_control_quantizer.hpp>
#include <musicrat/dsp/transport_follower.hpp>
#include <musicrat/musicrat.hpp>
#include <musicrat/protocol/file_player.hpp>

#include <algorithm>
#include <chrono>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <limits>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <string_view>
#include <thread>

namespace CommRaT {

class AudioFilePlayer : public MusicRaT::Module2<
    commrat::Output<Messages::AudioBlock>,
    commrat::Output<Messages::PlaybackStatusBlock>,
    commrat::SyncedInput<Messages::DeckControlEventBlock>,
    commrat::SyncedInput<Messages::TransportBlock>,
    commrat::Period<commrat::Milliseconds(musicrat::config::default_block_period_ms)>,
    commrat::Params<Parameters::FilePlayer>
> {
    using Base = MusicRaT::Module2<
        commrat::Output<Messages::AudioBlock>,
        commrat::Output<Messages::PlaybackStatusBlock>,
        commrat::SyncedInput<Messages::DeckControlEventBlock>,
        commrat::SyncedInput<Messages::TransportBlock>,
        commrat::Period<commrat::Milliseconds(musicrat::config::default_block_period_ms)>,
        commrat::Params<Parameters::FilePlayer>>;

    static constexpr std::size_t decode_chunk_count = 4;

public:
    static musicrat::launcher::DescriptorMetadata descriptor_metadata() {
        using namespace musicrat::launcher;
        auto metadata = audio_source_metadata("output_sample_rate_hz");
        metadata.musicrat_ports.ports = {
            {
                .id = "audio_out",
                .display_name = "Audio Out",
                .direction = PORT_DIRECTION_OUTPUT,
                .port_index = 0,
                .domain = PORT_DOMAIN_AUDIO,
            },
            {
                .id = "playback_status",
                .display_name = "Playback Status",
                .direction = PORT_DIRECTION_OUTPUT,
                .port_index = 1,
                .domain = PORT_DOMAIN_TELEMETRY,
            },
            {
                .id = "deck_control",
                .display_name = "Deck Control",
                .direction = PORT_DIRECTION_SYNCED_INPUT,
                .port_index = 0,
                .domain = PORT_DOMAIN_CONTROL,
                .required = false,
            },
            {
                .id = "transport",
                .display_name = "Transport",
                .direction = PORT_DIRECTION_SYNCED_INPUT,
                .port_index = 1,
                .domain = PORT_DOMAIN_TRANSPORT,
                .required = false,
            },
        };
        metadata.musicrat_parameters.parameters = {
            {
                .id = Parameters::FILE_PLAYER_PATH_PARAMETER_ID,
                .name = "path",
                .display_name = "Media Path",
                .group = "Media",
                .kind = PARAMETER_KIND_TEXT,
            },
            {
                .id = Parameters::FILE_PLAYER_OUTPUT_SAMPLE_RATE_PARAMETER_ID,
                .name = "output_sample_rate_hz",
                .display_name = "Output Sample Rate",
                .group = "Audio",
                .kind = PARAMETER_KIND_INTEGER,
                .unit = "Hz",
                .minimum = 8000.0,
                .maximum = 192000.0,
                .step = 1.0,
            },
            {
                .id = Parameters::FILE_PLAYER_INITIAL_RATE_PARAMETER_ID,
                .name = "initial_rate",
                .display_name = "Initial Rate",
                .group = "Playback",
                .kind = PARAMETER_KIND_CONTINUOUS,
                .unit = "x",
                .minimum = 0.25,
                .maximum = 4.0,
                .step = 0.01,
            },
            {
                .id = Parameters::FILE_PLAYER_AUTOPLAY_PARAMETER_ID,
                .name = "autoplay",
                .display_name = "Autoplay",
                .group = "Playback",
                .kind = PARAMETER_KIND_BOOLEAN,
            },
            {
                .id = Parameters::FILE_PLAYER_PITCH_LOCK_PARAMETER_ID,
                .name = "pitch_lock",
                .display_name = "Pitch Lock",
                .group = "Playback",
                .kind = PARAMETER_KIND_BOOLEAN,
            },
            {
                .id = Parameters::FILE_PLAYER_TRANSPORT_SYNC_MODE_PARAMETER_ID,
                .name = "transport_sync_mode",
                .display_name = "Transport Sync",
                .group = "Transport",
                .kind = PARAMETER_KIND_CHOICE,
                .choices = {
                    {.value = Messages::TRANSPORT_SYNC_OFF, .label = "Off"},
                    {.value = Messages::TRANSPORT_SYNC_TEMPO, .label = "Tempo"},
                    {.value = Messages::TRANSPORT_SYNC_BEAT, .label = "Beat"},
                    {.value = Messages::TRANSPORT_SYNC_BAR, .label = "Bar"},
                },
            },
            {
                .id = Parameters::FILE_PLAYER_PROTECT_PLAYING_LOADS_PARAMETER_ID,
                .name = "protect_playing_loads",
                .display_name = "Protect Playing Deck",
                .group = "Media",
                .kind = PARAMETER_KIND_BOOLEAN,
                .unit = "",
                .choices = {},
            },
            {
                .id = Parameters::FILE_PLAYER_SYNC_RATE_RAMP_PARAMETER_ID,
                .name = "sync_rate_ramp_frames",
                .display_name = "Sync Rate Ramp",
                .group = "Transport",
                .kind = PARAMETER_KIND_INTEGER,
                .unit = "frames",
                .minimum = 0.0,
                .maximum = 4294967295.0,
                .step = 1.0,
            },
        };
        metadata.musicrat_actions.actions = {
            {.id = "play", .display_name = "Play", .group = "Transport",
             .input_port_id = "deck_control", .kind = SEMANTIC_KIND_TRIGGER,
             .event_type = Messages::DECK_CONTROL_PLAY, .default_value = 1.0},
            {.id = "pause", .display_name = "Pause", .group = "Transport",
             .input_port_id = "deck_control", .kind = SEMANTIC_KIND_TRIGGER,
             .event_type = Messages::DECK_CONTROL_PAUSE, .default_value = 1.0},
            {.id = "set_rate", .display_name = "Set Rate", .group = "Playback",
             .input_port_id = "deck_control", .kind = SEMANTIC_KIND_CONTINUOUS,
             .event_type = Messages::DECK_CONTROL_SET_RATE,
             .minimum = -4.0, .maximum = 4.0, .default_value = 1.0},
            {.id = "ramp_rate", .display_name = "Ramp Rate", .group = "Playback",
             .input_port_id = "deck_control", .kind = SEMANTIC_KIND_CONTINUOUS,
             .event_type = Messages::DECK_CONTROL_RAMP_RATE,
             .minimum = -4.0, .maximum = 4.0, .default_value = 1.0},
            {.id = "set_cue", .display_name = "Set Cue", .group = "Cue",
             .input_port_id = "deck_control", .kind = SEMANTIC_KIND_TRIGGER,
             .event_type = Messages::DECK_CONTROL_SET_CUE, .default_value = 1.0},
            {.id = "return_to_cue", .display_name = "Return to Cue", .group = "Cue",
             .input_port_id = "deck_control", .kind = SEMANTIC_KIND_TRIGGER,
             .event_type = Messages::DECK_CONTROL_RETURN_TO_CUE, .default_value = 1.0},
            {.id = "clear_cue", .display_name = "Clear Cue", .group = "Cue",
             .input_port_id = "deck_control", .kind = SEMANTIC_KIND_TRIGGER,
             .event_type = Messages::DECK_CONTROL_CLEAR_CUE, .default_value = 1.0},
            {.id = "cue_button_down", .display_name = "CUE Button Down", .group = "Cue",
             .input_port_id = "deck_control", .kind = SEMANTIC_KIND_TRIGGER,
             .event_type = Messages::DECK_CONTROL_CUE_BUTTON_DOWN, .default_value = 1.0},
            {.id = "cue_button_hold", .display_name = "CUE Button Hold", .group = "Cue",
             .input_port_id = "deck_control", .kind = SEMANTIC_KIND_TRIGGER,
             .event_type = Messages::DECK_CONTROL_CUE_BUTTON_HOLD, .default_value = 1.0},
            {.id = "cue_button_up", .display_name = "CUE Button Up", .group = "Cue",
             .input_port_id = "deck_control", .kind = SEMANTIC_KIND_TRIGGER,
             .event_type = Messages::DECK_CONTROL_CUE_BUTTON_UP, .default_value = 1.0},
            {.id = "set_loop_start", .display_name = "Set Loop Start", .group = "Loop",
             .input_port_id = "deck_control", .kind = SEMANTIC_KIND_TRIGGER,
             .event_type = Messages::DECK_CONTROL_SET_LOOP_START, .default_value = 1.0},
            {.id = "set_loop_end", .display_name = "Set Loop End", .group = "Loop",
             .input_port_id = "deck_control", .kind = SEMANTIC_KIND_TRIGGER,
             .event_type = Messages::DECK_CONTROL_SET_LOOP_END, .default_value = 1.0},
            {.id = "enable_loop", .display_name = "Enable Loop", .group = "Loop",
             .input_port_id = "deck_control", .kind = SEMANTIC_KIND_TRIGGER,
             .event_type = Messages::DECK_CONTROL_ENABLE_LOOP, .default_value = 1.0},
            {.id = "disable_loop", .display_name = "Disable Loop", .group = "Loop",
             .input_port_id = "deck_control", .kind = SEMANTIC_KIND_TRIGGER,
             .event_type = Messages::DECK_CONTROL_DISABLE_LOOP, .default_value = 1.0},
        };
        metadata.musicrat_observables.observables = {
            {.id = "load_state", .display_name = "Load Result", .group = "Media",
             .output_port_id = "playback_status", .selector = "load_state",
             .kind = SEMANTIC_KIND_CHOICE, .unit = "", .maximum = 6.0},
            {.id = "load_decode_error", .display_name = "Load Decode Error", .group = "Media",
             .output_port_id = "playback_status", .selector = "load_decode_error",
             .kind = SEMANTIC_KIND_CHOICE, .unit = "", .maximum = 10.0},
            {.id = "elapsed_time", .display_name = "Elapsed Time", .group = "Timeline",
             .output_port_id = "playback_status", .selector = "elapsed_time",
             .kind = SEMANTIC_KIND_TEXT},
            {.id = "remaining_time", .display_name = "Remaining Time", .group = "Timeline",
             .output_port_id = "playback_status", .selector = "remaining_time",
             .kind = SEMANTIC_KIND_TEXT},
            {.id = "position", .display_name = "Track Position", .group = "Timeline",
             .output_port_id = "playback_status", .selector = "position",
             .kind = SEMANTIC_KIND_CONTINUOUS},
            {.id = "playback_rate", .display_name = "Playback Rate", .group = "Playback",
             .output_port_id = "playback_status", .selector = "playback_rate",
             .kind = SEMANTIC_KIND_CONTINUOUS, .unit = "x",
             .minimum = -4.0, .maximum = 4.0},
            {.id = "media_position", .display_name = "Media Position", .group = "Timeline",
             .output_port_id = "playback_status", .selector = "media_position_frames",
             .kind = SEMANTIC_KIND_CONTINUOUS, .unit = "frames",
             .maximum = 18446744073709551615.0},
            {.id = "duration", .display_name = "Duration", .group = "Timeline",
             .output_port_id = "playback_status", .selector = "duration_frames",
             .kind = SEMANTIC_KIND_CONTINUOUS, .unit = "frames",
             .maximum = 18446744073709551615.0},
            {.id = "beat_position", .display_name = "Beat Position", .group = "Timeline",
             .output_port_id = "playback_status", .selector = "beat_position",
             .kind = SEMANTIC_KIND_CONTINUOUS, .unit = "beats",
             .maximum = 1000000000.0},
            {.id = "ready", .display_name = "Ready", .group = "State",
             .output_port_id = "playback_status", .selector = "flags.ready",
             .kind = SEMANTIC_KIND_BOOLEAN},
            {.id = "playing", .display_name = "Playing", .group = "State",
             .output_port_id = "playback_status", .selector = "flags.playing",
             .kind = SEMANTIC_KIND_BOOLEAN},
            {.id = "looping", .display_name = "Looping", .group = "State",
             .output_port_id = "playback_status", .selector = "flags.looping",
             .kind = SEMANTIC_KIND_BOOLEAN},
            {.id = "worker_state", .display_name = "Worker State", .group = "State",
             .output_port_id = "playback_status", .selector = "worker_state",
             .kind = SEMANTIC_KIND_CHOICE, .maximum = 4.0},
            {.id = "decode_error", .display_name = "Decode Error", .group = "State",
             .output_port_id = "playback_status", .selector = "decode_error",
             .kind = SEMANTIC_KIND_CHOICE, .maximum = 10.0},
        };
        return metadata;
    }

    explicit AudioFilePlayer(const commrat::ModuleConfig& config)
        : Base(config)
        , player_(
            Messages::AudioBlock::MAX_FRAMES,
            decode_chunk_count,
            this->params_.output_sample_rate_hz,
            this->params_.pitch_lock)
        , frames_per_period_(calculate_frame_count(
            config, this->params_.output_sample_rate_hz)) {
        validate_params();
        if (musicrat::dsp::BeatGridMapper{this->params_.beat_grid}.valid()) {
            tempo_grid_.emplace(this->params_.beat_grid);
        }
        if (this->params_.transport_sync_mode != Messages::TRANSPORT_SYNC_OFF) {
            transport_follower_.emplace(
                this->params_.beat_grid,
                musicrat::dsp::TransportFollower::Settings{
                    .minimum_rate = player_.minimum_rate(),
                    .maximum_rate = player_.maximum_rate(),
                });
        }
        static_cast<void>(player_.set_rate_immediate(this->params_.initial_rate));
        player_.set_playing(this->params_.autoplay);
        this->template register_command_handler<
            0, Messages::LoadMedia, &AudioFilePlayer::handle_load>(*this);
        this->template register_command_handler<
            0, Messages::SeekMedia, &AudioFilePlayer::handle_seek>(*this);
        this->template register_command_handler<
            0, Messages::UnloadMedia, &AudioFilePlayer::handle_unload>(*this);
    }

protected:
    void on_start() override {
        worker_ = std::jthread([this](std::stop_token stop_token) {
            worker_loop(stop_token);
        });
    }

    void on_stop() override {
        if (worker_.joinable()) {
            worker_.request_stop();
            request_ready_.notify_one();
            worker_.join();
        }
        {
            std::lock_guard lock(request_mutex_);
            if (pending_request_.type == RequestType::Load) {
                player_.worker_cancel_request(pending_request_.load_request_id);
            }
            pending_request_ = {};
            lifecycle_request_ = {};
            request_in_progress_ = false;
        }
        analyzed_track_.store(0, std::memory_order_release);
        analyzed_sample_rate_hz_.store(0, std::memory_order_release);
        analyzed_tempo_bpm_.store(0.0, std::memory_order_release);
        analyzed_confidence_per_mille_.store(0, std::memory_order_release);
        player_.shutdown();
    }

    commrat::LifecycleResult on_enable() override {
        if (!this->params_.path.empty()) {
            PendingRequest request{};
            request.type = RequestType::Load;
            request.path = this->params_.path;
            enqueue_lifecycle(request);
        }
        return commrat::LifecycleResult::Success;
    }

    void on_disable() override {
        PendingRequest request{};
        request.type = RequestType::Unload;
        enqueue_lifecycle(request);
    }

    void process(
        const commrat::Synced<Messages::DeckControlEventBlock>& deck_controls,
        const commrat::Synced<Messages::TransportBlock>& transport,
        Messages::AudioBlock& output,
        Messages::PlaybackStatusBlock& status) override {
        musicrat::backends::media::TempoGridAnalysisResult analysis{};
        while (tempo_analysis_.poll(analysis)) {
            if (!analysis.valid) continue;
            analyzed_track_.store(analysis.request_id, std::memory_order_release);
            analyzed_sample_rate_hz_.store(analysis.sample_rate_hz, std::memory_order_release);
            analyzed_tempo_bpm_.store(analysis.tempo_bpm, std::memory_order_release);
            analyzed_confidence_per_mille_.store(analysis.confidence_per_mille, std::memory_order_release);
        }
        const Messages::DeckControlEventBlock* controls = nullptr;
        if (deck_controls.is_fresh()) {
            controls = &deck_controls.value();
            ++deck_control_blocks_received_;
        }
        const Messages::TransportBlock* transport_block = nullptr;
        if (transport.is_fresh()) {
            transport_block = &transport.value();
            ++transport_blocks_received_;
        }
        update_transport_sync(transport);
        const uint64_t generation = player_.generation();
        if (generation != controls_generation_) {
            if (controls_generation_ != 0) {
                control_quantizer_.reset();
            }
            controls_generation_ = generation;
        }
        const auto quantizer_result = control_quantizer_.process(
            controls,
            transport_block,
            frames_per_period_,
            quantized_controls_);
        quantized_actions_pending_ = quantizer_result.pending_count;
        quantized_actions_dropped_ += quantizer_result.dropped_count;
        controls = quantized_controls_.events.empty()
            ? nullptr
            : &quantized_controls_;
        const uint64_t timestamp_ns = commrat::Time::now();
        const uint64_t sequence_number = sequence_number_++;
        const auto result = player_.render(
            this->params_.output_sample_rate_hz,
            frames_per_period_,
            timestamp_ns,
            sequence_number,
            output,
            controls);
        if (generation != player_.generation()) {
            control_quantizer_.reset();
            quantized_actions_pending_ = 0;
            transport_seek_frame_.store(no_transport_seek, std::memory_order_release);
            cue_seek_frame_.store(no_transport_seek, std::memory_order_release);
        }
        if (uint64_t cue_seek_frame = 0; player_.take_seek_request(cue_seek_frame)) {
            cue_seek_frame_.store(cue_seek_frame, std::memory_order_release);
        }
        decltype(player_)::PlaybackDiagnostics diagnostics{};
        const bool diagnostics_current = player_.try_read_diagnostics(diagnostics)
            && diagnostics.generation == player_.generation();
        status = {
            .media_position_frames = result.media_position,
            .playback_rate = player_.current_rate(),
            .minimum_playback_rate = player_.minimum_rate(),
            .maximum_playback_rate = player_.maximum_rate(),
            .beat_position = current_beat_position(),
            .transport_beat_position = transport_beat_position_,
            .sync_phase_error_beats = sync_phase_error_beats_,
            .generation = player_.generation(),
            .timestamp_ns = timestamp_ns,
            .sequence_number = sequence_number,
            .duration_frames = diagnostics_current
                ? diagnostics.duration_frames
                : uint64_t{0},
            .resident_frames = diagnostics_current
                ? diagnostics.resident_frames
                : uint64_t{0},
            .buffered_start_frame = diagnostics_current
                ? diagnostics.buffered_start_frame
                : uint64_t{0},
            .buffered_end_frame = diagnostics_current
                ? diagnostics.buffered_end_frame
                : uint64_t{0},
            .quantized_actions_dropped = quantized_actions_dropped_,
            .deck_control_blocks_received = deck_control_blocks_received_,
            .transport_blocks_received = transport_blocks_received_,
            .source_sample_rate_hz = diagnostics_current
                ? diagnostics.source_sample_rate_hz
                : uint32_t{0},
            .algorithmic_latency_frames = player_.algorithmic_latency_frames(),
            .quantized_actions_pending = quantized_actions_pending_,
            .channel_count = output.channel_count,
            .source_channel_count = diagnostics_current
                ? diagnostics.source_channel_count
                : uint16_t{0},
            .codec = diagnostics_current
                ? playback_codec(diagnostics.codec)
                : Messages::PLAYBACK_CODEC_UNKNOWN,
            .worker_state = diagnostics_current
                ? playback_worker_state(diagnostics.worker_state)
                : Messages::PLAYBACK_WORKER_PREPARING,
            .decode_error = diagnostics_current
                ? playback_decode_error(diagnostics.decode_error)
                : Messages::PLAYBACK_DECODE_ERROR_NONE,
            .pitch_mode = player_.pitch_lock_enabled()
                ? Messages::PLAYBACK_PITCH_MODE_LOCKED
                : Messages::PLAYBACK_PITCH_MODE_VARISPEED,
            .transport_sync_mode = transport_follower_
                ? this->params_.transport_sync_mode
                : Messages::TRANSPORT_SYNC_OFF,
            .flags = status_flags(output),
            .track_instance_id = player_.track_instance_id(),
            .load_request_id = player_.load_status().request_id,
            .load_state = player_.load_status().state,
            .load_decode_error = playback_decode_error(player_.load_status().decode_error),
            .marker_track_instance_id = player_.track_instance_id(),
            .markers = player_.track_instance_id() != 0 ? player_.markers() : Messages::DeckMarkerState{},
        };
        const bool startup_grid = tempo_grid_ && diagnostics_current && status.track_instance_id != 0
            && status.track_instance_id == startup_grid_track_.load(std::memory_order_acquire)
            && tempo_grid_->grid().sample_rate_hz == status.source_sample_rate_hz
            && status.duration_frames != 0 && std::isfinite(status.media_position_frames)
            && status.media_position_frames >= 0;
        if (startup_grid) {
            const auto bpm = tempo_grid_->tempo_at_frame(std::min(status.media_position_frames,
                static_cast<double>(status.duration_frames - 1)));
            const auto current = bpm * status.playback_rate;
            if (std::isfinite(current) && current > 0) {
                status.beat_grid_track_instance_id = status.track_instance_id;
                status.track_bpm = bpm;
                status.current_bpm = current;
                status.beat_grid = tempo_grid_->grid();
            }
        } else if (diagnostics_current && status.track_instance_id != 0
            && status.track_instance_id == analyzed_track_.load(std::memory_order_acquire)
            && status.source_sample_rate_hz == analyzed_sample_rate_hz_.load(std::memory_order_acquire)
            && status.duration_frames != 0
            && std::isfinite(status.playback_rate) && status.playback_rate > 0) {
            const auto bpm = analyzed_tempo_bpm_.load(std::memory_order_acquire);
            const auto current = bpm * status.playback_rate;
            if (std::isfinite(bpm) && bpm > 0 && std::isfinite(current) && current > 0) {
                status.beat_grid_track_instance_id = status.track_instance_id;
                status.track_bpm = bpm;
                status.current_bpm = current;
                status.beat_grid.sample_rate_hz = status.source_sample_rate_hz;
                status.beat_grid.beats_per_bar = 4;
                status.beat_grid.beat_unit = 4;
                status.beat_grid.confidence_per_mille = analyzed_confidence_per_mille_.load(std::memory_order_acquire);
                status.beat_grid.flags = Messages::BEAT_GRID_FLAG_ANALYZED;
                static_cast<void>(status.beat_grid.segments.push_back({.tempo_bpm = bpm}));
            }
        }
    }

private:
    enum class RequestType : uint8_t {
        None,
        Load,
        Seek,
        Unload,
    };

    struct PendingRequest {
        RequestType type{RequestType::None};
        sertial::fixed_string<512> path{};
        uint64_t media_frame{0};
        uint64_t load_request_id{0};
        bool protect_playing{true};
    };

    static uint32_t calculate_frame_count(
        const commrat::ModuleConfig& config,
        double sample_rate_hz) {
        if (!config.period.has_value() || config.period->count() <= 0
            || !std::isfinite(sample_rate_hz) || sample_rate_hz <= 0.0) {
            throw std::invalid_argument(
                "AudioFilePlayer requires a valid period and sample rate");
        }
        const auto frames = static_cast<uint64_t>(std::llround(
            sample_rate_hz * static_cast<double>(config.period->count()) / 1000.0));
        if (frames == 0 || frames > Messages::AudioBlock::MAX_FRAMES) {
            throw std::invalid_argument(
                "AudioFilePlayer period exceeds audio block capacity");
        }
        return static_cast<uint32_t>(frames);
    }

    void validate_params() const {
        if (!std::isfinite(this->params_.initial_rate)
            || this->params_.initial_rate < player_.minimum_rate()
            || this->params_.initial_rate > player_.maximum_rate()) {
            throw std::invalid_argument(
                "AudioFilePlayer initial rate is out of range");
        }
        if (this->params_.transport_sync_mode > Messages::TRANSPORT_SYNC_BAR) {
            throw std::invalid_argument(
                "AudioFilePlayer transport sync mode is invalid");
        }
        if (this->params_.transport_sync_mode != Messages::TRANSPORT_SYNC_OFF
            && !musicrat::dsp::BeatGridMapper{this->params_.beat_grid}.valid()) {
            throw std::invalid_argument(
                "AudioFilePlayer transport sync requires a valid beat grid");
        }
    }

    [[nodiscard]] uint16_t status_flags(
        const Messages::AudioBlock& output) const noexcept {
        uint16_t flags = 0;
        if (output.channel_count > 0) {
            flags |= Messages::PLAYBACK_STATUS_READY;
        }
        if (player_.playing()) {
            flags |= Messages::PLAYBACK_STATUS_PLAYING;
        }
        if ((output.flags & Messages::AUDIO_BLOCK_UNDERRUN) != 0) {
            flags |= Messages::PLAYBACK_STATUS_UNDERRUN;
        }
        if ((output.flags & Messages::AUDIO_BLOCK_END_OF_STREAM) != 0) {
            flags |= Messages::PLAYBACK_STATUS_END_OF_STREAM;
        }
        if ((output.flags & Messages::AUDIO_BLOCK_DISCONTINUITY) != 0) {
            flags |= Messages::PLAYBACK_STATUS_DISCONTINUITY;
        }
        if ((output.flags & Messages::AUDIO_BLOCK_INVALID) != 0) {
            flags |= Messages::PLAYBACK_STATUS_INVALID;
        }
        if (player_.loop_enabled()) {
            flags |= Messages::PLAYBACK_STATUS_LOOPING;
        }
        if (transport_follower_ && transport_seen_) {
            flags |= Messages::PLAYBACK_STATUS_TRANSPORT_FOLLOWING;
        }
        if (transport_seek_frame_.load(std::memory_order_relaxed)
            != no_transport_seek) {
            flags |= Messages::PLAYBACK_STATUS_SYNC_SEEK_PENDING;
        }
        if (quantized_actions_pending_ != 0) {
            flags |= Messages::PLAYBACK_STATUS_QUANTIZED_ACTION_PENDING;
        }
        return flags;
    }

    void update_transport_sync(
        const commrat::Synced<Messages::TransportBlock>& transport) noexcept {
        if (!transport_follower_ || !transport.is_fresh()) {
            return;
        }
        const auto& block = transport.value();
        if (transport_seen_ && block.sequence_number == transport_sequence_number_) {
            return;
        }
        transport_seen_ = true;
        transport_sequence_number_ = block.sequence_number;
        transport_beat_position_ = block.beat_position;
        const auto decision = transport_follower_->update(
            block,
            this->params_.transport_sync_mode,
            player_.media_position());
        if (!decision.valid) {
            return;
        }
        sync_phase_error_beats_ = decision.phase_error_beats;
        player_.set_playing(decision.playing);
        if (decision.action == musicrat::dsp::TransportSyncAction::SetRate) {
            static_cast<void>(player_.ramp_rate(
                decision.target_rate,
                this->params_.sync_rate_ramp_frames));
        } else if (decision.action == musicrat::dsp::TransportSyncAction::Seek) {
            static_cast<void>(player_.set_rate_immediate(decision.target_rate));
            publish_transport_seek(decision.seek_frame);
        }
    }

    void publish_transport_seek(uint64_t media_frame) noexcept {
        uint64_t expected = no_transport_seek;
        if (!transport_seek_frame_.compare_exchange_strong(
                expected,
                media_frame,
                std::memory_order_release,
                std::memory_order_relaxed)) {
            return;
        }
    }

    [[nodiscard]] double current_beat_position() const noexcept {
        if (!transport_follower_) {
            return 0.0;
        }
        const auto position = transport_follower_->position_at_frame(
            player_.media_position());
        return position.valid ? position.beat_position : 0.0;
    }

    static Messages::PlaybackCodec playback_codec(
        musicrat::backends::media::MediaCodec codec) noexcept {
        switch (codec) {
        case musicrat::backends::media::MediaCodec::PcmWav:
            return Messages::PLAYBACK_CODEC_PCM_WAV;
        case musicrat::backends::media::MediaCodec::Flac:
            return Messages::PLAYBACK_CODEC_FLAC;
        case musicrat::backends::media::MediaCodec::Mp3:
            return Messages::PLAYBACK_CODEC_MP3;
        case musicrat::backends::media::MediaCodec::Aac:
            return Messages::PLAYBACK_CODEC_AAC;
        case musicrat::backends::media::MediaCodec::Opus:
            return Messages::PLAYBACK_CODEC_OPUS;
        case musicrat::backends::media::MediaCodec::Unknown:
            return Messages::PLAYBACK_CODEC_UNKNOWN;
        }
        return Messages::PLAYBACK_CODEC_UNKNOWN;
    }

    static Messages::PlaybackWorkerState playback_worker_state(
        musicrat::backends::media::DecodeAheadState state) noexcept {
        switch (state) {
        case musicrat::backends::media::DecodeAheadState::Closed:
            return Messages::PLAYBACK_WORKER_CLOSED;
        case musicrat::backends::media::DecodeAheadState::Ready:
            return Messages::PLAYBACK_WORKER_READY;
        case musicrat::backends::media::DecodeAheadState::EndOfStream:
            return Messages::PLAYBACK_WORKER_END_OF_STREAM;
        case musicrat::backends::media::DecodeAheadState::Error:
            return Messages::PLAYBACK_WORKER_ERROR;
        }
        return Messages::PLAYBACK_WORKER_ERROR;
    }

    static Messages::PlaybackDecodeError playback_decode_error(
        musicrat::backends::media::DecodeError error) noexcept {
        return static_cast<Messages::PlaybackDecodeError>(error);
    }

    bool enqueue(const PendingRequest& request) {
        {
            std::lock_guard lock(request_mutex_);
            if (pending_request_.type != RequestType::None || request_in_progress_
                || lifecycle_request_.type != RequestType::None) {
                return false;
            }
            pending_request_ = request;
        }
        request_ready_.notify_one();
        return true;
    }

    void enqueue_lifecycle(const PendingRequest& request) {
        {
            std::lock_guard lock(request_mutex_);
            lifecycle_request_ = request;
            if (request.type == RequestType::Load) {
                if (load_request_counter_ == std::numeric_limits<uint64_t>::max()) {
                    throw std::overflow_error("AudioFilePlayer load request IDs exhausted");
                }
                lifecycle_request_.load_request_id = ++load_request_counter_;
                lifecycle_request_.protect_playing = false;
                startup_grid_track_.store(lifecycle_request_.load_request_id, std::memory_order_release);
            }
        }
        request_ready_.notify_one();
    }

    void worker_loop(std::stop_token stop_token) noexcept {
        while (!stop_token.stop_requested()) {
            static_cast<void>(player_.worker_refill());
            if (player_.worker_load_pending()) {
                std::unique_lock lock(request_mutex_);
                if (lifecycle_request_.type != RequestType::None) {
                    lock.unlock();
                    if (player_.worker_cancel_load()) continue;
                    lock.lock();
                }
                request_ready_.wait_for(lock, std::chrono::milliseconds{2}, [&] {
                    return stop_token.stop_requested();
                });
                continue;
            }
            PendingRequest request{};
            {
                std::unique_lock lock(request_mutex_);
                request_ready_.wait_for(
                    lock,
                    std::chrono::milliseconds{2},
                    [&] {
                        return stop_token.stop_requested()
                            || pending_request_.type != RequestType::None
                            || lifecycle_request_.type != RequestType::None
                            || transport_seek_frame_.load(
                                std::memory_order_acquire) != no_transport_seek
                            || cue_seek_frame_.load(
                                std::memory_order_acquire) != no_transport_seek;
                    });
                if (stop_token.stop_requested()) {
                    break;
                }
                request_in_progress_ = false;
                if (lifecycle_request_.type != RequestType::None) {
                    if (pending_request_.type == RequestType::Load) {
                        player_.worker_cancel_request(pending_request_.load_request_id);
                    }
                    pending_request_ = {};
                    request = lifecycle_request_;
                    lifecycle_request_ = {};
                } else {
                    request = pending_request_;
                    pending_request_ = {};
                }
                request_in_progress_ = request.type != RequestType::None;
            }

            switch (request.type) {
            case RequestType::Load:
                transport_seek_frame_.store(
                    no_transport_seek, std::memory_order_release);
                cue_seek_frame_.store(
                    no_transport_seek, std::memory_order_release);
                if (player_.worker_prepare(
                        request.path.c_str(), request.load_request_id, request.protect_playing)) {
                    static_cast<void>(tempo_analysis_.submit(
                        request.load_request_id, request.path.c_str()));
                }
                break;
            case RequestType::Seek:
                transport_seek_frame_.store(
                    no_transport_seek, std::memory_order_release);
                cue_seek_frame_.store(
                    no_transport_seek, std::memory_order_release);
                static_cast<void>(player_.worker_seek(request.media_frame));
                break;
            case RequestType::Unload:
                transport_seek_frame_.store(
                    no_transport_seek, std::memory_order_release);
                cue_seek_frame_.store(
                    no_transport_seek, std::memory_order_release);
                static_cast<void>(player_.worker_close());
                break;
            case RequestType::None:
                if (const uint64_t seek_frame = cue_seek_frame_.exchange(
                        no_transport_seek, std::memory_order_acq_rel);
                    seek_frame != no_transport_seek) {
                    static_cast<void>(player_.worker_seek(seek_frame));
                } else if (const uint64_t seek_frame = transport_seek_frame_.exchange(
                        no_transport_seek, std::memory_order_acq_rel);
                    seek_frame != no_transport_seek) {
                    static_cast<void>(player_.worker_seek(seek_frame));
                }
                break;
            }
            static_cast<void>(player_.worker_refill());
            if (!player_.worker_load_pending()) {
                std::lock_guard lock(request_mutex_);
                request_in_progress_ = false;
            }
        }
    }

protected:
    void handle_load(const Messages::LoadMedia& command, Messages::LoadMedia::Reply& reply) {
        reply = {};
        const std::string_view path{command.path.c_str(), command.path.size()};
        if (path.empty() || path.find('\0') != std::string_view::npos) {
            reply.rejection = Messages::MEDIA_LOAD_REJECTION_INVALID_PATH;
            return;
        }
        PendingRequest request{};
        request.type = RequestType::Load;
        request.path = command.path;
        request.protect_playing = this->params_.protect_playing_loads;
        {
            std::lock_guard lock(request_mutex_);
            if (pending_request_.type != RequestType::None || request_in_progress_
                || lifecycle_request_.type != RequestType::None) {
                reply.rejection = Messages::MEDIA_LOAD_REJECTION_BUSY;
                return;
            }
            if (load_request_counter_ == std::numeric_limits<uint64_t>::max()) {
                reply.rejection = Messages::MEDIA_LOAD_REJECTION_ID_EXHAUSTED;
                return;
            }
            request.load_request_id = ++load_request_counter_;
            pending_request_ = request;
            reply.accepted = true;
            reply.request_id = request.load_request_id;
        }
        request_ready_.notify_one();
    }

    void handle_seek(const Messages::SeekMedia& command, Messages::SeekMedia::Reply& reply) {
        PendingRequest request{};
        request.type = RequestType::Seek;
        request.media_frame = command.media_frame;
        reply.accepted = enqueue(request);
    }

    void handle_unload(
        const Messages::UnloadMedia&,
        Messages::UnloadMedia::Reply& reply) {
        PendingRequest request{};
        request.type = RequestType::Unload;
        reply.accepted = enqueue(request);
    }

private:
    musicrat::backends::media::PreparedPlaybackCoordinator<
        musicrat::backends::media::MediaDecoder,
        decode_chunk_count> player_{};
    std::jthread worker_{};
    std::mutex request_mutex_{};
    std::condition_variable request_ready_{};
    PendingRequest pending_request_{};
    PendingRequest lifecycle_request_{};
    uint64_t load_request_counter_{0};
    bool request_in_progress_{false};
    std::optional<musicrat::dsp::TransportFollower> transport_follower_{};
    std::optional<musicrat::dsp::BeatGridMapper> tempo_grid_{};
    musicrat::backends::media::TempoGridAnalysisWorker tempo_analysis_{};
    std::atomic<uint64_t> startup_grid_track_{0};
    std::atomic<uint64_t> analyzed_track_{0};
    std::atomic<uint32_t> analyzed_sample_rate_hz_{0};
    std::atomic<double> analyzed_tempo_bpm_{0};
    std::atomic<uint16_t> analyzed_confidence_per_mille_{0};
    musicrat::dsp::DeckControlQuantizer control_quantizer_{};
    Messages::DeckControlEventBlock quantized_controls_{};
    static constexpr uint64_t no_transport_seek =
        std::numeric_limits<uint64_t>::max();
    std::atomic<uint64_t> transport_seek_frame_{no_transport_seek};
    std::atomic<uint64_t> cue_seek_frame_{no_transport_seek};
    uint32_t frames_per_period_;
    uint64_t sequence_number_{0};
    uint64_t transport_sequence_number_{0};
    uint64_t controls_generation_{0};
    uint64_t quantized_actions_dropped_{0};
    uint64_t deck_control_blocks_received_{0};
    uint64_t transport_blocks_received_{0};
    double transport_beat_position_{0.0};
    double sync_phase_error_beats_{0.0};
    uint32_t quantized_actions_pending_{0};
    bool transport_seen_{false};
};

} // namespace CommRaT