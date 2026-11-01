#include <musicrat/protocol/file_player.hpp>
#include <musicrat/protocol/playback_status.hpp>

#include <sertial/message.hpp>

#include <cassert>

int main() {
    using namespace CommRaT::Messages;
    const LoadMedia command{.path = "/media/test.wav"};
    const auto encoded = sertial::Message<LoadMedia>::serialize(command);
    const auto restored = sertial::Message<LoadMedia>::deserialize(encoded.view());
    assert(restored && restored->path == command.path);
    const LoadMedia::Reply reply{
        .accepted = true, .request_id = 123, .rejection = MEDIA_LOAD_REJECTION_NONE};
    const auto encoded_reply = sertial::Message<LoadMedia::Reply>::serialize(reply);
    const auto restored_reply = sertial::Message<LoadMedia::Reply>::deserialize(encoded_reply.view());
    assert(restored_reply && restored_reply->accepted && restored_reply->request_id == 123);
    for (auto rejection : {MEDIA_LOAD_REJECTION_INVALID_PATH, MEDIA_LOAD_REJECTION_BUSY,
            MEDIA_LOAD_REJECTION_ID_EXHAUSTED}) {
        const auto bytes = sertial::Message<LoadMedia::Reply>::serialize({
            .accepted = false, .request_id = 0, .rejection = rejection});
        const auto rejected = sertial::Message<LoadMedia::Reply>::deserialize(bytes.view());
        assert(rejected && !rejected->accepted && rejected->rejection == rejection);
    }
    for (auto state : {MEDIA_LOAD_IDLE, MEDIA_LOAD_PREPARING, MEDIA_LOAD_PREPARED,
            MEDIA_LOAD_COMMITTED, MEDIA_LOAD_FAILED, MEDIA_LOAD_PROTECTED, MEDIA_LOAD_CANCELLED}) {
        PlaybackStatusBlock status{};
        status.track_instance_id = 122;
        status.load_request_id = 123;
        status.load_state = state;
        status.load_decode_error = PLAYBACK_DECODE_ERROR_CORRUPT_DATA;
        const auto bytes = sertial::Message<PlaybackStatusBlock>::serialize(status);
        const auto decoded = sertial::Message<PlaybackStatusBlock>::deserialize(bytes.view());
        assert(decoded && decoded->track_instance_id == 122 && decoded->load_request_id == 123);
        assert(decoded->load_state == state && decoded->load_decode_error == status.load_decode_error);
    }
    assert(CommRaT::Parameters::FilePlayer{}.protect_playing_loads);
}
