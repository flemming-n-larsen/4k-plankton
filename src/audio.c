#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "audio.h"

// mmsystem.h is excluded by WIN32_LEAN_AND_MEAN; the waveOut calls and
// structures needed are declared by hand instead, matching this project's style
// of avoiding extra system headers. waveOut rather than PlaySoundA, because it
// can report how far the sound has played, which is the intro's clock.
#pragma pack(push, 1)
typedef struct { WORD tag, channels; DWORD rate, bytes_per_second; WORD block_align, bits, extra; } WaveFormat;
typedef struct { UINT type; DWORD value; DWORD unused; } WaveTime;
#pragma pack(pop)
typedef struct { char* data; DWORD length, recorded; DWORD_PTR user; DWORD flags, loops; void* next; DWORD_PTR reserved; } WaveHeader;
__declspec(dllimport) UINT __stdcall waveOutOpen(void** device, UINT id, const void* format, DWORD_PTR callback, DWORD_PTR instance, DWORD flags);
__declspec(dllimport) UINT __stdcall waveOutPrepareHeader(void* device, WaveHeader* header, UINT size);
__declspec(dllimport) UINT __stdcall waveOutWrite(void* device, WaveHeader* header, UINT size);
__declspec(dllimport) UINT __stdcall waveOutGetPosition(void* device, WaveTime* time, UINT size);
static void* device;
static WaveHeader header;
#ifdef DEBUG
static DWORD fallback_ticks;
#endif

#define SAMPLE_RATE AUDIO_SAMPLE_RATE
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
// this many samples instead of jumping, so the rise and fall itself takes
// seconds, like a real wave set. Five, not the two it was: at two seconds a
// new wave arrives faster than the ear reads one, and the ambience is busy
// rather than moving.
#define SWELL_SAMPLES (SAMPLE_RATE * 5)
// Slower than SWELL_SAMPLES, and not a multiple of it, so the two envelopes
// drift against each other instead of lining up: eases the foam/hiss amount
// between waypoints, so the brightness of the ambience drifts over several
// seconds instead of staying at one fixed mix.
#define TEXTURE_SAMPLES (SAMPLE_RATE * 9)
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
#define VOICES 6    // three pad voices, then the sub, accent and bubbles

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
    { 2, 3, 5 },    // from 18s   Dm/F
    { 1, 2, 4 },    // from 24s   Bb
    { 0, 2, 3 },    // from 30s   F
    { 1, 2, 3 }     // from 38s   Dm
};

// The motif: four struck tones, ratios above D5, D, F, A and then D an octave
// up, an arpeggio of the key. It is first heard twice as a question that stops
// on F, softly at 12 s and louder under the wave of light at 18.6 s, and then
// answered, resolving on the high D, as the four letters of the word lock.
// Start times are in fifths of a second.
static const float motif_ratio[4] = { 1.f, 1.1892f, 1.4983f, 2.f };
static const unsigned char motif_start[3] = { 60, 93, 176 };

static float osc_y[VOICES];
static float osc_z[VOICES] = { 1.f, 1.f, 1.f, 1.f, 1.f, 1.f };

static float __vectorcall Oscillator(int voice, float frequency) {
    float k = frequency * TAU_PER_SAMPLE;
    osc_y[voice] += k * osc_z[voice];
    osc_z[voice] -= k * osc_y[voice];
    return osc_y[voice];
}

#ifdef DEBUG
// The 44-byte RIFF header, only for the ocean.wav the Debug build writes so the
// sound can be reviewed outside the intro: the Compo build hands raw samples to
// waveOut and needs no header at all. Literal little-endian words: mono 16-bit
// PCM at SAMPLE_RATE, with every size a compile-time constant.
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
#endif

static unsigned int rng_state;

