const PARTICLE_COUNT: u32 = __PARTICLE_COUNT__u;

struct Particle {
  position: vec4<f32>,
  velocity: vec4<f32>,
};

struct SimulationParams {
  delta: f32,
  time: f32,
  padding: vec2<f32>,
};

@group(0) @binding(0) var<storage, read_write> particles: array<Particle>;
@group(0) @binding(1) var<uniform> params: SimulationParams;

fn hash(value: f32) -> f32 {
  return fract(sin(value + 3.0 * 17.13) * 43758.5453);
}

fn attraction(a: f32, b: f32) -> f32 {
  let weight = hash(a * 7.3 + b * 13.7 + 3.1) * 2.0 - 1.0;
  if (a == b) { return 0.5 + 0.5 * abs(weight); }
  return weight;
}

fn stroke_point(stroke: u32, u: f32, jitter: f32) -> vec2<f32> {
  if (stroke < 2u) { return vec2<f32>(f32(stroke) * 0.6 + jitter, u); }
  if (stroke == 2u) { return vec2<f32>(0.6 * u, 1.0 - u) + vec2<f32>(0.857493, 0.514496) * jitter; }
  if (stroke < 5u) { return vec2<f32>(0.85 + f32(stroke - 3u) * 0.6 + jitter, 0.35 + 0.65 * u); }
  if (stroke == 5u) {
    let angle = 3.14159 + 3.1416 * u;
    return vec2<f32>(1.15, 0.35) + (0.3 + jitter) * vec2<f32>(cos(angle), sin(angle));
  }
  if (stroke == 6u) { return vec2<f32>(1.7 + 0.6 * u, 1.0 + jitter); }
  if (stroke == 7u) { return vec2<f32>(2.0 + jitter, u); }
  if (stroke == 8u) {
    let angle = 0.2618 + 4.7124 * u;
    return vec2<f32>(2.85, 0.75) + (0.25 + jitter) * vec2<f32>(cos(angle), sin(angle));
  }
  let angle = 4.7124 * u - 2.8798;
  return vec2<f32>(2.85, 0.25) + (0.25 + jitter) * vec2<f32>(cos(angle), sin(angle));
}

fn letter_target(index: f32) -> vec3<f32> {
  let local = stroke_point(
    u32(hash(index * 1.3 + 77.0) * 10.0),
    hash(index * 2.9 + 12.0),
    (hash(index * 1.9 + 41.0) - 0.5) * 0.1
  );
  return vec3<f32>((local.x - 1.575) * 0.8, (local.y - 0.5) * 0.8, 0.0);
}

@compute @workgroup_size(128)
fn main(@builtin(global_invocation_id) global_id: vec3<u32>) {
  let i = global_id.x;
  if (i >= PARTICLE_COUNT) { return; }

  let current = particles[i];
  if (params.delta == 0.0) {
    let n = f32(i);
    let direction = normalize(vec3<f32>(hash(n * 0.37), hash(n * 0.71 + 1.0), hash(n * 1.13 + 2.0)) - vec3<f32>(0.5));
    let radius = pow(hash(n * 2.3 + 5.0), 0.3333) * 0.9;
    particles[i].position = vec4<f32>(direction * radius, floor(hash(n * 3.7 + 9.0) * 6.0));
    particles[i].velocity = vec4<f32>(0.0);
    return;
  }

  if (params.delta < 0.0) {
    particles[i].position = vec4<f32>(current.position.xyz + current.velocity.xyz * -params.delta, current.position.w);
    return;
  }

  var force = vec3<f32>(0.0);
  for (var j = 0u; j < PARTICLE_COUNT; j += 1u) {
    let other = particles[j].position;
    let offset = other.xyz - current.position.xyz;
    let distance = length(offset);
    if (distance < 0.00001 || distance > 0.11) { continue; }
    let r = distance / 0.11;
    var strength: f32;
    if (r < 0.6) {
      strength = r / 0.6 - 1.0;
    } else {
      strength = attraction(current.position.w, other.w) * (1.0 - abs(2.0 * r - 1.0 - 0.6) / (1.0 - 0.6));
    }
    force += offset / distance * strength;
  }
  force *= 3.0;

  let radius = length(current.position.xyz);
  if (radius > 0.85) {
    let over = radius - 0.85;
    force -= current.position.xyz * over * (26.0 + 60.0 * over) / radius;
  }

  let reveal = smoothstep(34.0, 36.0, params.time) * (1.0 - smoothstep(44.0, 51.0, params.time));
  if (reveal > 0.001) {
    force = mix(force, (letter_target(f32(i)) - current.position.xyz) * 9.0, reveal);
  }

  let velocity = current.velocity.xyz + force * params.delta;
  particles[i].velocity = vec4<f32>(velocity * exp(-17.0 * params.delta), 0.0);
}
