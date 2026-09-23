#pragma once

#include <musicrat/backends/media/media_decoder.hpp>
#include <musicrat/backends/media/playback_coordinator.hpp>
#include <musicrat/launcher/audio_format_validation.hpp>
#include <musicrat/dsp/beat_grid.hpp>
#include <musicrat/dsp/deck_control_quantizer.hpp>
#include <musicrat/dsp/transport_follower.hpp>
#include <musicrat/musicrat.hpp>
#include <musicrat/protocol/file_player.hpp>

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
        player_.worker_close();
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
        };
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
            if (pending_request_.type != RequestType::None) {
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
            pending_request_ = request;
        }
        request_ready_.notify_one();
    }

    void worker_loop(std::stop_token stop_token) noexcept {
        while (!stop_token.stop_requested()) {
            PendingRequest request{};
            {
                std::unique_lock lock(request_mutex_);
                request_ready_.wait_for(
                    lock,
                    std::chrono::milliseconds{2},
                    [&] {
                        return stop_token.stop_requested()
                            || pending_request_.type != RequestType::None
                            || transport_seek_frame_.load(
                                std::memory_order_acquire) != no_transport_seek;
                    });
                if (stop_token.stop_requested()) {
                    break;
                }
                request = pending_request_;
                pending_request_ = {};
            }

            switch (request.type) {
            case RequestType::Load:
                transport_seek_frame_.store(
                    no_transport_seek, std::memory_order_release);
                static_cast<void>(player_.worker_open(request.path.c_str()));
                break;
            case RequestType::Seek:
                transport_seek_frame_.store(
                    no_transport_seek, std::memory_order_release);
                static_cast<void>(player_.worker_seek(request.media_frame));
                break;
            case RequestType::Unload:
                transport_seek_frame_.store(
                    no_transport_seek, std::memory_order_release);
                player_.worker_close();
                break;
            case RequestType::None:
                if (const uint64_t seek_frame = transport_seek_frame_.exchange(
                        no_transport_seek, std::memory_order_acq_rel);
                    seek_frame != no_transport_seek) {
                    static_cast<void>(player_.worker_seek(seek_frame));
                }
                break;
            }
            static_cast<void>(player_.worker_refill());
        }
    }

    void handle_load(const Messages::LoadMedia& command, Messages::LoadMedia::Reply& reply) {
        PendingRequest request{};
        request.type = RequestType::Load;
        request.path = command.path;
        reply.accepted = !command.path.empty() && enqueue(request);
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

    musicrat::backends::media::PlaybackCoordinator<
        musicrat::backends::media::MediaDecoder,
        decode_chunk_count> player_{};
    std::jthread worker_{};
    std::mutex request_mutex_{};
    std::condition_variable request_ready_{};
    PendingRequest pending_request_{};
    std::optional<musicrat::dsp::TransportFollower> transport_follower_{};
    musicrat::dsp::DeckControlQuantizer control_quantizer_{};
    Messages::DeckControlEventBlock quantized_controls_{};
    static constexpr uint64_t no_transport_seek =
        std::numeric_limits<uint64_t>::max();
    std::atomic<uint64_t> transport_seek_frame_{no_transport_seek};
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