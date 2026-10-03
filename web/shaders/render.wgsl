struct Particle {
  position: vec4<f32>,
  velocity: vec4<f32>,
};

struct FrameParams {
  time: f32,
  width: f32,
  height: f32,
  padding: f32,
};

@group(0) @binding(0) var<storage, read> particles: array<Particle>;
@group(0) @binding(1) var<uniform> frame: FrameParams;

struct ParticleVertex {
  @builtin(position) position: vec4<f32>,
  @location(0) uv: vec2<f32>,
  @location(1) colour: vec3<f32>,
};

struct BackgroundVertex {
  @builtin(position) position: vec4<f32>,
};

const PALETTE = array<vec3<f32>, 6>(
  vec3<f32>(0.10, 0.45, 0.50),
  vec3<f32>(0.14, 0.62, 0.68),
  vec3<f32>(0.16, 0.52, 0.30),
  vec3<f32>(0.10, 0.26, 0.62),
  vec3<f32>(0.88, 0.24, 0.18),
  vec3<f32>(0.82, 0.68, 0.20)
);

@vertex
fn background_vertex(@builtin(vertex_index) index: u32) -> BackgroundVertex {
  let positions = array<vec2<f32>, 3>(
    vec2<f32>(-1.0, -1.0),
    vec2<f32>(3.0, -1.0),
    vec2<f32>(-1.0, 3.0)
  );
  var output: BackgroundVertex;
  output.position = vec4<f32>(positions[index], 0.5, 1.0);
  return output;
}

fn smoothstep_reverse(outer: f32, inner: f32, value: f32) -> f32 {
  return 1.0 - smoothstep(inner, outer, value);
}

@fragment
fn background_fragment(@builtin(position) position: vec4<f32>) -> @location(0) vec4<f32> {
  let t = frame.time;
  let width = frame.width;
  let height = frame.height;
  let fade = min(smoothstep(0.0, 5.0, t), 1.0 - smoothstep(48.0, 56.0, t));
  let p = vec2<f32>(position.x / width, 1.0 - position.y / height);
  var q = p;
  q.x += 0.015 * sin(q.y * 14.0 + t * 1.1) + 0.008 * sin(q.y * 27.0 - t * 1.7);

  let rise = smoothstep(4.0, 40.0, t);
  let lit = p.y * (0.3 + 0.7 * rise);
  var colour = vec3<f32>(0.0, 0.03, 0.08) + vec3<f32>(0.05, 0.32, 0.37) * lit;
  let caustic = pow(abs(sin(q.x * 10.0 + sin(q.y * 8.0 + t * 0.6) * 2.0)), 4.0);
  colour += caustic * vec3<f32>(0.6, 1.0, 0.9) * 0.12 * lit;

  let sun = q - vec2<f32>(0.5, 1.15);
  let angle = atan2(sun.x, sun.y);
  let rays = pow(abs(sin(angle * 9.0 + sin(t * 0.3) * 0.4)), 6.0)
    * (0.55 + 0.45 * sin(angle * 3.7 - t * 0.13));
  colour += rays * vec3<f32>(0.9, 1.0, 0.85) * 0.9 * lit / (1.0 + dot(sun, sun) * 0.6);

  let cells = vec2<f32>(p.x * width / height, p.y);
  var bubbles = 0.0;
  for (var layer = 0; layer < 2; layer += 1) {
    let depth = f32(layer);
    let grid = cells * (6.0 + depth * 4.0) + vec2<f32>(depth * 3.7, -t);
    let index = floor(grid);
    let h = fract(sin(index.x * 12.9898 + index.y * 78.233 + depth * 37.7) * 43758.5453);
    let g = fract(h * 31.7);
    let radius = 0.07 + 0.15 * h;
    let centre = fract(grid) - vec2<f32>(0.5 + 0.18 * sin(t * 1.3 + h * 20.0), 0.15 + 0.7 * g);
    let distance = length(centre);
    let rim = smoothstep_reverse(radius, radius * 0.8, distance)
      - smoothstep_reverse(radius * 0.76, radius * 0.5, distance);
    bubbles += rim * max(h - 0.78, 0.0) * 5.0;
  }
  colour += bubbles * vec3<f32>(0.5, 0.8, 0.9) * 0.26 * (0.3 + 0.7 * rise);
  return vec4<f32>(colour * (1.0 - 0.35 * length(p - vec2<f32>(0.5))) * fade, 1.0);
}

