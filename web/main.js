import { OceanAudio } from './audio.js';

const PARTICLES_PER_STEP = 128;
const SIM_STEP = 1 / 60;
const END_SECONDS = 59;
const MAX_CATCHUP = 4;
const UNIFORM_ALIGNMENT = 256;

const canvas = document.querySelector('#scene');
const startButton = document.querySelector('#start-button');
const qualitySelect = document.querySelector('#quality');
const statusText = document.querySelector('#status');
const introCard = document.querySelector('#intro-card');
const errorCard = document.querySelector('#error-card');
const errorMessage = document.querySelector('#error-message');
const playControls = document.querySelector('#play-controls');
const endCard = document.querySelector('#end-card');
const progressBar = document.querySelector('#progress');
const soundButton = document.querySelector('#sound-button');
const pauseButton = document.querySelector('#pause-button');
const fullscreenButton = document.querySelector('#fullscreen-button');
const replayButton = document.querySelector('#replay-button');

const oceanAudio = new OceanAudio();
let device;
let context;
let canvasFormat;
let scene;
let runStartedAt = 0;
let pausedAt = 0;
let frameNumber = 0;
let simulationTime = 0;
let animationFrame = 0;
let running = false;
let paused = false;

function resizeCanvas() {
  if (!device || !context) return;
  const ratio = Math.min(window.devicePixelRatio || 1, 2);
  const width = Math.max(1, Math.round(canvas.clientWidth * ratio));
  const height = Math.max(1, Math.round(canvas.clientHeight * ratio));
  if (canvas.width === width && canvas.height === height) return;
  canvas.width = width;
  canvas.height = height;
}

async function initializeGPU() {
  if (!navigator.gpu) throw new Error('This browser does not expose WebGPU. Try a current Chromium browser with a supported graphics driver.');
  const adapter = await navigator.gpu.requestAdapter({ powerPreference: 'high-performance' });
  if (!adapter) throw new Error('No compatible GPU adapter was found. Check browser graphics settings and driver support.');
  device = await adapter.requestDevice();
  context = canvas.getContext('webgpu');
  if (!context) throw new Error('The browser could not create a WebGPU canvas context.');
  canvasFormat = navigator.gpu.getPreferredCanvasFormat();
  canvas.width = Math.max(1, Math.round(canvas.clientWidth * Math.min(window.devicePixelRatio || 1, 2)));
  canvas.height = Math.max(1, Math.round(canvas.clientHeight * Math.min(window.devicePixelRatio || 1, 2)));
  context.configure({ device, format: canvasFormat, alphaMode: 'opaque' });
  window.addEventListener('resize', resizeCanvas);
  device.lost.then((info) => {
    if (info.reason !== 'destroyed') showError(`The GPU device was lost${info.message ? `: ${info.message}` : '.'}`);
  });
}

async function loadText(path) {
  const response = await fetch(path);
  if (!response.ok) throw new Error(`Could not load ${path} (${response.status}).`);
  return response.text();
}

function makeShaderModule(code, label) {
  const module = device.createShaderModule({ code, label });
  return module;
}

