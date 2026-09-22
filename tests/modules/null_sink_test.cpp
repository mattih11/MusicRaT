#include <musicrat/modules/null_sink.hpp>

#include <cassert>

namespace {

class TestNullSink : public CommRaT::NullSink {
public:
    using CommRaT::NullSink::NullSink;
    using CommRaT::NullSink::process;
};

} // namespace

int main() {
    commrat::ModuleConfig config{};
    config.name = "NullSinkTest";
    config.outputs = commrat::NoOutputConfig{.system_id = 31, .instance_id = 1};
    config.inputs = commrat::SingleInputConfig{
        .source_system_id = 10,
        .source_instance_id = 1,
        .source_lifecycle_address = 0,
    };

    TestNullSink sink{config};
    CommRaT::Messages::AudioBlock block{};
    block.sample_rate_hz = 48000.0;
    block.frame_count = 1;
    block.channel_count = 1;
    block.channels[0].push_back(0.0);

    sink.process(block);
    assert(sink.blocks_received() == 1);
    assert(sink.invalid_blocks() == 0);

    block.channels[1].push_back(0.0);
    sink.process(block);
    assert(sink.blocks_received() == 2);
    assert(sink.invalid_blocks() == 1);
}