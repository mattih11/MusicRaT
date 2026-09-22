#include <musicrat/modules/wav_sink.hpp>

#include <rfl/json.hpp>

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

bool keep_test_artifacts() noexcept {
    return std::getenv("MUSICRAT_KEEP_TEST_ARTIFACTS") != nullptr;
}

class TestWavSink : public CommRaT::WavSink {
public:
    using CommRaT::WavSink::WavSink;
    using CommRaT::WavSink::on_disable;
    using CommRaT::WavSink::on_enable;
    using CommRaT::WavSink::process;
};

} // namespace

int main() {
    const auto path = std::filesystem::temp_directory_path()
        / "musicrat_wav_sink_test.wav";
    std::filesystem::remove(path);

    commrat::ModuleConfig config{};
    config.name = "WavSinkTest";
    config.outputs = commrat::NoOutputConfig{.system_id = 32, .instance_id = 1};
    config.inputs = commrat::SingleInputConfig{
        .source_system_id = 10,
        .source_instance_id = 1,
        .source_lifecycle_address = 0,
    };
    config.params = rfl::json::read<rfl::Generic>(
        "{\"path\":\"" + path.string()
        + "\",\"sample_rate_hz\":48000,\"channel_count\":1}").value();

    TestWavSink sink{config};
    assert(sink.on_enable() == commrat::LifecycleResult::Success);

    CommRaT::Messages::AudioBlock block{};
    block.sample_rate_hz = 48000.0;
    block.frame_count = 2;
    block.channel_count = 1;
    block.channels[0].push_back(-1.0);
    block.channels[0].push_back(1.0);
    sink.process(block);

    block.channel_count = 2;
    block.channels[1].push_back(0.0);
    block.channels[1].push_back(0.0);
    sink.process(block);
    sink.on_disable();

    assert(sink.blocks_written() == 1);
    assert(sink.blocks_rejected() == 1);

    std::ifstream stream(path, std::ios::binary);
    const std::vector<unsigned char> bytes{
        std::istreambuf_iterator<char>{stream},
        std::istreambuf_iterator<char>{}};
    assert(bytes.size() == 48);
    assert(bytes[4] == 40);
    assert(bytes[40] == 4);
    assert(bytes[44] == 0x00 && bytes[45] == 0x80);
    assert(bytes[46] == 0xFF && bytes[47] == 0x7F);

    if (!keep_test_artifacts()) {
        std::filesystem::remove(path);
    }
}