async function buildScene(particleCount) {
  statusText.textContent = 'Compiling the particle and water shaders…';
  const [simulationSource, renderSource] = await Promise.all([
    loadText('./shaders/simulation.wgsl'),
    loadText('./shaders/render.wgsl')
  ]);
  const simulationModule = makeShaderModule(
    simulationSource.replace('__PARTICLE_COUNT__', String(particleCount)),
    'Particle Life compute shader'
  );
  const renderModule = makeShaderModule(renderSource, 'Plankton render shaders');

  const particleBuffer = device.createBuffer({
    label: `${particleCount} particle states`,
    size: particleCount * 32,
    usage: GPUBufferUsage.STORAGE
  });
  const frameBuffer = device.createBuffer({
    label: 'Frame uniforms',
    size: 16,
    usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST
  });
  const parameterBuffer = device.createBuffer({
    label: 'Simulation step uniforms',
    size: UNIFORM_ALIGNMENT * MAX_CATCHUP * 2,
    usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST
  });

  const simulationGroupLayout = device.createBindGroupLayout({
    entries: [
      { binding: 0, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'storage', minBindingSize: particleCount * 32 } },
      { binding: 1, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'uniform', hasDynamicOffset: true, minBindingSize: 16 } }
    ]
  });
  const simulationLayout = device.createPipelineLayout({ bindGroupLayouts: [simulationGroupLayout] });
  const simulationPipeline = await device.createComputePipelineAsync({
    label: 'Particle Life compute pipeline',
    layout: simulationLayout,
    compute: { module: simulationModule, entryPoint: 'main' }
  });
  const simulationGroup = device.createBindGroup({
    layout: simulationGroupLayout,
    entries: [
      { binding: 0, resource: { buffer: particleBuffer, offset: 0, size: particleCount * 32 } },
      { binding: 1, resource: { buffer: parameterBuffer, offset: 0, size: 16 } }
    ]
  });

  const renderGroupLayout = device.createBindGroupLayout({
    entries: [
      { binding: 0, visibility: GPUShaderStage.VERTEX, buffer: { type: 'read-only-storage', minBindingSize: particleCount * 32 } },
      { binding: 1, visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT, buffer: { type: 'uniform', minBindingSize: 16 } }
    ]
  });
  const renderLayout = device.createPipelineLayout({ bindGroupLayouts: [renderGroupLayout] });
  const additiveBlend = {
    color: { operation: 'add', srcFactor: 'one', dstFactor: 'one' },
    alpha: { operation: 'add', srcFactor: 'one', dstFactor: 'one' }
  };
  const particlePipeline = await device.createRenderPipelineAsync({
    label: 'Additive particle sprites',
    layout: renderLayout,
    vertex: { module: renderModule, entryPoint: 'particle_vertex' },
    fragment: { module: renderModule, entryPoint: 'particle_fragment', targets: [{ format: canvasFormat, blend: additiveBlend }] },
    primitive: { topology: 'triangle-list', cullMode: 'none' }
  });
  const backgroundPipeline = await device.createRenderPipelineAsync({
    label: 'Procedural underwater background',
    layout: renderLayout,
    vertex: { module: renderModule, entryPoint: 'background_vertex' },
    fragment: { module: renderModule, entryPoint: 'background_fragment', targets: [{ format: canvasFormat }] },
    primitive: { topology: 'triangle-list' }
  });
  const renderGroup = device.createBindGroup({
    layout: renderGroupLayout,
    entries: [
      { binding: 0, resource: { buffer: particleBuffer, offset: 0, size: particleCount * 32 } },
      { binding: 1, resource: { buffer: frameBuffer, offset: 0, size: 16 } }
    ]
  });

  scene = {
    particleCount,
    particleBuffer,
    frameBuffer,
    parameterBuffer,
    simulationPipeline,
    simulationGroup,
    particlePipeline,
    backgroundPipeline,
    renderGroup,
    workgroups: Math.ceil(particleCount / PARTICLES_PER_STEP)
  };
}

function writeStepParameter(bytes, index, delta, time) {
  const view = new DataView(bytes);
  const offset = index * UNIFORM_ALIGNMENT;
  view.setFloat32(offset, delta, true);
  view.setFloat32(offset + 4, time, true);
  view.setFloat32(offset + 8, 0, true);
  view.setFloat32(offset + 12, 0, true);
}

