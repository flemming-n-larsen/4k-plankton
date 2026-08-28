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
// Kept longer than the couple of seconds a single swell takes, so the loop
// itself does not feel repetitive.
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

// The 44-byte RIFF header as literal little-endian words rather than a packed
// struct filled in field by field: mono 16-bit PCM at SAMPLE_RATE, with every
// size a compile-time constant. Copying eleven words is far less code than
// fifteen assignments and a four-character tag helper.
static const unsigned wav_header[11] = {
    0x46464952,                 // "RIFF"
    36 + LOOP_SAMPLES * 2,      // bytes after this field
    0x45564157,                 // "WAVE"
    0x20746d66,                 // "fmt "
    16,                         // format chunk size
    0x00010001,                 // PCM, one channel
    SAMPLE_RATE,
    SAMPLE_RATE * 2,            // byte rate
    0x00100002,                 // block align 2, 16 bits per sample
    0x61746164,                 // "data"
    LOOP_SAMPLES * 2
};

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

// Waypoint state for the two envelopes, indexed by the slot argument: the
// swell in slot 0 and the foam texture in slot 1. Held here rather than passed
// in as three pointers per call, which cost more than the two arrays do. The
// counters start at their own period so the first call seeds a target.
static float waypoint_from[2], waypoint_to[2];
static int waypoint_counter[2] = { SWELL_SAMPLES, TEXTURE_SAMPLES };

// Eases between two waypoints spaced period samples apart, smoothstepped.
static float Waypoint(int slot, int period) {
    float fraction, eased;
    if (waypoint_counter[slot] >= period) {
        waypoint_counter[slot] = 0;
        waypoint_from[slot] = waypoint_to[slot];
        waypoint_to[slot] = NextNoise() * .5f + .5f;
    }
    fraction = (float)waypoint_counter[slot]++ / (float)period;
    eased = fraction * fraction * (3.f - 2.f * fraction);
    return waypoint_from[slot] + (waypoint_to[slot] - waypoint_from[slot]) * eased;
}

// Synthesizes a seamless, loopable ocean ambience directly into the PCM block
// the player is handed - no float scratch buffer, since the only thing that
// needed one was the crossfade, which works just as well on the samples.
//
// The amplitude envelope eases between random waypoints a couple of seconds
// apart, so swells rise and fall with real amplitude rather than staying
// near-constant - a single leaky-integrator filter was tried first, but a
// coefficient slow enough to change over seconds also crushed its variance to
// almost nothing, since for that filter bandwidth and amplitude shrink
// together. The envelope shapes a two-stage low-passed surf body.
//
// Foam is differenced noise - a 6 dB/octave rise - smoothed by two low-pass
// stages rather than one. One stage only takes that same 6 dB/octave back, so
// nothing above the corner ever rolls off and the layer reads as bright hiss
// sitting on top of the water. Two stages fall 12 dB/octave, leaving a band a
// few hundred Hz wide that reads as foam rather than treble. The stages cost
// most of the level, so the mix amount below is scaled back up to keep the
// foam-to-surf balance; that amount itself drifts slowly between quieter and
// hissier waypoints, so the brightness of the ambience varies over time.
//
// The last CROSSFADE_SAMPLES of a longer run are blended into the loop start
// so the wrap has no audible click.
static void* GenerateOcean(void) {
    short* pcm;
    void* block = VirtualAlloc(0, 44 + TOTAL_SAMPLES * 2, MEM_COMMIT, PAGE_READWRITE);
    int i;
    float surf1 = 0.f, surf2 = 0.f, foam1 = 0.f, foam2 = 0.f, previous_white = 0.f;
    if (!block) return 0;
    for (i = 0; i < 11; i++) ((unsigned*)block)[i] = wav_header[i];
    pcm = (short*)((char*)block + 44);

    rng_state = AUDIO_SEED;
    for (i = 0; i < TOTAL_SAMPLES; i++) {
        float white = NextNoise();
        float white2 = NextNoise();
        float shaped, texture, foam_raw, sample;
        surf1 += (white - surf1) * .035f;
        surf2 += (surf1 - surf2) * .035f;
        shaped = Waypoint(0, SWELL_SAMPLES);
        texture = Waypoint(1, TEXTURE_SAMPLES);

        foam_raw = white2 - previous_white;
        previous_white = white2;
        foam1 += (foam_raw - foam1) * .16f;
        foam2 += (foam1 - foam2) * .16f;

        sample = surf2 * (.25f + .7f * shaped)
               + foam2 * shaped * shaped * (.72f + 2.88f * texture);
        // Soft-knee saturation instead of a hard clamp: maps all of R into
        // (-1,1) smoothly, so loud swells round off instead of clipping.
        pcm[i] = (short)(int)(sample / (1.f + (sample < 0.f ? -sample : sample)) * 30000.f);
    }
    // The loop plays samples 0..LOOP_SAMPLES-1, so sample 0 has to continue
    // from sample LOOP_SAMPLES-1: the blend therefore starts at the run's
    // continuation past the loop and eases back to the run's own opening.
    for (i = 0; i < CROSSFADE_SAMPLES; i++) {
        float mix = (float)i / (float)CROSSFADE_SAMPLES;
        pcm[i] = (short)(int)(pcm[LOOP_SAMPLES + i] * (1.f - mix) + pcm[i] * mix);
    }
    return block;
}

void AudioStart(void) {
    void* wav = GenerateOcean();
    if (!wav) return;
#ifdef DEBUG
    // Written so the generated sound can be reviewed outside the running
    // intro; not part of the Compo path.
    {
        DWORD written; unsigned size = 44 + LOOP_SAMPLES * 2;
        HANDLE file = CreateFileA("ocean.wav", GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
        if (file != INVALID_HANDLE_VALUE) { WriteFile(file, wav, size, &written, 0); CloseHandle(file); }
    }
#endif
    PlaySoundA(wav, 0, SND_ASYNC | SND_LOOP | SND_MEMORY | SND_NODEFAULT);
}
