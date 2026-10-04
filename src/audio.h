#ifndef IN4K_AUDIO_H
#define IN4K_AUDIO_H

#define AUDIO_SAMPLE_RATE 22050

void AudioStart(void);

// The number of samples the sound device has played so far. This is the one
// clock of the intro: the picture's time is this divided by the sample rate, so
// the picture starts when the sound really starts and cannot drift from it. If
// there is no sound device it counts from the call to AudioStart instead.
int AudioSamples(void);

#endif