function initializeParticles() {
  const bytes = new ArrayBuffer(UNIFORM_ALIGNMENT);
  writeStepParameter(bytes, 0, 0, 0);
  device.queue.writeBuffer(scene.parameterBuffer, 0, bytes);
  const encoder = device.createCommandEncoder({ label: 'Initialize particle swarm' });
  const pass = encoder.beginComputePass();
  pass.setPipeline(scene.simulationPipeline);
  pass.setBindGroup(0, scene.simulationGroup, [0]);
  pass.dispatchWorkgroups(scene.workgroups);
  pass.end();
  device.queue.submit([encoder.finish()]);
  frameNumber = 0;
  simulationTime = 0;
}

function showError(message) {
  running = false;
  paused = false;
  cancelAnimationFrame(animationFrame);
  oceanAudio.stop();
  errorMessage.textContent = message;
  errorCard.hidden = false;
  introCard.hidden = true;
  playControls.hidden = true;
  endCard.hidden = true;
  statusText.textContent = message;
  startButton.disabled = true;
  startButton.textContent = 'Unavailable';
}

function drawFrame(now) {
  if (!running || paused || !scene) return;
  const elapsed = Math.min((now - runStartedAt) / 1000, END_SECONDS);
  const wantedFrame = Math.floor(elapsed / SIM_STEP);
  const stepCount = Math.min(MAX_CATCHUP, Math.max(0, wantedFrame - frameNumber));
  const parameterBytes = new ArrayBuffer(UNIFORM_ALIGNMENT * MAX_CATCHUP * 2);
  for (let step = 0; step < stepCount; step++) {
    const nextTime = (frameNumber + step + 1) * SIM_STEP;
    writeStepParameter(parameterBytes, step * 2, SIM_STEP, nextTime);
    writeStepParameter(parameterBytes, step * 2 + 1, -SIM_STEP, nextTime);
  }
  if (stepCount) device.queue.writeBuffer(scene.parameterBuffer, 0, parameterBytes);

  const nextFrame = frameNumber + stepCount;
  simulationTime = nextFrame * SIM_STEP;
  device.queue.writeBuffer(scene.frameBuffer, 0, new Float32Array([simulationTime, canvas.width, canvas.height, 0]));
  const encoder = device.createCommandEncoder({ label: 'Plankton frame' });
  for (let step = 0; step < stepCount; step++) {
    const velocityOffset = (step * 2) * UNIFORM_ALIGNMENT;
    const positionOffset = velocityOffset + UNIFORM_ALIGNMENT;
    const velocityPass = encoder.beginComputePass({ label: 'Update particle velocities' });
    velocityPass.setPipeline(scene.simulationPipeline);
    velocityPass.setBindGroup(0, scene.simulationGroup, [velocityOffset]);
    velocityPass.dispatchWorkgroups(scene.workgroups);
    velocityPass.end();
    const positionPass = encoder.beginComputePass({ label: 'Integrate particle positions' });
    positionPass.setPipeline(scene.simulationPipeline);
    positionPass.setBindGroup(0, scene.simulationGroup, [positionOffset]);
    positionPass.dispatchWorkgroups(scene.workgroups);
    positionPass.end();
  }

  const target = context.getCurrentTexture().createView();
  const renderPass = encoder.beginRenderPass({
    label: 'Water and plankton',
    colorAttachments: [{
      view: target,
      clearValue: { r: 0, g: 0, b: 0, a: 1 },
      loadOp: 'clear',
      storeOp: 'store'
    }]
  });
  renderPass.setPipeline(scene.backgroundPipeline);
  renderPass.setBindGroup(0, scene.renderGroup);
  renderPass.draw(3);
  renderPass.setPipeline(scene.particlePipeline);
  renderPass.setBindGroup(0, scene.renderGroup);
  renderPass.draw(6, scene.particleCount);
  renderPass.end();
  device.queue.submit([encoder.finish()]);

  frameNumber = nextFrame;
  progressBar.style.width = `${Math.min(100, simulationTime / END_SECONDS * 100)}%`;
  if (simulationTime >= END_SECONDS) {
    finishRun();
    return;
  }
  animationFrame = requestAnimationFrame(drawFrame);
}