static unsigned int NextRandom(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

// Signed range, not unsigned: a direct float-to-unsigned cast pulls in a CRT
// helper that /NODEFAULTLIB has no library for, while float-to-int does not.
static float __vectorcall NextNoise(void) {
    return (float)(int)NextRandom() * (1.f / 2147483648.f);
}

// Waypoint state for the two envelopes, indexed by the slot argument: the
// swell in slot 0 and the foam texture in slot 1. Held here rather than passed
// in as three pointers per call, which cost more than the two arrays do. The
// counters start at their own period so the first call seeds a target.
static float waypoint_from[2], waypoint_to[2];
static int waypoint_counter[2] = { SWELL_SAMPLES, TEXTURE_SAMPLES };

// GLSL's smoothstep, which the envelope below leans on as heavily as the
// shaders do. Waypoint was already computing one inline. The edges are ints:
// a float constant is a five-byte push at every call, an int under 128 is two.
// The envelope's times are whole numbers of half seconds, which keeps them
// ints, and doubling is exact in floating point, so the result is bit for bit
// what it was with the edges in seconds.
static float __vectorcall Smoothstep(int edge0, int edge1, float x) {
    float f = (x - (float)edge0) / (float)(edge1 - edge0);
    f = f < 0.f ? 0.f : f > 1.f ? 1.f : f;
    return f * f * (3.f - 2.f * f);
}

// One-pole low-pass: the state moves a fraction k of the way to the input.
static float __vectorcall Lowpass(float* state, float input, float k) {
    return *state += (input - *state) * k;
}

// A swell: up between the first two edges, down between the last two.
static float __vectorcall Bump(int up0, int up1, int down0, int down1, float x) {
    return Smoothstep(up0, up1, x) * (1.f - Smoothstep(down0, down1, x));
}

// Eases between two waypoints spaced period samples apart, smoothstepped.
static float __vectorcall Waypoint(int slot, int period) {
    if (waypoint_counter[slot] >= period) {
        waypoint_counter[slot] = 0;
        waypoint_from[slot] = waypoint_to[slot];
        waypoint_to[slot] = NextNoise() * .5f + .5f;
    }
    return waypoint_from[slot] + (waypoint_to[slot] - waypoint_from[slot])
         * Smoothstep(0, period, (float)waypoint_counter[slot]++);
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
// a single struck tone on the sample the word forms. Bloops are sparse,
// untimed cascades of short bubble grains in the open water, not a beat; their
// density rises only with the two logo transitions. The visuals are driven by
// sim_time and the audio by the sound card, so anything rhythmic here could
// drift against the picture on a machine that cannot hold 60 Hz. The pad rides
// the same macro envelope as the water, so adding it does not flatten the run's
// dynamics.
//
// Over the run the ambience follows the picture: quiet and close while the
// water is deep, opening out as the scene rises, surging as the word snaps
// together at 35 seconds and settling again over the held word, then fading with the
// image. Nothing here is a sample or a sequence - the swells are still the
// random waypoints, with a scripted macro envelope over the top. Because the
// buffer is one shot rather than a loop, it needs no crossfade at all.
static short pcm[RUN_SAMPLES];

static void GenerateOcean(void) {
    int i;
    float surf1 = 0.f, surf2 = 0.f, foam1 = 0.f, foam2 = 0.f, foam3 = 0.f, previous_white = 0.f;
    float accent = 0.f, accent_ratio = 1.f;
    int bubble_counter = SAMPLE_RATE, bubble_age = 0, bubble_length = 0, bubble_chain = 0, bubble_delay = 0;
    float bubble_frequency = 0.f;
    rng_state = AUDIO_SEED;
    for (i = 0; i < RUN_SAMPLES; i++) {
        float white = NextNoise();
        float white2 = NextNoise();
        float shaped, texture, foam_raw, sample, tonal, wet, bubble;
        int section, voice;
        // Half seconds into the run, which is what the macro envelope is
        // written against: every number below that is compared with it is twice
        // the time in seconds. These windows must stay in step with the shaders:
        // 6..30 s is the ascent, 34..36 s is when the word snaps together, and
        // 54..62 s is the master fade.
        float half = (float)i * (2.f / SAMPLE_RATE);
        float arc = .3f + .7f * Smoothstep(12, 60, half);
        // The small swell under the wave of light at 17..21, so the free-swim
        // stretch has an event the sound answers as well as the picture.
        float surge = Bump(62, 70, 74, 90, half)
                    + .5f * Bump(34, 38, 39, 45, half);
        // A hush in the water around 18 s, a moment before the wave of light
        // arrives, which the picture shows as the dip in its brightness.
        float hush = Bump(33, 36, 36, 39, half);
        float macro = (arc + .6f * surge) * (1.f - .5f * hush);
        // A few bubbles belong in the water throughout the run. Pack them
        // around the two visible transitions: formation at 30..36 and the
        // logo's fade-out at 48..56. These are broad windows so the sound
        // does not call attention to one arbitrary sample boundary.
        float logo_burst = Bump(58, 62, 70, 74, half)
                         + Bump(94, 98, 108, 112, half);
        // The master fade is pulled out of macro and applied to the finished
        // mix instead, so it takes the tonal layer down with the ambience.
        float fade = 1.f - Smoothstep(108, 124, half);
        Lowpass(&surf1, white, .035f);
        Lowpass(&surf2, surf1, .035f);
        shaped = Waypoint(0, SWELL_SAMPLES);
        texture = Waypoint(1, TEXTURE_SAMPLES);

        foam_raw = white2 - previous_white;
        previous_white = white2;
        Lowpass(&foam1, foam_raw, .14f);
        Lowpass(&foam2, foam1, .14f);
        Lowpass(&foam3, foam2, .14f);

        // The chord section, on the same clock as the macro envelope above.
        section = half < 36.f ? 0 : half < 48.f ? 1
                : half < 60.f ? 2 : half < 76.f ? 3 : 4;
        tonal = 0.f;
        for (voice = 0; voice < 3; voice++)
            tonal += Oscillator(voice, pitch[chord[section][voice]]);
        // The pad rides the same macro envelope as the water, so the chord
        // swells with the ascent and lifts at the reveal instead of sitting
        // at one level and flattening the run's dynamics.
        tonal *= .17f * macro * Smoothstep(12, 32, half);
        // The sub is what gives the reveal weight on a hall system, rather
        // than letting it arrive as nothing but more foam.
        tonal += Oscillator(3, pitch[6]) * .25f
               * Bump(64, 70, 86, 106, half);
        // The motif, a struck tone every .55 s. The last of the three
        // statements is the one that follows the left-to-right assembly of
        // the letters in particles.comp.glsl. Each tone is decayed by a
        // repeated multiply: an exponential with a 0.65 s time constant and no
        // expf.
        for (voice = 0; voice < 3; voice++) {
            int hit = i - motif_start[voice] * (SAMPLE_RATE / 5), gap = SAMPLE_RATE * 55 / 100, note = hit / gap;
            if (hit >= 0 && hit % gap == 0 && note < 4) {
                accent = .3f + .35f * voice;
                accent_ratio = motif_ratio[note == 3 && voice < 2 ? 1 : note];
            }
        }
        tonal += Oscillator(4, pitch[7] * accent_ratio) * accent * .18f;
        // A power of two, so that no rounding of the constant can change it: this
        // factor is within 1e-4 of 1, and /TRUNCATEFLOATS rounded .99993 to 1.
        accent -= accent * (1.f / 16384);

        // Reference WAV analysis: sine sweeps rise from about 140..200 Hz
        // toward 400..600 Hz over roughly 150 ms. A rounded attack and
        // diminishing tail give each bubble its own short, upward bloop.
        // A bubble starts either inside a chain, when its delay runs out, or as
        // the first of a new chain when the gap counter does. The two share
        // everything but the chain bookkeeping, and the random draws keep the
        // order they always had.
        if (!bubble_age && (bubble_chain ? --bubble_delay <= 0 : --bubble_counter <= 0)) {
            int fresh = !bubble_chain;
            bubble_age = 1;
            bubble_length = SAMPLE_RATE / (10 + 2 * (NextRandom() & 3));
            bubble_frequency = 80.f + (NextNoise() * .5f + .5f) * 60.f;
            osc_y[5] = 0.f; osc_z[5] = 1.f;
            if (fresh) bubble_chain = 6 * (2 + (NextRandom() & 3) + (int)(logo_burst * 2.f));
            bubble_chain--;
            bubble_delay = SAMPLE_RATE / (400 + (NextRandom() & 255));
            if (fresh) bubble_counter = (int)(SAMPLE_RATE * (1.2f + 2.2f * (NextNoise() * .5f + .5f)
                - .8f * logo_burst));
        }
        bubble = 0.f;
        if (bubble_age) {
            float age = (float)bubble_age / bubble_length;
            float envelope = 6.75f * age * (1.f - age) * (1.f - age);
            bubble = Oscillator(5, bubble_frequency * (1.f + age + 2.f * age * age)) * envelope;
            if (++bubble_age >= bubble_length) bubble_age = 0;
        }

        // The foam gate has a floor under it. Squaring the swell alone put the
        // foam through a 94x range and silenced it outright for two seconds at
        // a time whenever the waypoint landed near zero, which is what made the
        // ambience read as washy instead of moving: the layer was not varying,
        // it was switching off. The .2 keeps a bed of foam under the quietest
        // swell, and the mix constants below are scaled by 1/1.4 to hold the
        // same average foam-to-surf level, so only the evenness changes.
        //
        // The surge opens the foam further than the level alone would, so the
        // reveal arrives as a wash of white water instead of just louder surf.
        sample = (surf2 * (.25f + .7f * shaped)
               + foam3 * (.2f + .8f * shaped * shaped) * (.52f + 2.f * texture) * (1.f + 1.4f * surge)) * macro;
        // Soft-knee saturation instead of a hard clamp: maps all of R into
        // (-1,1) smoothly, so loud swells round off instead of clipping. The
        // ambience gets its own knee before the tonal layer is added, so the
        // sustained pad cannot dominate the knee and duck the water on every
        // swell. The sum is limited by a cubic soft clip instead: x - x^3/3 is
        // nearly linear for small x, so the pad and the sub, which are pure
        // sines, are not given the harmonics a knee with no linear part adds
        // (the old x / (1 + |x|) put 5 to 13% of the output in the nonlinear
        // part, the cubic 1 to 8%), and it still rounds off the loud swells,
        // which it turns over gently above 1. It would change sign at 1.73;
        // the sound is the same on every run and its sum never passes about
        // 1.12, so there is no clamp in front of it, which would cost bytes.
        wet = sample / (1.f + (sample < 0.f ? -sample : sample)) * .78f + tonal + bubble * .08f;
        pcm[i] = (short)(int)(wet * (1.f - wet * wet * (1.f / 3.f)) * fade * 25000.f);
    }
}

// An initialised static rather than a local filled in field by field.
static const WaveFormat format = { 1, 1, SAMPLE_RATE, SAMPLE_RATE * 2, 2, 16, 0 };

void AudioStart(void) {
    GenerateOcean();
    header.data = (char*)pcm;
    header.length = RUN_SAMPLES * 2;
#ifdef DEBUG
    fallback_ticks = GetTickCount();
    // Written so the generated sound can be reviewed outside the running
    // intro; not part of the Compo path.
    {
        DWORD written;
        HANDLE file = CreateFileA("ocean.wav", GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
        if (file != INVALID_HANDLE_VALUE) {
            WriteFile(file, wav_header, 44, &written, 0);
            WriteFile(file, header.data, header.length, &written, 0);
            CloseHandle(file);
        }
    }
    // Without a device the intro still runs, on the tick count.
    if (waveOutOpen(&device, (UINT)-1, &format, 0, 0, 0)) { device = 0; return; }
#else
    // No fallbacks in the Compo build: the compo machines have a sound device,
    // and without one there is nothing to hear.
    waveOutOpen(&device, (UINT)-1, &format, 0, 0, 0);
#endif
    waveOutPrepareHeader(device, &header, sizeof(header));
    waveOutWrite(device, &header, sizeof(header));
}

int AudioSamples(void) {
    WaveTime time = { 2 };    // TIME_SAMPLES
#ifdef DEBUG
    if (!device) return (int)((float)(int)(GetTickCount() - fallback_ticks) * (SAMPLE_RATE * .001f));
#endif
    waveOutGetPosition(device, &time, sizeof(time));
    return (int)time.value;
}
