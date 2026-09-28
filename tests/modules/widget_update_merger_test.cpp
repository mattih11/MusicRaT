#include <musicrat/modules/widget_update_merger.hpp>

#include <cassert>
#include <memory>

namespace {

class TestWidgetUpdateMerger : public CommRaT::WidgetUpdateMerger {
public:
    using CommRaT::WidgetUpdateMerger::WidgetUpdateMerger;
    using CommRaT::WidgetUpdateMerger::process;
};

} // namespace

int main() {
    commrat::ModuleConfig config{};
    config.name = "WidgetUpdateMergerTest";
    config.outputs = commrat::MultiOutputConfig{.addresses = {{
        .system_id = 50,
        .instance_id = 1,
    }}};
    config.inputs = commrat::MultiInputConfig{
        .sources = {
            {
                .system_id = 40,
                .instance_id = 1,
                .lifecycle_address = 0,
                .is_primary = true,
            },
            {
                .system_id = 40,
                .instance_id = 2,
                .lifecycle_address = 0,
                .is_primary = false,
            },
        },
    };

    auto merger = std::make_unique<TestWidgetUpdateMerger>(config);
    CommRaT::Messages::WidgetUpdateBlock primary{};
    primary.events.push_back({.binding_id = 1});
    commrat::Synced<CommRaT::Messages::WidgetUpdateBlock> secondary{};
    CommRaT::Messages::WidgetUpdateBlock output{};
    merger->process(primary, secondary, output);
    assert(output.events.size() == 1);
    assert(output.events[0].binding_id == 1);
}