function finishRun() {
  running = false;
  paused = false;
  oceanAudio.stop();
  playControls.hidden = true;
  endCard.hidden = false;
}

function pauseRun() {
  if (!running || paused) return;
  paused = true;
  pausedAt = performance.now();
  cancelAnimationFrame(animationFrame);
  oceanAudio.stop();
  pauseButton.textContent = 'Resume';
  pauseButton.setAttribute('aria-pressed', 'true');
}

function resumeRun() {
  if (!running || !paused) return;
  runStartedAt += performance.now() - pausedAt;
  paused = false;
  oceanAudio.play(simulationTime);
  pauseButton.textContent = 'Pause';
  pauseButton.setAttribute('aria-pressed', 'false');
  animationFrame = requestAnimationFrame(drawFrame);
}

async function startRun() {
  startButton.disabled = true;
  startButton.textContent = 'Preparing the intro…';
  statusText.textContent = 'Unlocking audio and compiling WebGPU shaders…';

  // Start the browser audio permission request directly in the user gesture.
  const audioReady = oceanAudio.prepare().catch((error) => {
    statusText.textContent = `Sound is unavailable; continuing silently. ${error.message}`;
    return false;
  });
  try {
    if (!device) await initializeGPU();
    const particleCount = Number(qualitySelect.value);
    if (!scene || scene.particleCount !== particleCount) await buildScene(particleCount);
    const soundAvailable = await audioReady;
    if (!soundAvailable) {
      oceanAudio.setMuted(true);
      soundButton.textContent = 'Sound off';
      soundButton.setAttribute('aria-pressed', 'false');
    }
    initializeParticles();
    introCard.hidden = true;
    errorCard.hidden = true;
    endCard.hidden = true;
    playControls.hidden = false;
    progressBar.style.width = '0%';
    frameNumber = 0;
    simulationTime = 0;
    running = true;
    paused = false;
    runStartedAt = performance.now();
    oceanAudio.play(0);
    pauseButton.textContent = 'Pause';
    pauseButton.setAttribute('aria-pressed', 'false');
    animationFrame = requestAnimationFrame(drawFrame);
  } catch (error) {
    console.error(error);
    showError(error?.message || 'The intro could not initialize the GPU.');
  }
}

function toggleSound() {
  const muted = !oceanAudio.muted;
  oceanAudio.setMuted(muted);
  soundButton.textContent = muted ? 'Sound off' : 'Sound on';
  soundButton.setAttribute('aria-pressed', String(!muted));
}

async function toggleFullscreen() {
  try {
    if (!document.fullscreenElement) await document.documentElement.requestFullscreen();
    else await document.exitFullscreen();
  } catch (error) {
    statusText.textContent = `Fullscreen is unavailable: ${error.message}`;
  }
}

startButton.addEventListener('click', startRun);
replayButton.addEventListener('click', startRun);
soundButton.addEventListener('click', toggleSound);
fullscreenButton.addEventListener('click', toggleFullscreen);
pauseButton.addEventListener('click', () => paused ? resumeRun() : pauseRun());

document.addEventListener('visibilitychange', () => {
  if (document.hidden) pauseRun();
});

window.addEventListener('keydown', (event) => {
  if (event.target instanceof HTMLSelectElement || event.target instanceof HTMLInputElement) return;
  if (event.code === 'Space' && running) {
    event.preventDefault();
    paused ? resumeRun() : pauseRun();
  } else if (event.key.toLowerCase() === 'm') {
    toggleSound();
  } else if (event.key.toLowerCase() === 'f') {
    void toggleFullscreen();
  }
});

initializeGPU().then(() => {
  startButton.disabled = false;
  startButton.textContent = 'Begin the descent';
  statusText.textContent = 'WebGPU ready · sound begins when you press play';
}).catch((error) => {
  showError(error?.message || 'WebGPU could not initialize.');
});
