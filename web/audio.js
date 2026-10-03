const SAMPLE_RATE = 22050;
const RUN_SECONDS = 75;
const SAMPLE_COUNT = SAMPLE_RATE * RUN_SECONDS;
const TAU_PER_SAMPLE = (2 * Math.PI) / SAMPLE_RATE;
const SWELL_SAMPLES = SAMPLE_RATE * 5;
const TEXTURE_SAMPLES = SAMPLE_RATE * 9;
const AUDIO_SEED = 20260825;
const pitches = [130.813, 146.832, 174.614, 220, 233.082, 293.665, 73.416, 587.33];
const chords = [[1, 2, 3], [2, 3, 5], [1, 2, 4], [0, 2, 3], [1, 2, 3]];

function smoothstep(edge0, edge1, value) {
  const x = Math.max(0, Math.min(1, (value - edge0) / (edge1 - edge0)));
  return x * x * (3 - 2 * x);
}

function makeRandom() {
  let state = AUDIO_SEED >>> 0;
  const nextRandom = () => {
    state ^= state << 13;
    state >>>= 0;
    state ^= state >>> 17;
    state >>>= 0;
    state ^= state << 5;
    state >>>= 0;
    return state >>> 0;
  };
  return {
    nextRandom,
    noise: () => (nextRandom() | 0) / 2147483648
  };
}

function synthesizeOcean(context) {
  const buffer = context.createBuffer(1, SAMPLE_COUNT, SAMPLE_RATE);
  const pcm = buffer.getChannelData(0);
  const random = makeRandom();
  const noise = random.noise;
  const oscY = new Float32Array(6);
  const oscZ = new Float32Array([1, 1, 1, 1, 1, 1]);
  const waypointFrom = [0, 0];
  const waypointTo = [0, 0];
  const waypointCounter = [SWELL_SAMPLES, TEXTURE_SAMPLES];
  let surf1 = 0, surf2 = 0, foam1 = 0, foam2 = 0, foam3 = 0, previousWhite = 0;
  let accent = 0;
  let bubbleCounter = SAMPLE_RATE, bubbleAge = 0, bubbleLength = 0, bubbleChain = 0, bubbleDelay = 0;
  let bubbleFrequency = 0, logoChainStarted = false;

  const oscillator = (voice, frequency) => {
    const k = frequency * TAU_PER_SAMPLE;
    oscY[voice] += k * oscZ[voice];
    oscZ[voice] -= k * oscY[voice];
    return oscY[voice];
  };
  const waypoint = (slot, period) => {
    if (waypointCounter[slot] >= period) {
      waypointCounter[slot] = 0;
      waypointFrom[slot] = waypointTo[slot];
      waypointTo[slot] = noise() * 0.5 + 0.5;
    }
    const value = waypointCounter[slot]++;
    return waypointFrom[slot] + (waypointTo[slot] - waypointFrom[slot]) * smoothstep(0, period, value);
  };

  for (let i = 0; i < SAMPLE_COUNT; i++) {
    const white = noise();
    const white2 = noise();
    const second = i / SAMPLE_RATE;
    const arc = 0.3 + 0.7 * smoothstep(6, 30, second);
    const surge = smoothstep(28, 31.5, second) * (1 - smoothstep(33, 42, second));
    const macro = arc + 0.6 * surge;
    const logoBurst = smoothstep(29, 31, second) * (1 - smoothstep(35, 37, second))
      + smoothstep(47, 49, second) * (1 - smoothstep(54, 56, second));
    const fade = 1 - smoothstep(48, 56, second);

    if (second >= 30 && second < 31 && !logoChainStarted) {
      logoChainStarted = true;
      if (!bubbleAge && !bubbleChain) bubbleCounter = 0;
    }

    surf1 += (white - surf1) * 0.035;
    surf2 += (surf1 - surf2) * 0.035;
    const shaped = waypoint(0, SWELL_SAMPLES);
    const texture = waypoint(1, TEXTURE_SAMPLES);
    const foamRaw = white2 - previousWhite;
    previousWhite = white2;
    foam1 += (foamRaw - foam1) * 0.14;
    foam2 += (foam1 - foam2) * 0.14;
    foam3 += (foam2 - foam3) * 0.14;

    const section = second < 16 ? 0 : second < 24 ? 1 : second < 30 ? 2 : second < 38 ? 3 : 4;
    let tonal = 0;
    for (let voice = 0; voice < 3; voice++) tonal += oscillator(voice, pitches[chords[section][voice]]);
    tonal *= 0.17 * macro * smoothstep(6, 16, second);
    tonal += oscillator(3, pitches[6]) * 0.25 * smoothstep(29, 32, second) * (1 - smoothstep(43, 53, second));
    if (i === 30 * SAMPLE_RATE) accent = 1;
    tonal += oscillator(4, pitches[7]) * accent * 0.18;
    accent *= 0.99993;

    if (!bubbleAge && bubbleChain && --bubbleDelay <= 0) {
      bubbleAge = 1;
      bubbleLength = Math.floor(SAMPLE_RATE / (10 + 2 * (random.nextRandom() & 3)));
      bubbleFrequency = 80 + (noise() * 0.5 + 0.5) * 60;
      oscY[5] = 0;
      oscZ[5] = 1;
      bubbleChain--;
      bubbleDelay = Math.floor(SAMPLE_RATE / (400 + (random.nextRandom() & 255)));
    } else if (!bubbleAge && !bubbleChain && --bubbleCounter <= 0) {
      bubbleAge = 1;
      bubbleLength = Math.floor(SAMPLE_RATE / (10 + 2 * (random.nextRandom() & 3)));
      bubbleFrequency = 80 + (noise() * 0.5 + 0.5) * 60;
      oscY[5] = 0;
      oscZ[5] = 1;
      bubbleChain = 6 * (2 + (random.nextRandom() & 3) + Math.floor(logoBurst * 2)) - 1;
      bubbleDelay = Math.floor(SAMPLE_RATE / (400 + (random.nextRandom() & 255)));
      bubbleCounter = Math.floor(SAMPLE_RATE * (1.2 + 2.2 * (noise() * 0.5 + 0.5) - 0.8 * logoBurst));
    }

    let bubble = 0;
    if (bubbleAge) {
      const age = bubbleAge / bubbleLength;
      const envelope = 6.75 * age * (1 - age) * (1 - age);
      bubble = oscillator(5, bubbleFrequency * (1 + age + 2 * age * age)) * envelope;
      if (++bubbleAge >= bubbleLength) bubbleAge = 0;
    }

    const sample = (surf2 * (0.25 + 0.7 * shaped)
      + foam3 * (0.2 + 0.8 * shaped * shaped) * (0.52 + 2 * texture) * (1 + 1.4 * surge)) * macro;
    const wet = sample / (1 + Math.abs(sample)) * 0.78 + tonal + bubble * 0.08;
    pcm[i] = wet / (1 + Math.abs(wet)) * fade * (30000 / 32768);
  }
  return buffer;
}

