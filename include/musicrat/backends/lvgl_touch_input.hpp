#pragma once

#include <lvgl.h>
#include <linux/input.h>

#include <array>
#include <cstddef>

namespace musicrat::backends {

class LvglTouchInput {
public:
    static constexpr std::size_t max_contacts = 16;

    LvglTouchInput() = default;
    ~LvglTouchInput();
    LvglTouchInput(const LvglTouchInput&) = delete;
    LvglTouchInput& operator=(const LvglTouchInput&) = delete;

    static LvglTouchInput* open(
        const char* path, lv_display_t* display, bool& multitouch);

    bool initialize(lv_display_t* display, std::size_t contacts,
                    input_absinfo x_axis, input_absinfo y_axis);
    void consume(const input_event& event);

private:
    struct Contact {
        lv_indev_t* pointer{nullptr};
        int tracking_id{-1};
        int reported_id{-1};
        int x{0};
        int y{0};
        lv_point_t point{};
        bool pressed{false};
    };

    static void read_contact(lv_indev_t* pointer, lv_indev_data_t* data);
    static void poll_timer(lv_timer_t* timer);
    void poll();
    void report();
    void release_all();

    std::array<Contact, max_contacts> contacts_{};
    std::size_t contact_count_{0};
    input_absinfo x_axis_{};
    input_absinfo y_axis_{};
    lv_display_t* display_{nullptr};
    lv_timer_t* timer_{nullptr};
    int fd_{-1};
    int slot_{0};
    bool dropping_{false};
};

} // namespace musicrat::backends
