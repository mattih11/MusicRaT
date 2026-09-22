#include <musicrat/backends/media/decoded_audio_pool.hpp>

#include <cassert>

namespace {

using Pool = musicrat::backends::media::DecodedAudioPool<3>;

void fill(Pool::WriteReservation reservation, double first_sample) {
    assert(reservation);
    reservation.audio->sample_rate_hz = 48000.0;
    reservation.audio->frame_count = 2;
    reservation.audio->channel_count = 1;
    reservation.audio->channels[0].push_back(first_sample);
    reservation.audio->channels[0].push_back(first_sample + 1.0);
}

} // namespace

int main() {
    Pool pool{};

    auto first = pool.try_begin_write();
    fill(first, 1.0);
    assert(pool.publish(first, 100, 1));

    auto second = pool.try_begin_write();
    fill(second, 3.0);
    assert(pool.publish(second, 102, 1));
    assert(pool.ready_count() == 2);
    assert(pool.resident_frames() == 4);
    auto range = pool.buffered_range(1);
    assert(range.start_frame == 100);
    assert(range.end_frame == 104);

    assert(!pool.try_acquire(99.0, 1));
    assert(!pool.try_acquire(100.0, 2));

    auto read = pool.try_acquire(101.5, 1);
    assert(read);
    assert(read.chunk.start_frame == 100);
    assert(read.chunk.generation == 1);
    assert(read.chunk.audio->channels[0][0] == 1.0);
    assert(pool.ready_count() == 1);
    assert(pool.resident_frames() == 4);
    range = pool.buffered_range(1);
    assert(range.start_frame == 100);
    assert(range.end_frame == 104);
    assert(!pool.try_acquire(101.5, 1));

    auto third = pool.try_begin_write();
    fill(third, 5.0);
    assert(pool.publish(third, 200, 2));
    assert(!pool.try_begin_write());
    assert(pool.resident_frames() == 6);
    range = pool.buffered_range(2);
    assert(range.start_frame == 200);
    assert(range.end_frame == 202);

    pool.release(read);
    assert(pool.resident_frames() == 4);
    auto spare = pool.try_begin_write();
    assert(spare);
    pool.cancel(spare);

    assert(pool.discard_before_generation(2) == 1);
    assert(pool.resident_frames() == 2);
    range = pool.buffered_range(1);
    assert(range.start_frame == 0);
    assert(range.end_frame == 0);
    assert(!pool.try_acquire(102.0, 1));
    auto current = pool.try_acquire(200.0, 2);
    assert(current);
    pool.release(current);
    assert(pool.resident_frames() == 0);

    auto invalid = pool.try_begin_write();
    assert(invalid);
    invalid.audio->sample_rate_hz = 0.0;
    assert(!pool.publish(invalid, 300, 2));
    assert(pool.resident_frames() == 0);
}