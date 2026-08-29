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

#define SAMPLE_RATE 22050
// One buffer covering the whole run rather than a short loop, so the envelope
// can be written against the clock and put an event under the reveal. Longer
// than END_SECONDS in src/intro.c, since a machine that cannot hold 60 Hz
// stretches the visuals against the audio, which always plays in real time.
#define RUN_SECONDS 75
#define RUN_SAMPLES (SAMPLE_RATE * RUN_SECONDS)

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

// ---------------------------------------------------------------------------
// Tonal layer
//
// Sines without the CRT: the magic-circle oscillator, a symplectic rotation
// whose amplitude neither decays nor grows across the whole run - no lookup
// table, no renormalisation, and no sinf, which /NODEFAULTLIB has no library
// for. The output y starts at zero, so a voice entering mid-run enters
// silently, and k may change at any sample without disturbing the phase,
// which is what makes the chord changes below click-free.
//
//     y += k * z;   z -= k * y;      with k = 2*PI*f / SAMPLE_RATE
//
// That small-angle k puts the true frequency out by k*k/24 - 0.002% at the
// pad pitches and 0.12% at the accent - inaudible, and far cheaper than
// correcting it. The map is stable for k < 2, which every pitch here clears
// by three orders of magnitude.
#define TAU_PER_SAMPLE (6.2831853f / SAMPLE_RATE)
#define VOICES 5    // three pad voices, then the sub and the accent

// Key of D minor: it resolves brighter across the ascent and settles home
// under the held word. The three pad voices sit in D3..D4, above the surf
// body - which carries about 72% of its energy below 200 Hz - and below the
// foam band, so the chord occupies a gap in the ambience instead of fighting
// it. Six distinct pitches serve fifteen chord slots, so the chords are
// stored as byte indices into this table rather than as a flat float grid.
static const float pitch[8] = {
    130.813f,   // 0  C3
    146.832f,   // 1  D3
    174.614f,   // 2  F3
    220.000f,   // 3  A3
    233.082f,   // 4  Bb3
    293.665f,   // 5  D4
    73.416f,    // 6  D2, the sub under the reveal
    587.330f    // 7  D5, the accent struck at the reveal
};
// No voice moves by more than a whole step at any change. k is switched
// instantaneously, and smooth voice leading is what keeps that step reading
// as a chord change rather than as a jump.
static const unsigned char chord[5][3] = {
    { 1, 2, 3 },    // from  6s   Dm
    { 2, 3, 5 },    // from 16s   Dm/F
    { 1, 2, 4 },    // from 24s   Bb
    { 0, 2, 3 },    // from 30s   F
    { 1, 2, 3 }     // from 38s   Dm
};

static float osc_y[VOICES];
static float osc_z[VOICES] = { 1.f, 1.f, 1.f, 1.f, 1.f };

static float Oscillator(int voice, float frequency) {
    float k = frequency * TAU_PER_SAMPLE;
    osc_y[voice] += k * osc_z[voice];
    osc_z[voice] -= k * osc_y[voice];
    return osc_y[voice];
}

