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
// Long enough to give the sparse, 9-17-second-spaced creaks (see CREAK_MIN/
// MAX_SAMPLES) real room to land away from both loop edges, and to make the
// loop itself less repetitive.
#define LOOP_SECONDS 20
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
// Slower than SWELL_SAMPLES: eases the foam/hiss amount between waypoints, so
// the brightness of the ambience itself drifts over several seconds instead
// of staying at one fixed mix.
#define TEXTURE_SAMPLES (SAMPLE_RATE * 6)

// A resonant two-pole filter tuned once, offline, to ring at ~130 Hz when
// driven - a low creak or moan, not a pitched note. b1 and b2 are
// 2*r*cos(2*pi*130/SAMPLE_RATE) and r*r for r = .996 (lower and shorter-ringing
// than an earlier r = .9998/180 Hz tuning, which decayed as a long, near-pure
// sine and read as an electronic "bib" beep rather than a creak); computed by
// hand rather than calling cos() at runtime, since this project has no CRT
// and so no library sin/cos on the C side (only GLSL has that).
#define CREAK_B1 1.99063f
#define CREAK_B2 .992016f
// Random interval between creaks, so they do not fall in a mechanical pattern.
#define CREAK_MIN_SAMPLES (SAMPLE_RATE * 9)
#define CREAK_MAX_SAMPLES (SAMPLE_RATE * 17)
// A single-sample impulse rang the resonator as a clean decaying sine - the
// "bib" the excitation is instead spread over this many samples (~125 ms) as
// an envelope-shaped noise burst, so the resonator is driven like a groan
// instead of struck like a bell.
#define CREAK_BURST_SAMPLES (SAMPLE_RATE / 8)

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

// Eases linearly between two waypoints spaced `period` samples apart,
// smoothstepped. `counter`, `from`, and `to` are the caller's persistent state.
static float Waypoint(int* counter, float* from, float* to, int period) {
    float fraction, eased;
    if (*counter >= period) { *counter = 0; *from = *to; *to = NextNoise() * .5f + .5f; }
    fraction = (float)*counter / (float)period;
    eased = fraction * fraction * (3.f - 2.f * fraction);
    (*counter)++;
    return *from + (*to - *from) * eased;
}

// Synthesizes a seamless, loopable ocean ambience. The amplitude envelope
// eases between random waypoints a couple of seconds apart, so swells rise
// and fall with real amplitude rather than staying near-constant - a single
// leaky-integrator filter was tried first, but a coefficient slow enough to
// change over seconds also crushed its variance to almost nothing, since for
// that filter bandwidth and amplitude shrink together. The envelope shapes a
// two-stage low-passed "surf" body (one stage alone still let through enough
// high end to sound bright and harsh - this project's first pass at the whole
// ambience had the same complaint, from a "foam" layer that was raw
// differenced noise, i.e. almost pure treble). Foam is now the differenced
// noise smoothed by its own low-pass and mixed in at an amount that itself
// drifts slowly between quieter and hissier waypoints, so the ambience's
// brightness varies over time rather than sitting at one fixed mix. A sparse,
// randomly-timed low resonant "creak" - a boat hull, or something living out
// there - rings underneath. The last CROSSFADE_SAMPLES of a longer run are
// blended into the loop start so the wrap has no audible click.
static void* GenerateOcean(void) {
    void* block = VirtualAlloc(0, sizeof(WavHeader) + LOOP_SAMPLES * 2, MEM_COMMIT, PAGE_READWRITE);
    float* raw = (float*)VirtualAlloc(0, TOTAL_SAMPLES * sizeof(float), MEM_COMMIT, PAGE_READWRITE);
    WavHeader* header; short* pcm; int i;
    int swell_counter = SWELL_SAMPLES, texture_counter = TEXTURE_SAMPLES, creak_timer = 0, creak_burst = 0;
    float surf1 = 0.f, surf2 = 0.f, foam_smooth = 0.f, previous_white = 0.f;
    float swell_from = 0.f, swell_to = 0.f, texture_from = 0.f, texture_to = 0.f;
    float creak_y1 = 0.f, creak_y2 = 0.f;
    if (!block || !raw) return 0;

    rng_state = AUDIO_SEED;
    for (i = 0; i < TOTAL_SAMPLES; i++) {
        float white = NextNoise();
        float white2 = NextNoise();
        float shaped, texture, foam_raw, creak_excite, creak, sample;
        surf1 += (white - surf1) * .035f;
        surf2 += (surf1 - surf2) * .035f;
        shaped = Waypoint(&swell_counter, &swell_from, &swell_to, SWELL_SAMPLES);
        texture = Waypoint(&texture_counter, &texture_from, &texture_to, TEXTURE_SAMPLES);

        foam_raw = white2 - previous_white;
        previous_white = white2;
        foam_smooth += (foam_raw - foam_smooth) * .3f;

        if (creak_timer <= 0) {
            // NextNoise() is bipolar ([-1,1)); rescaled to [0,1) here since a
            // negative draw would shorten or even negate the interval - the
            // bug that made the very first creak's minimum delay land right
            // at the edge of the "too close to the loop end" guard below,
            // leaving it almost no room to ever actually fire.
            creak_timer = CREAK_MIN_SAMPLES + (int)((NextNoise() * .5f + .5f) * (float)(CREAK_MAX_SAMPLES - CREAK_MIN_SAMPLES));
            // Start the excitation burst (but still reschedule) only away
            // from the loop edges, so a creak's decay is never split across
            // the crossfade seam.
            if (i > SAMPLE_RATE && i < LOOP_SAMPLES - SAMPLE_RATE) creak_burst = CREAK_BURST_SAMPLES;
        }
        creak_timer--;
        if (creak_burst > 0) {
            // Parabolic envelope: 0 at both ends of the burst, peaking at its
            // midpoint - a rounded swell in and out. An envelope that instead
            // starts at full amplitude and only decays gives the burst a hard,
            // instant onset, which is what rang the resonator as a click - the
            // "bib" - rather than driving it like a groan.
            float fraction = 1.f - (float)creak_burst / (float)CREAK_BURST_SAMPLES;
            float envelope = 4.f * fraction * (1.f - fraction);
            creak_excite = NextNoise() * envelope * .4f;
            creak_burst--;
        } else {
            creak_excite = 0.f;
        }
        creak = creak_excite + CREAK_B1 * creak_y1 - CREAK_B2 * creak_y2;
        creak_y2 = creak_y1; creak_y1 = creak;

        sample = surf2 * (.25f + .7f * shaped)
               + foam_smooth * shaped * shaped * (.08f + .32f * texture)
               + creak * .05f;
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
