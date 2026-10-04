#ifndef IN4K_AUDIO_H
#define IN4K_AUDIO_H

void AudioStart(void);

#ifdef DEBUG
// Debug only. AudioStart also opens a second, silent waveOut stream just before
// the real sound is started, so the Debug build can see when the sound device
// actually begins consuming samples. AudioProbePosition returns the number of
// samples that stream has played, or -1 if it could not be opened.
int AudioProbePosition(void);
#endif

#endif