// The 44-byte RIFF header as literal little-endian words rather than a packed
// struct filled in field by field: mono 16-bit PCM at SAMPLE_RATE, with every
// size a compile-time constant. Copying eleven words is far less code than
// fifteen assignments and a four-character tag helper.
static const unsigned wav_header[11] = {
    0x46464952,                 // "RIFF"
    36 + RUN_SAMPLES * 2,       // bytes after this field
    0x45564157,                 // "WAVE"
    0x20746d66,                 // "fmt "
    16,                         // format chunk size
    0x00010001,                 // PCM, one channel
    SAMPLE_RATE,
    SAMPLE_RATE * 2,            // byte rate
    0x00100002,                 // block align 2, 16 bits per sample
    0x61746164,                 // "data"
    RUN_SAMPLES * 2
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

// GLSL's smoothstep, which the envelope below leans on as heavily as the
// shaders do. Waypoint was already computing one inline.
static float Smoothstep(float edge0, float edge1, float x) {
    float f = (x - edge0) / (edge1 - edge0);
    f = f < 0.f ? 0.f : f > 1.f ? 1.f : f;
    return f * f * (3.f - 2.f * f);
}

// Eases between two waypoints spaced period samples apart, smoothstepped.
static float Waypoint(int slot, int period) {
    if (waypoint_counter[slot] >= period) {
        waypoint_counter[slot] = 0;
        waypoint_from[slot] = waypoint_to[slot];
        waypoint_to[slot] = NextNoise() * .5f + .5f;
    }
    return waypoint_from[slot] + (waypoint_to[slot] - waypoint_from[slot])
         * Smoothstep(0.f, (float)period, (float)waypoint_counter[slot]++);
}

// Synthesizes the whole run's ocean ambience directly into the PCM block the
// player is handed - one pass, no float scratch buffer, no second allocation.
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
// Over that noise bed sits a tonal layer: three sine voices moving through
// five slow chord changes in D minor, a sub that arrives under the reveal and
// a single struck tone on the sample the word forms. It carries no rhythm and
// no melody by choice - the visuals are driven by sim_time and the audio by
// the sound card, so anything rhythmic here could drift against the picture on
// a machine that cannot hold 60 Hz. The pad rides the same macro envelope as
// the water, so adding it does not flatten the run's dynamics.
//
// Over the run the ambience follows the picture: quiet and close while the
// water is deep, opening out as the scene rises, surging as the particles let
// go at 30 seconds and settling again over the held word, then fading with the
// image. Nothing here is a sample or a sequence - the swells are still the
// random waypoints, with a scripted macro envelope over the top. Because the
// buffer is one shot rather than a loop, it needs no crossfade at all.
static void* GenerateOcean(void) {
    short* pcm;
    void* block = VirtualAlloc(0, 44 + RUN_SAMPLES * 2, MEM_COMMIT, PAGE_READWRITE);
    int i;
    float surf1 = 0.f, surf2 = 0.f, foam1 = 0.f, foam2 = 0.f, previous_white = 0.f;
    float accent = 0.f;
    if (!block) return 0;
    for (i = 0; i < 11; i++) ((unsigned*)block)[i] = wav_header[i];
    pcm = (short*)((char*)block + 44);

    rng_state = AUDIO_SEED;
    for (i = 0; i < RUN_SAMPLES; i++) {
        float white = NextNoise();
        float white2 = NextNoise();
        float shaped, texture, foam_raw, sample, tonal, wet;
        int section, voice;
        // Seconds into the run, which is what the macro envelope is written
        // against. These four windows must stay in step with the shaders: 6..30
        // is the ascent, 30 is when the particles let go, and 45..53 is the
        // master fade.
        float second = (float)i * (1.f / SAMPLE_RATE);
        float arc = .3f + .7f * Smoothstep(6.f, 30.f, second);
        float surge = Smoothstep(28.f, 31.5f, second) * (1.f - Smoothstep(33.f, 42.f, second));
        float macro = arc + .6f * surge;
        // The master fade is pulled out of macro and applied to the finished
        // mix instead, so it takes the tonal layer down with the ambience.
        float fade = 1.f - Smoothstep(45.f, 53.f, second);
        surf1 += (white - surf1) * .035f;
        surf2 += (surf1 - surf2) * .035f;
        shaped = Waypoint(0, SWELL_SAMPLES);
        texture = Waypoint(1, TEXTURE_SAMPLES);

        foam_raw = white2 - previous_white;
        previous_white = white2;
        foam1 += (foam_raw - foam1) * .16f;
        foam2 += (foam1 - foam2) * .16f;

        // The chord section, on the same clock as the macro envelope above.
        section = second < 16.f ? 0 : second < 24.f ? 1
                : second < 30.f ? 2 : second < 38.f ? 3 : 4;
        tonal = 0.f;
        for (voice = 0; voice < 3; voice++)
            tonal += Oscillator(voice, pitch[chord[section][voice]]);
        // The pad rides the same macro envelope as the water, so the chord
        // swells with the ascent and lifts at the reveal instead of sitting
        // at one level and flattening the run's dynamics.
        tonal *= .17f * macro * Smoothstep(6.f, 16.f, second);
        // The sub is what gives the reveal weight on a hall system, rather
        // than letting it arrive as nothing but more foam.
        tonal += Oscillator(3, pitch[6]) * .25f
               * Smoothstep(29.f, 32.f, second) * (1.f - Smoothstep(40.f, 50.f, second));
        // One struck tone on the sample the word forms, decayed by a repeated
        // multiply: an exponential with a 0.65 s time constant and no expf.
        if (i == 30 * SAMPLE_RATE) accent = 1.f;
        tonal += Oscillator(4, pitch[7]) * accent * .18f;
        accent *= .99993f;

        // The surge opens the foam further than the level alone would, so the
        // reveal arrives as a wash of white water rather than just louder surf.
        sample = (surf2 * (.25f + .7f * shaped)
               + foam2 * shaped * shaped * (.72f + 2.88f * texture) * (1.f + 1.8f * surge)) * macro;
        // Soft-knee saturation instead of a hard clamp: maps all of R into
        // (-1,1) smoothly, so loud swells round off instead of clipping. The
        // ambience gets its own knee before the tonal layer is added, so the
        // sustained pad cannot dominate the knee and duck the water on every
        // swell; the sum is then rounded off by a second one.
        wet = sample / (1.f + (sample < 0.f ? -sample : sample)) * .78f + tonal;
        pcm[i] = (short)(int)(wet / (1.f + (wet < 0.f ? -wet : wet)) * fade * 30000.f);
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
        DWORD written; unsigned size = 44 + RUN_SAMPLES * 2;
        HANDLE file = CreateFileA("ocean.wav", GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
        if (file != INVALID_HANDLE_VALUE) { WriteFile(file, wav, size, &written, 0); CloseHandle(file); }
    }
#endif
    PlaySoundA(wav, 0, SND_ASYNC | SND_MEMORY | SND_NODEFAULT);
}
