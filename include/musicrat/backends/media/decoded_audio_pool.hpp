#pragma once

#include <musicrat/dsp/varispeed_renderer.hpp>
#include <musicrat/utility/audio_block.hpp>

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace musicrat::backends::media {

struct BufferedFrameRange {
    uint64_t start_frame{0};
    uint64_t end_frame{0};
};

template<std::size_t ChunkCount>
class DecodedAudioPool {
    static_assert(ChunkCount > 1);
    static_assert(std::atomic<uint64_t>::is_always_lock_free);

public:
    using AudioBlock = CommRaT::Messages::AudioBlock;
    static constexpr std::size_t invalid_index = std::numeric_limits<std::size_t>::max();

    struct WriteReservation {
        std::size_t index{invalid_index};
        AudioBlock* audio{nullptr};

        [[nodiscard]] explicit operator bool() const noexcept {
            return audio != nullptr;
        }
    };

    struct ReadReservation {
        std::size_t index{invalid_index};
        musicrat::dsp::DecodedAudioChunkView chunk{};

        [[nodiscard]] explicit operator bool() const noexcept {
            return chunk.audio != nullptr;
        }
    };

    [[nodiscard]] WriteReservation try_begin_write() noexcept {
        for (std::size_t offset = 0; offset < ChunkCount; ++offset) {
            const std::size_t index = (next_write_index_ + offset) % ChunkCount;
            SlotState expected = SlotState::Free;
            if (!slots_[index].state.compare_exchange_strong(
                    expected,
                    SlotState::Writing,
                    std::memory_order_acquire,
                    std::memory_order_relaxed)) {
                continue;
            }

            clear_audio_channels(slots_[index].audio);
            slots_[index].audio = {};
            next_write_index_ = (index + 1) % ChunkCount;
            return {.index = index, .audio = &slots_[index].audio};
        }
        return {};
    }

    [[nodiscard]] bool publish(
        WriteReservation reservation,
        uint64_t start_frame,
        uint64_t generation) noexcept {
        if (!valid_reservation(reservation)
            || validate_audio_block(*reservation.audio) != AudioBlockValidationError::None) {
            cancel(reservation);
            return false;
        }

        auto& slot = slots_[reservation.index];
        slot.start_frame.store(start_frame, std::memory_order_relaxed);
        slot.generation.store(generation, std::memory_order_relaxed);
        slot.frame_count.store(reservation.audio->frame_count, std::memory_order_relaxed);
        resident_frames_.fetch_add(
            reservation.audio->frame_count, std::memory_order_relaxed);
        slot.state.store(SlotState::Ready, std::memory_order_release);
        return true;
    }

    void cancel(WriteReservation reservation) noexcept {
        if (!valid_reservation(reservation)) {
            return;
        }
        auto& slot = slots_[reservation.index];
        SlotState expected = SlotState::Writing;
        (void)slot.state.compare_exchange_strong(
            expected,
            SlotState::Free,
            std::memory_order_release,
            std::memory_order_relaxed);
    }

    [[nodiscard]] ReadReservation try_acquire(
        double media_position,
        uint64_t generation) noexcept {
        if (!std::isfinite(media_position) || media_position < 0.0) {
            return {};
        }

        for (std::size_t index = 0; index < ChunkCount; ++index) {
            auto& slot = slots_[index];
            SlotState expected = SlotState::Ready;
            if (!slot.state.compare_exchange_strong(
                    expected,
                    SlotState::Reading,
                    std::memory_order_acquire,
                    std::memory_order_relaxed)) {
                continue;
            }
            if (slot.generation.load(std::memory_order_relaxed) != generation
                || !contains(slot, media_position)) {
                slot.state.store(SlotState::Ready, std::memory_order_release);
                continue;
            }
            return {
                .index = index,
                .chunk = {
                    .audio = &slot.audio,
                    .start_frame = slot.start_frame.load(std::memory_order_relaxed),
                    .generation = slot.generation.load(std::memory_order_relaxed),
                },
            };
        }
        return {};
    }

    [[nodiscard]] ReadReservation try_acquire_ready(
        uint64_t generation) noexcept {
        for (std::size_t index = 0; index < ChunkCount; ++index) {
            auto& slot = slots_[index];
            SlotState expected = SlotState::Ready;
            if (!slot.state.compare_exchange_strong(
                    expected,
                    SlotState::Reading,
                    std::memory_order_acquire,
                    std::memory_order_relaxed)) {
                continue;
            }
            if (slot.generation.load(std::memory_order_relaxed) != generation) {
                slot.state.store(SlotState::Ready, std::memory_order_release);
                continue;
            }
            return {
                .index = index,
                .chunk = {
                    .audio = &slot.audio,
                    .start_frame = slot.start_frame.load(std::memory_order_relaxed),
                    .generation = slot.generation.load(std::memory_order_relaxed),
                },
            };
        }
        return {};
    }

    void release(ReadReservation reservation) noexcept {
        if (reservation.index >= ChunkCount
            || reservation.chunk.audio != &slots_[reservation.index].audio) {
            return;
        }
        auto& slot = slots_[reservation.index];
        const uint32_t frame_count = slot.audio.frame_count;
        SlotState expected = SlotState::Reading;
        if (slot.state.compare_exchange_strong(
            expected,
            SlotState::Free,
            std::memory_order_release,
            std::memory_order_relaxed)) {
            resident_frames_.fetch_sub(frame_count, std::memory_order_relaxed);
        }
    }

    std::size_t discard_before_generation(uint64_t generation) noexcept {
        std::size_t discarded = 0;
        for (auto& slot : slots_) {
            SlotState expected = SlotState::Ready;
            if (!slot.state.compare_exchange_strong(
                    expected,
                    SlotState::Reading,
                    std::memory_order_acquire,
                    std::memory_order_relaxed)) {
                continue;
            }
            if (slot.generation.load(std::memory_order_relaxed) < generation) {
                const uint32_t frame_count = slot.audio.frame_count;
                slot.state.store(SlotState::Free, std::memory_order_release);
                resident_frames_.fetch_sub(frame_count, std::memory_order_relaxed);
                ++discarded;
            } else {
                slot.state.store(SlotState::Ready, std::memory_order_release);
            }
        }
        return discarded;
    }

    [[nodiscard]] std::size_t ready_count() const noexcept {
        std::size_t count = 0;
        for (const auto& slot : slots_) {
            if (slot.state.load(std::memory_order_acquire) == SlotState::Ready) {
                ++count;
            }
        }
        return count;
    }

    [[nodiscard]] uint64_t resident_frames() const noexcept {
        return resident_frames_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] BufferedFrameRange buffered_range(
        uint64_t generation) const noexcept {
        BufferedFrameRange range{};
        bool found = false;
        for (const auto& slot : slots_) {
            const SlotState before = slot.state.load(std::memory_order_acquire);
            if (before != SlotState::Ready && before != SlotState::Reading) {
                continue;
            }
            const uint64_t slot_generation = slot.generation.load(
                std::memory_order_relaxed);
            const uint64_t start = slot.start_frame.load(std::memory_order_relaxed);
            const uint64_t frame_count = slot.frame_count.load(
                std::memory_order_relaxed);
            const SlotState after = slot.state.load(std::memory_order_acquire);
            if (before != after || slot_generation != generation || frame_count == 0) {
                continue;
            }
            const uint64_t end = start > std::numeric_limits<uint64_t>::max() - frame_count
                ? std::numeric_limits<uint64_t>::max()
                : start + frame_count;
            if (!found || start < range.start_frame) {
                range.start_frame = start;
            }
            if (!found || end > range.end_frame) {
                range.end_frame = end;
            }
            found = true;
        }
        return range;
    }

private:
    enum class SlotState : uint8_t {
        Free,
        Writing,
        Ready,
        Reading,
    };

    struct Slot {
        AudioBlock audio{};
        std::atomic<uint64_t> start_frame{0};
        std::atomic<uint64_t> generation{0};
        std::atomic<uint64_t> frame_count{0};
        std::atomic<SlotState> state{SlotState::Free};
    };

    [[nodiscard]] bool valid_reservation(
        WriteReservation reservation) const noexcept {
        return reservation.index < ChunkCount
            && reservation.audio == &slots_[reservation.index].audio;
    }

    static bool contains(const Slot& slot, double media_position) noexcept {
        const double start = static_cast<double>(
            slot.start_frame.load(std::memory_order_relaxed));
        const double end = start + slot.audio.frame_count;
        return media_position >= start && media_position < end;
    }

    std::array<Slot, ChunkCount> slots_{};
    std::atomic<uint64_t> resident_frames_{0};
    std::size_t next_write_index_{0};
};

} // namespace musicrat::backends::media