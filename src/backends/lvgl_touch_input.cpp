#include <musicrat/backends/lvgl_touch_input.hpp>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdint>
#include <fcntl.h>
#include <memory>
#include <sys/ioctl.h>
#include <unistd.h>

namespace musicrat::backends {

LvglTouchInput::~LvglTouchInput() {
    if (timer_ != nullptr) lv_timer_delete(timer_);
    release_all();
    for (auto& contact : contacts_) {
        if (contact.pointer != nullptr) lv_indev_delete(contact.pointer);
    }
    if (fd_ >= 0) close(fd_);
}

LvglTouchInput* LvglTouchInput::open(
    const char* path, lv_display_t* display, bool& multitouch) {
    multitouch = true;
    const int fd = ::open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        std::perror("MusicRaT touch input open");
        return nullptr;
    }
    input_absinfo slots{}, x{}, y{};
    if (ioctl(fd, EVIOCGABS(ABS_MT_SLOT), &slots) < 0) {
        const auto error = errno;
        close(fd);
        if (error == EINVAL || error == ENOTTY) {
            multitouch = false;
        } else {
            errno = error;
            std::perror("MusicRaT touch slot query");
        }
        return nullptr;
    }
    if (slots.minimum != 0 || slots.maximum < 0
        || slots.maximum >= static_cast<int>(max_contacts)) {
        std::fprintf(stderr, "MusicRaT touch input: unsupported slot range %d..%d\n",
                     slots.minimum, slots.maximum);
        close(fd);
        return nullptr;
    }
    if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &x) < 0
        || ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &y) < 0) {
        std::perror("MusicRaT touch axis query");
        close(fd);
        return nullptr;
    }
    auto input = std::make_unique<LvglTouchInput>();
    input->fd_ = fd;
    if (!input->initialize(display, static_cast<std::size_t>(slots.maximum) + 1, x, y)) {
        return nullptr;
    }
    input->slot_ = slots.value;
    input->timer_ = lv_timer_create(poll_timer, 5, input.get());
    if (input->timer_ == nullptr) {
        std::fprintf(stderr, "MusicRaT touch input: cannot create poll timer\n");
        return nullptr;
    }
    std::fprintf(stderr, "MusicRaT touch input: %zu independent contacts\n",
                 input->contact_count_);
    return input.release();
}

bool LvglTouchInput::initialize(
    lv_display_t* display, std::size_t contacts,
    input_absinfo x_axis, input_absinfo y_axis) {
    if (display_ != nullptr || display == nullptr || contacts == 0
        || contacts > max_contacts || x_axis.maximum <= x_axis.minimum
        || y_axis.maximum <= y_axis.minimum) {
        std::fprintf(stderr, "MusicRaT touch input: invalid contact configuration\n");
        return false;
    }
    display_ = display;
    contact_count_ = contacts;
    x_axis_ = x_axis;
    y_axis_ = y_axis;
    for (std::size_t index = 0; index < contacts; ++index) {
        auto& contact = contacts_[index];
        contact.pointer = lv_indev_create();
        if (contact.pointer == nullptr) {
            std::fprintf(stderr, "MusicRaT touch input: cannot create contact pointer\n");
            return false;
        }
        lv_indev_set_type(contact.pointer, LV_INDEV_TYPE_POINTER);
        lv_indev_set_display(contact.pointer, display);
        lv_indev_set_driver_data(contact.pointer, &contact);
        lv_indev_set_read_cb(contact.pointer, read_contact);
        lv_indev_set_mode(contact.pointer, LV_INDEV_MODE_EVENT);
    }
    return true;
}

void LvglTouchInput::read_contact(lv_indev_t* pointer, lv_indev_data_t* data) {
    const auto& contact = *static_cast<Contact*>(lv_indev_get_driver_data(pointer));
    data->point = contact.point;
    data->state = contact.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void LvglTouchInput::release_all() {
    for (auto& contact : contacts_) {
        if (contact.pointer != nullptr && contact.pressed) {
            contact.pressed = false;
            lv_indev_read(contact.pointer);
        }
        contact.tracking_id = -1;
        contact.reported_id = -1;
    }
}

void LvglTouchInput::consume(const input_event& event) {
    if (event.type == EV_SYN && event.code == SYN_DROPPED) {
        std::fprintf(stderr, "MusicRaT touch input: event loss; cancelling held contacts\n");
        release_all();
        dropping_ = true;
        return;
    }
    if (dropping_) {
        if (event.type == EV_SYN && event.code == SYN_REPORT) dropping_ = false;
        return;
    }
    if (event.type == EV_SYN && event.code == SYN_REPORT) {
        report();
    } else if (event.type == EV_ABS && event.code == ABS_MT_SLOT) {
        slot_ = event.value;
    } else if (event.type == EV_ABS && slot_ >= 0
               && slot_ < static_cast<int>(contact_count_)) {
        auto& contact = contacts_[static_cast<std::size_t>(slot_)];
        if (event.code == ABS_MT_TRACKING_ID) contact.tracking_id = event.value;
        else if (event.code == ABS_MT_POSITION_X) contact.x = event.value;
        else if (event.code == ABS_MT_POSITION_Y) contact.y = event.value;
    }
}

void LvglTouchInput::report() {
    const auto scale = [](int value, input_absinfo axis, int32_t size) {
        const auto offset = static_cast<int64_t>(
            std::clamp(value, axis.minimum, axis.maximum)) - axis.minimum;
        return static_cast<int32_t>(
            offset * (size - 1) / (static_cast<int64_t>(axis.maximum) - axis.minimum));
    };
    for (std::size_t index = 0; index < contact_count_; ++index) {
        auto& contact = contacts_[index];
        if (contact.pressed && contact.reported_id != contact.tracking_id) {
            contact.pressed = false;
            lv_indev_read(contact.pointer);
        }
        contact.point = {
            scale(contact.x, x_axis_, lv_display_get_horizontal_resolution(display_)),
            scale(contact.y, y_axis_, lv_display_get_vertical_resolution(display_)),
        };
        contact.pressed = contact.tracking_id >= 0;
        contact.reported_id = contact.tracking_id;
        lv_indev_read(contact.pointer);
    }
}

void LvglTouchInput::poll_timer(lv_timer_t* timer) {
    static_cast<LvglTouchInput*>(lv_timer_get_user_data(timer))->poll();
}

void LvglTouchInput::poll() {
    for (std::size_t index = 0; index < 256; ++index) {
        input_event event{};
        const auto bytes = read(fd_, &event, sizeof(event));
        if (bytes == sizeof(event)) {
            consume(event);
        } else if (bytes < 0 && errno == EINTR) {
            continue;
        } else if (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return;
        } else {
            if (bytes < 0) std::perror("MusicRaT touch input read");
            else std::fprintf(stderr, "MusicRaT touch input: incomplete event or device closed\n");
            release_all();
            lv_timer_pause(timer_);
            return;
        }
    }
}

} // namespace musicrat::backends
