#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "audio.h"

// mmsystem.h is excluded by WIN32_LEAN_AND_MEAN; the one function needed is
// declared by hand instead, matching this project's style of avoiding extra
// system headers.
__declspec(dllimport) int __stdcall PlaySoundA(const void* sound, HMODULE module, DWORD flags);
#define SND_ASYNC 1
#define SND_NODEFAULT 2
#define SND_MEMORY 4
#define SND_LOOP 8

#define SAMPLE_RATE 22050
#define LOOP_SECONDS 10
#define LOOP_SAMPLES (SAMPLE_RATE * LOOP_SECONDS)
// Blended into the loop start so the seam is inaudible; see GenerateOcean.
#define CROSSFADE_SAMPLES (SAMPLE_RATE / 6)
#define TOTAL_SAMPLES (LOOP_SAMPLES + CROSSFADE_SAMPLES)

// Explicit, fully reproducible seed - never derived from the clock, so the
// ocean texture is identical on every run. Change it to hear a different one.
#define AUDIO_SEED 20260825u
// Time between fresh swell targets; the envelope eases toward a new one over
// this many samples rather than jumping, so the amplitude rise and fall
// itself takes a couple of seconds, like a real wave set.
#define SWELL_SAMPLES (SAMPLE_RATE * 2)

#pragma pack(push, 1)
typedef struct {
    char riff_id[4]; unsigned riff_size; char wave_id[4];
    char fmt_id[4]; unsigned fmt_size; unsigned short format; unsigned short channels;
    unsigned sample_rate; unsigned byte_rate; unsigned short block_align; unsigned short bits_per_sample;
    char data_id[4]; unsigned data_size;
} WavHeader;
#pragma pack(pop)

static unsigned int rng_state;

static unsigned int NextRandom(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

// Signed range, not unsigned: a direct float-to-unsigned cast pulls in a CRT
// helper that /NODEFAULTLIB has no library for, while float-to-int does not.
static float NextNoise(void) {
    return (float)(int)NextRandom() * (1.f / 2147483648.f);
}

static void PutTag(char* destination, const char* text) {
    int i;
    for (i = 0; i < 4; i++) destination[i] = text[i];
}

// Synthesizes a seamless, loopable ocean ambience. The amplitude envelope
// eases between random waypoints a couple of seconds apart, so swells rise
// and fall with real amplitude rather than staying near-constant - a single
// leaky-integrator filter was tried first, but a coefficient slow enough to
// change over seconds also crushed its variance to almost nothing, since for
// that filter bandwidth and amplitude shrink together. The envelope shapes a
// low-passed "surf" body, with a high-passed "foam" layer that only appears
// near the swell peaks. The last CROSSFADE_SAMPLES of a longer run are
// blended into the loop start so the wrap has no audible click.
static void* GenerateOcean(void) {
    void* block = VirtualAlloc(0, sizeof(WavHeader) + LOOP_SAMPLES * 2, MEM_COMMIT, PAGE_READWRITE);
    float* raw = (float*)VirtualAlloc(0, TOTAL_SAMPLES * sizeof(float), MEM_COMMIT, PAGE_READWRITE);
    WavHeader* header; short* pcm; int i, swell_counter = SWELL_SAMPLES;
    float surf = 0.f, previous_white = 0.f, swell_from = 0.f, swell_to = 0.f;
    if (!block || !raw) return 0;

    rng_state = AUDIO_SEED;
    for (i = 0; i < TOTAL_SAMPLES; i++) {
        float white = NextNoise();
        float white2 = NextNoise();
        float fraction, eased, shaped, foam, sample;
        surf += (white - surf) * .05f;
        if (swell_counter >= SWELL_SAMPLES) { swell_counter = 0; swell_from = swell_to; swell_to = NextNoise() * .5f + .5f; }
        fraction = (float)swell_counter / (float)SWELL_SAMPLES;
        eased = fraction * fraction * (3.f - 2.f * fraction);
        shaped = swell_from + (swell_to - swell_from) * eased;
        swell_counter++;
        foam = white2 - previous_white;
        previous_white = white2;
        sample = surf * (.25f + .7f * shaped) + foam * shaped * shaped * .5f;
        // Soft-knee saturation instead of a hard clamp: maps all of R into
        // (-1,1) smoothly, so loud swells round off instead of clipping.
        raw[i] = sample / (1.f + (sample < 0.f ? -sample : sample));
    }
    for (i = 0; i < CROSSFADE_SAMPLES; i++) {
        float mix = (float)i / (float)CROSSFADE_SAMPLES;
        raw[i] = raw[i] * (1.f - mix) + raw[LOOP_SAMPLES + i] * mix;
    }

    header = (WavHeader*)block;
    PutTag(header->riff_id, "RIFF"); PutTag(header->wave_id, "WAVE"); PutTag(header->fmt_id, "fmt ");
    header->riff_size = 36 + LOOP_SAMPLES * 2;
    header->fmt_size = 16; header->format = 1; header->channels = 1;
    header->sample_rate = SAMPLE_RATE; header->byte_rate = SAMPLE_RATE * 2;
    header->block_align = 2; header->bits_per_sample = 16;
    PutTag(header->data_id, "data"); header->data_size = LOOP_SAMPLES * 2;

    pcm = (short*)((char*)block + sizeof(WavHeader));
    for (i = 0; i < LOOP_SAMPLES; i++) pcm[i] = (short)(int)(raw[i] * 30000.f);

    VirtualFree(raw, 0, MEM_RELEASE);
    return block;
}

void AudioStart(void) {
    void* wav = GenerateOcean();
    if (!wav) return;
#ifdef DEBUG
    // Written so the generated sound can be reviewed outside the running
    // intro; not part of the Compo path.
    {
        DWORD written; unsigned size = sizeof(WavHeader) + LOOP_SAMPLES * 2;
        HANDLE file = CreateFileA("ocean.wav", GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
        if (file != INVALID_HANDLE_VALUE) { WriteFile(file, wav, size, &written, 0); CloseHandle(file); }
    }
#endif
    PlaySoundA(wav, 0, SND_ASYNC | SND_LOOP | SND_MEMORY | SND_NODEFAULT);
}

void AudioStop(void) {
    PlaySoundA(0, 0, 0);
}
