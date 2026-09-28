#include <musicrat/modules/audio_device_sink.hpp>

#include <rfl/json.hpp>

#include <cassert>
#include <memory>
#include <string>

namespace {

class FakeAudioOutputBackend final
    : public musicrat::backends::audio::AudioOutputBackend {
public:
    [[nodiscard]] const char* name() const noexcept override {
        return "fake";
    }

    [[nodiscard]] bool open(
        const musicrat::backends::audio::AudioOutputConfig& config) noexcept override {
        opened = config.sample_rate_hz == 48000
            && config.channel_count == 2
            && config.period_frames == 128;
        return opened;
    }

    void close() noexcept override {
        opened = false;
    }

    [[nodiscard]] bool write(
        const CommRaT::Messages::AudioBlock&) noexcept override {
        ++writes;
        return accept_writes;
    }

    [[nodiscard]] uint64_t underrun_count() const noexcept override {
        return 3;
    }

    [[nodiscard]] uint64_t underrun_frame_count() const noexcept override {
        return 17;
    }

    [[nodiscard]] uint64_t stream_failure_count() const noexcept override {
        return 2;
    }

    bool opened{false};
    bool accept_writes{true};
    uint64_t writes{0};
};

class TestAudioDeviceSink : public CommRaT::AudioDeviceSink {
public:
    using CommRaT::AudioDeviceSink::AudioDeviceSink;
    using CommRaT::AudioDeviceSink::on_disable;
    using CommRaT::AudioDeviceSink::on_enable;
    using CommRaT::AudioDeviceSink::process;
};

} // namespace

int main() {
    commrat::ModuleConfig config{};
    config.name = "AudioDeviceSinkTest";
    config.outputs = commrat::NoOutputConfig{.system_id = 34, .instance_id = 1};
    config.inputs = commrat::SingleInputConfig{
        .source_system_id = 10,
        .source_instance_id = 1,
        .source_lifecycle_address = 0,
    };
    config.params = rfl::json::read<rfl::Generic>(
        R"({"device_name":"test","sample_rate_hz":48000,"channel_count":2,"period_frames":128})")
        .value();

    auto backend = std::make_unique<FakeAudioOutputBackend>();
    auto* backend_ptr = backend.get();
    TestAudioDeviceSink sink{config, std::move(backend)};
    assert(std::string{sink.backend_name()} == "fake");
    assert(sink.on_enable() == commrat::LifecycleResult::Success);
    assert(backend_ptr->opened);

    CommRaT::Messages::AudioBlock block{};
    block.sample_rate_hz = 48000.0;
    block.frame_count = 2;
    block.channel_count = 2;
    block.channels[0].push_back(-1.0F);
    block.channels[0].push_back(1.0F);
    block.channels[1].push_back(0.5F);
    block.channels[1].push_back(-0.5F);
    sink.process(block);
    assert(sink.blocks_queued() == 1);

    block.sample_rate_hz = 44100.0;
    sink.process(block);
    assert(sink.blocks_rejected() == 1);
    assert(backend_ptr->writes == 1);

    block.sample_rate_hz = 48000.0;
    backend_ptr->accept_writes = false;
    sink.process(block);
    assert(sink.blocks_rejected() == 2);
    assert(sink.underruns() == 3);
    assert(sink.underrun_frames() == 17);
    assert(sink.stream_failures() == 2);

    sink.on_disable();
    assert(!backend_ptr->opened);
}