export class OceanAudio {
  constructor() {
    this.context = null;
    this.buffer = null;
    this.source = null;
    this.gain = null;
    this.muted = false;
  }

  async prepare() {
    if (!this.context) {
      const AudioContextClass = window.AudioContext || window.webkitAudioContext;
      if (!AudioContextClass) throw new Error('Web Audio is unavailable in this browser.');
      this.context = new AudioContextClass();
      this.gain = this.context.createGain();
      this.gain.gain.value = this.muted ? 0 : 0.68;
      this.gain.connect(this.context.destination);
    }
    await this.context.resume();
    if (!this.buffer) this.buffer = synthesizeOcean(this.context);
    return true;
  }

  play(offset = 0) {
    if (!this.context || !this.buffer || offset >= this.buffer.duration) return;
    this.stop();
    this.source = this.context.createBufferSource();
    this.source.buffer = this.buffer;
    this.source.connect(this.gain);
    this.source.start(0, Math.max(0, offset));
  }

  stop() {
    if (!this.source) return;
    try { this.source.stop(); } catch {}
    this.source.disconnect();
    this.source = null;
  }

  setMuted(muted) {
    this.muted = muted;
    if (this.gain && this.context) {
      this.gain.gain.setTargetAtTime(muted ? 0 : 0.68, this.context.currentTime, 0.025);
    }
  }
}