@vertex
fn particle_vertex(
  @builtin(vertex_index) vertex_index: u32,
  @builtin(instance_index) instance_index: u32
) -> ParticleVertex {
  let corners = array<vec2<f32>, 6>(
    vec2<f32>(-1.0, -1.0), vec2<f32>(1.0, -1.0), vec2<f32>(1.0, 1.0),
    vec2<f32>(-1.0, -1.0), vec2<f32>(1.0, 1.0), vec2<f32>(-1.0, 1.0)
  );
  let uvs = array<vec2<f32>, 6>(
    vec2<f32>(0.0, 0.0), vec2<f32>(1.0, 0.0), vec2<f32>(1.0, 1.0),
    vec2<f32>(0.0, 0.0), vec2<f32>(1.0, 1.0), vec2<f32>(0.0, 1.0)
  );

  let particle = particles[instance_index];
  let t = frame.time;
  let reveal = smoothstep(30.0, 36.0, t);
  let rest = 1.0 - reveal;
  let azimuth = t * 0.13 * rest + sin(t * 0.25) * 0.045 * reveal;
  let elevation = sin(t * 0.07) * 0.5 * rest + sin(t * 0.17) * 0.03 * reveal;
  let sa = sin(azimuth);
  let ca = cos(azimuth);
  let se = sin(elevation);
  let ce = cos(elevation);
  let direction = vec3<f32>(sa * ce, se, ca * ce);
  let offset = particle.position.xyz - direction * (0.5 + 0.92 * reveal);
  let view = vec3<f32>(
    dot(offset, vec3<f32>(ca, 0.0, -sa)),
    dot(offset, vec3<f32>(-sa * se, ce, -ca * se)),
    -dot(offset, direction)
  );

  var clip = vec4<f32>(
    view.x * 1.7 * frame.height / frame.width,
    view.y * 1.7,
    view.z * 0.5,
    view.z
  );
  let point_size = clamp(0.0374 * frame.height / view.z, 1.0, 0.033 * frame.height);
  clip.x += corners[vertex_index].x * point_size / frame.width * clip.w;
  clip.y += corners[vertex_index].y * point_size / frame.height * clip.w;

  var colour = PALETTE[u32(particle.position.w)];
  let depth_absorption = clamp((view.z - 1.0) / 2.5, 0.0, 1.0) * (0.75 - 0.25 * smoothstep(4.0, 40.0, t));
  colour = mix(colour, vec3<f32>(0.02, 0.08, 0.15), depth_absorption);
  var pulse = 0.5 + 0.5 * sin(t * 1.2566 + particle.position.w * 1.0472);
  pulse *= pulse * pulse;
  colour *= 1.0 + (1.0 - 0.75 * reveal) * (2.08 * pulse - 0.65);
  colour *= 1.0 + 1.5 * length(particle.velocity.xyz);
  colour *= min(smoothstep(0.0, 5.0, t), 1.0 - smoothstep(48.0, 56.0, t));
  colour *= exp(0.275 - 0.55 * view.z);

  var output: ParticleVertex;
  output.position = clip;
  output.uv = uvs[vertex_index];
  output.colour = colour;
  return output;
}

@fragment
fn particle_fragment(input: ParticleVertex) -> @location(0) vec4<f32> {
  let offset = input.uv * 2.0 - vec2<f32>(1.0);
  let falloff = max(1.0 - dot(offset, offset), 0.0);
  let halo = falloff * falloff;
  let core = halo * halo * halo * halo;
  return vec4<f32>(input.colour * (halo * 0.04 + core * 0.5), 1.0);
}
