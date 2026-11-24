/* Exercise the production PCM callback without a device or player subprocess. */
#include "../native-playback/client.c"
#include <assert.h>

static float configured_bgm;
static cpymo_engine engine;

float cpymo_audio_get_channel_volume(size_t channel, const cpymo_audio_system *system)
{
    assert(channel == CPYMO_AUDIO_CHANNEL_BGM && system == &engine.audio);
    return configured_bgm;
}

static void check_volume(float volume, const int16_t expected[6])
{
    static const int16_t pcm[6] = {INT16_MIN, INT16_MAX, -12345, 12345, 0, -1};
    static struct native_playback shared;
    memset(&shared, 0, sizeof(shared));
    struct client client = {.shared = &shared};
    configured_bgm = volume;
    client.bgm_gain = native_movie_bgm_gain(&engine);
    /* The input crosses the end of the ring. */
    memcpy(shared.audio + (NATIVE_AUDIO_SAMPLES - 1) * 2, pcm, 4);
    memcpy(shared.audio, pcm + 2, 8);
    shared.audio_read = NATIVE_AUDIO_SAMPLES - 1;
    shared.audio_write = NATIVE_AUDIO_SAMPLES + 2;
    shared.audio_done = 1;
    shared.video_write = 1;
    int16_t buffer[6] = {1, 2, 3, 4, 5, 6};
    active = &client;
    assert(garlic_native_movie_audio(buffer, 3));
    assert(!memcmp(buffer, expected, sizeof(buffer)));
    assert(shared.audio_read == shared.audio_write);
    assert(native_load(&client.present.audio_clock) == 3);
    assert(!client.present.underrun_frames);
    shared.stop = 1;
    assert(garlic_native_movie_audio(buffer, 3));
    for (size_t i = 0; i < 6; i++) assert(buffer[i] == 0);
    assert(native_load(&client.present.audio_clock) == 3);
    active = NULL;
    assert(!garlic_native_movie_audio(buffer, 3));
}

int main(void)
{
    static const int16_t silent[6] = {0};
    static const int16_t half[6] = {-16384, 16383, -6172, 6172, 0, 0};
    static const int16_t full[6] = {INT16_MIN, INT16_MAX, -12345, 12345, 0, -1};
    check_volume(0.0f, silent);
    check_volume(0.5f, half);
    check_volume(1.0f, full);
    check_volume(-1.0f, silent);
    check_volume(2.0f, full);
    puts("Native MP4 volume, ring drain, audio clock and stop: PASS");
}
