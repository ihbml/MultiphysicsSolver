// ─────────────────────────────────────────────────────────────────────────────
// HydroCore WGSL — production shaders for the two-phase pipeline.
// WebGL2 mirror: src/gl/shaders.ts  ·  CPU field mirror: src/fields/analytics.ts
// ─────────────────────────────────────────────────────────────────────────────

struct Cam {
  mvp: mat4x4<f32>,
  cam_rel_hi: vec3<f32>,   // f64 domain center, high part
  cam_rel_lo: vec3<f32>,   // residual  (f64 → f32 split translation)
  res: vec2<f32>,
};
struct Field {
  dom:  vec4<f32>,  // (L, H, h1, stepX)
  cyl:  vec4<f32>,  // (cylX, cylY, cylR, 0)
  dev:  f32,        // flow development factor 0.35 → 1.0
  min:  f32,
  max:  f32,
  bands: f32,       // 0 = continuous, N = Ansys contour banding
  field: u32,       // 0 |U|, 1 p_rgh, 2 T, 3 k, 4 ω, 5 τw
  nan_active: f32,
  show_nan:   f32,
  thr:    f32,      // threshold isolator
  time:   f32,
  sel:    vec2<f32>,// (selected, hovered) face_group_id
  shade:  f32,      // 0 EDL, 1 cavity, 2 flat
  mode:   f32,      // 0 cad, 1 unlit raw
  lic:    f32,
};

// ── analytic multiphysics model (mirrors the CPU integration kernel) ────────
fn local_top(p: vec3<f32>, f: Field) -> f32 {
  return mix(f.dom.z, f.dom.y, smoothstep(f.dom.w - 0.04, f.dom.w + 0.04, p.x));
}

fn vel_field(p: vec3<f32>, f: Field) -> vec3<f32> {
  let hs = smoothstep(f.dom.w - 0.04, f.dom.w + 0.04, p.x);
  let h  = mix(f.dom.z, f.dom.y, hs);
  let u0 = 8.0 * f.dev;
  let yn = clamp(p.y / h, -0.2, 1.15);
  var u = u0 * (1.0 - yn * yn) * (1.0 - 0.16 * hs);
  var v = 0.2 * u0 * yn * (1.0 - yn);
  let rc = p.xy - vec2<f32>(f.dom.w + 1.4, (f.dom.z + f.dom.y) * 0.47);
  let r2 = dot(rc, rc) + 1e-4;
  let g  = 2.4 * f.dev * exp(-r2 / 0.36) / r2;
  u += -g * rc.y;
  v +=  g * rc.x;
  let qc = p.xy - vec2<f32>(f.cyl.x, f.cyl.y);
  let r2c = dot(qc, qc);
  if (r2c > f.cyl.z * f.cyl.z * 1.0816) {
    let k = (f.cyl.z * f.cyl.z) / (r2c * r2c);
    u += u0 * (1.0 - k * (qc.x * qc.x - qc.y * qc.y)) - u0;
    v += -u0 * 2.0 * k * qc.x * qc.y;
  }
  let wx = (p.x - f.cyl.x) * 2.2;
  let wy = (p.y - f.cyl.y) * 3.0;
  v += 1.6 * exp(-wx * wx) * smoothstep(f.cyl.x, f.cyl.x + 1.0, p.x) * exp(-wy * wy) * f.dev;
  u += 0.30 * f.dev * sin(6.0 * p.y + 3.0 + 2.0 * p.z);
  v += 0.22 * f.dev * sin(4.0 * p.x + 5.0 * p.z);
  let wz = 0.4 * f.dev * sin(3.0 * p.x + 2.0 * p.y) * (1.0 - p.z * p.z);
  return clamp(vec3<f32>(u, v, wz), vec3<f32>(-14.0, -10.0, -2.0), vec3<f32>(14.0, 10.0, 2.0));
}

fn p_gauge(p: vec3<f32>, f: Field) -> f32 {
  let u  = vel_field(p, f);
  let u0 = 8.0 * f.dev;
  var pg = 0.5 * 1.204 * (u0 * u0 * 0.9 - dot(u, u));
  pg += 220.0 * f.dev * smoothstep(f.dom.w, f.dom.w + 2.2, p.x);
  let rc = p.xy - vec2<f32>(f.dom.w + 1.4, (f.dom.z + f.dom.y) * 0.47);
  pg -= 160.0 * f.dev * exp(-dot(rc, rc) / 0.5);
  let qc = p.xy - vec2<f32>(f.cyl.x, f.cyl.y);
  pg += 140.0 * f.dev * exp(-dot(qc, qc) / 0.3);
  return pg;
}

// τw: (magnitude, transport dir xz/xy) — drives Surface-LIC streak orientation;
// mag → 0 marks separation lines (step face, bottom-wall reattachment, ±81.5° ring)
fn tau_w(p: vec3<f32>, f: Field, g: u32) -> vec3<f32> {
  let u0 = 8.0 * f.dev;
  switch g {
    case 3u: {
      let h  = local_top(p, f);
      let uw = u0 * 0.82 * (1.0 - (0.03 / h) * (0.03 / h));
      let fl = select(-1.0, 1.0, p.x > f.dom.w + 0.35 && p.x < f.dom.w + 3.4);
      let mag = 16.0 * f.dev * pow(max(0.0, uw / u0), 1.6) * select(0.55, 1.0, fl < 0.0)
               * (0.9 + 0.1 * sin(p.x * 2.1));
      return vec3<f32>(mag, fl, 0.0);
    }
    case 2u: {
      let uw = u0 * 0.62 * (1.0 - 0.3 * smoothstep(f.dom.w, f.dom.w + 2.0, p.x));
      return vec3<f32>(15.0 * f.dev * pow(max(0.01, uw / u0), 1.6), 1.0, 0.0);
    }
    case 4u: {
      return vec3<f32>(1.1 * f.dev * (1.0 - clamp((p.y - f.dom.z) / 0.4, 0.0, 1.0)), 0.0, 1.0);
    }
    case 5u: {
      let qc = p.xy - vec2<f32>(f.cyl.x, f.cyl.y);
      let r  = length(qc) + 1e-9;
      let th = abs(atan(qc.y, qc.x));
      let sh = exp(-(th - 0.75) * (th - 0.75) / 0.5);
      let sep = max(0.03, 1.0 - exp(-(th - 1.42) * (th - 1.42) / 0.015));
      return vec3<f32>(17.0 * f.dev * (0.25 + 1.15 * sh) * sep, -qc.y / r, qc.x / r);
    }
    default: { return vec3<f32>(0.0, 1.0, 0.0); }
  }
}

fn hash12(p: vec2<f32>) -> f32 {
  var p3 = fract(vec3<f32>(p.xyx) * 0.1031);
  p3 += dot(p3, p3.yzx) + 33.33;
  return fract((p3.x + p3.y) * p3.z);
}
fn vnoise(p: vec2<f32>) -> f32 {
  let i = floor(p);
  var q = fract(p);
  q = q * q * (3.0 - 2.0 * q);
  let a = hash12(i), b = hash12(i + vec2<f32>(1.0, 0.0));
  let c = hash12(i + vec2<f32>(0.0, 1.0)), d = hash12(i + vec2<f32>(1.0, 1.0));
  return mix(mix(a, b, q.x), mix(c, d, q.x), q.y);
}
// 8-tap forward accumulation along the transport direction → streak field
fn lic_streak(q: vec2<f32>, st: vec2<f32>) -> f32 {
  var s = 0.0;
  var amp = 1.0;
  for (var i = 0; i < 8; i++) {
    s += amp * vnoise(q * 2.3 + vec2<f32>(f32(i) * 0.71, f32(i) * 0.13));
    q += st * (0.05 * amp);
    amp *= 0.8;
  }
  return clamp(s / 2.6, 0.0, 1.0);
}

// ── binding groups ───────────────────────────────────────────────────────────
@group(0) @binding(0) var<uniform> cam: Cam;
@group(1) @binding(0) var<uniform> fld: Field;
@group(1) @binding(1) var lut: binding_texture;          // 256×1 Rgba8Unorm Oklab
@group(1) @binding(2) var t_alb: texture_2d<f32>;
@group(1) @binding(3) var t_nrm: texture_2d<f32>;
@group(1) @binding(4) var t_id:  texture_2d<f32>;
@group(1) @binding(5) var s_lin: sampler;
@group(2) @binding(0) var<uniform> u_color: vec4<f32>;   // line/defect edge color

// ── vertex stages ────────────────────────────────────────────────────────────
struct VSolid {
  @location(0) pos: vec3<f32>,
  @location(1) nrm: vec3<f32>,
  @location(2) grp: f32,
};
struct VOut {
  @builtin(position) clip: vec4<f32>,
  @location(0) n: vec3<f32>,
  @location(1) g: f32,
  @location(2) w: vec3<f32>,
  @location(3) col: vec3<f32>,
  @location(4) val: f32,
};

// Camera-relative translation: aPos (f32, small after import-time centering)
// minus the hi/lo split f64 center → no catastrophic cancellation at 10 m.
@vertex
fn vs_main(a: VSolid) -> VOut {
  let w = a.pos - (cam.cam_rel_hi + cam.cam_rel_lo);
  var o: VOut;
  o.clip = cam.mvp * vec4<f32>(w, 1.0);
  o.n = a.nrm;
  o.g = a.grp;
  o.w = w;
  o.col = vec3<f32>(0.0);
  o.val = 0.0;
  return o;
}
@vertex
fn vs_line(a: VSolid) -> VOut {
  let w = a.pos - (cam.cam_rel_hi + cam.cam_rel_lo);
  var o: VOut;
  o.clip = cam.mvp * vec4<f32>(w, 1.0);
  o.n = vec3<f32>(0.0); o.g = 0.0; o.w = w; o.col = vec3<f32>(0.0); o.val = 0.0;
  return o;
}
@vertex
fn vs_mesh(a: VSolid, b: VSolid) -> VOut {
  let w = a.pos - (cam.cam_rel_hi + cam.cam_rel_lo);
  var o: VOut;
  o.clip = cam.mvp * vec4<f32>(w, 1.0);
  o.n = vec3<f32>(0.0);
  o.g = 0.0;
  o.w = w;
  o.col = b.nrm;   // packed per-triangle-constant color (flat-faceting)
  o.val = b.grp;   // metric value; < 0 encodes NaN
  return o;
}
@vertex
fn vs_slice(a: VSolid) -> VOut {
  // a.pos.xy = plane uv (-1..1); a.pos.z = axis (0 x, 1 y, 2 z)
  var o: VOut;
  o.clip = cam.mvp * vec4<f32>(a.pos - (cam.cam_rel_hi + cam.cam_rel_lo), 1.0);
  o.n = vec3<f32>(0.0); o.g = 0.0; o.w = a.pos; o.col = vec3<f32>(0.0); o.val = 0.0;
  return o;
}
@vertex
fn vs_scr(a: VSolid) -> VOut {
  var o: VOut;
  o.clip = vec4<f32>(a.pos.xy, 0.0, 1.0);
  o.n = vec3<f32>(0.0); o.g = 0.0; o.w = a.pos; o.col = vec3<f32>(0.0); o.val = 0.0;
  return o;
}

// ── fragment stages ──────────────────────────────────────────────────────────
struct FOut3 {
  @location(0) alb: vec4<f32>,
  @location(1) nrm: vec4<f32>,
  @location(2) id:  vec4<f32>,
};
@fragment
fn fs_solid(v: VOut) -> FOut3 {
  var o: FOut3;
  let g = u32(v.g + 0.5);
  var c: vec3<f32>;
  switch g {
    case 0u: { c = vec3<f32>(0.22, 0.38, 0.72); }
    case 1u: { c = vec3<f32>(0.42, 0.56, 0.26); }
    case 2u: { c = vec3<f32>(0.24, 0.25, 0.29); }
    case 3u: { c = vec3<f32>(0.21, 0.22, 0.26); }
    case 4u: { c = vec3<f32>(0.46, 0.27, 0.58); }
    case 5u: { c = vec3<f32>(0.36, 0.37, 0.42); }
    default: { c = vec3<f32>(0.16, 0.17, 0.20); }
  }
  o.alb = vec4<f32>(c, 1.0);
  o.nrm = vec4<f32>(normalize(v.n) * 0.5 + 0.5, 0.0);
  o.id  = vec4<f32>(f32(g) / 255.0, 0.0, 0.0, 1.0);
  return o;
}

// Mode 2 surface: strictly unlit. Walls run Surface-LIC from τw; others ghost.
@fragment
fn fs_field(v: VOut) -> FOut3 {
  var o: FOut3;
  let g = u32(v.g + 0.5);
  var c: vec3<f32>;
  if (g == 2u || g == 3u || g == 4u || g == 5u) {
    let tw  = tau_w(v.w, fld, g);
    let mag = tw.x;
    let dir = tw.yz;
    var s = 0.0;
    if (fld.lic > 0.5) {
      var q2: vec2<f32>;
      var st: vec2<f32>;
      if (g == 5u)      { q2 = v.w.xy;        st = dir; }
      else if (g == 4u) { q2 = v.w.yz;        st = vec2<f32>(dir.y, dir.z); }
      else              { q2 = v.w.xz;        st = vec2<f32>(dir.x, 0.0); }
      s = lic_streak(q2, st);
    }
    let lutc = textureSample(lut, s_lin, vec2<f32>(clamp(mag / 17.0, 0.0, 1.0), 0.5)).rgb;
    let base = mix(vec3<f32>(0.085, 0.09, 0.105), vec3<f32>(0.22, 0.23, 0.26), s * 0.85);
    c = mix(base, lutc * (0.30 + 1.25 * s), clamp(mag / 5.5, 0.0, 1.0) * 0.95 + 0.05);
  } else if (g == 0u)      { c = vec3<f32>(0.10, 0.16, 0.30); }
  else if (g == 1u)        { c = vec3<f32>(0.13, 0.19, 0.11); }
  else                     { c = vec3<f32>(0.075, 0.08, 0.095); }
  o.alb = vec4<f32>(c, 1.0);
  o.nrm = vec4<f32>(normalize(v.n) * 0.5 + 0.5, 0.0);
  o.id  = vec4<f32>(f32(g) / 255.0, 0.0, 0.0, 1.0);
  return o;
}

@fragment
fn fs_line(_v: VOut) -> FOut3 {
  var o: FOut3;
  o.alb = vec4<f32>(u_color.xyz, 1.0);
  o.nrm = vec4<f32>(0.0);
  o.id  = vec4<f32>(0.0, 0.0, 1.0, 1.0); // overlay → not pickable
  return o;
}

// location:"cell" metric — v.col is per-triangle-constant: the interpolator
// carries it unchanged, so defective / NaN cells are never smoothed.
@fragment
fn fs_mesh(v: VOut) -> FOut3 {
  var o: FOut3;
  var c = v.col;
  if (v.val < 0.0) {
    if (fld.nan_active > 0.5 && fld.show_nan > 0.5) {
      let pulse = 0.5 + 0.5 * sin(fld.time * 6.0);
      c = vec3<f32>(0.937, 0.137, 0.235) * pulse + vec3<f32>(0.24, 0.02, 0.05) * (1.0 - pulse);
    } else {
      c = vec3<f32>(0.09, 0.09, 0.11);
    }
  } else if (v.val < fld.thr) {
    c = vec3<f32>(0.075, 0.075, 0.09);
  }
  o.alb = vec4<f32>(c, 1.0);
  o.nrm = vec4<f32>(0.0);
  o.id  = vec4<f32>(0.0, 0.0, 1.0, 1.0);
  return o;
}

@fragment
fn fs_slice(v: VOut) -> FOut3 {
  var o: FOut3;
  let qc2 = dot(v.w.xy - vec2<f32>(fld.cyl.x, fld.cyl.y), v.w.xy - vec2<f32>(fld.cyl.x, fld.cyl.y));
  if (v.w.y < 0.0 || v.w.y > local_top(v.w, fld) || qc2 < fld.cyl.z * fld.cyl.z) {
    o.alb = vec4<f32>(0.0); o.nrm = vec4<f32>(0.0); o.id = vec4<f32>(0.0, 0.0, 1.0, 1.0);
    return o;
  }
  var val = 0.0;
  switch fld.field {
    case 0u: { val = length(vel_field(v.w, fld)); }
    case 1u: { val = p_gauge(v.w, fld); }
    default: { val = 0.0; } // T / k / ω / τw — same pattern as the WebGL2 mirror
  }
  let t = clamp((val - fld.min) / (fld.max - fld.min), 0.0, 1.0);
  var c = textureSample(lut, s_lin, vec2<f32>(t, 0.5)).rgb;
  if (fld.bands > 1.0) {
    if (fract(t * fld.bands) < 0.035) { c *= 0.55; } // iso-contour isoline
  }
  let d = normalize(vel_field(v.w, fld).xy + vec2<f32>(1e-4));
  c *= 0.5 + 0.85 * lic_streak(v.w.xy, d) * 0.55;
  o.alb = vec4<f32>(c, 1.0);
  o.nrm = vec4<f32>(0.0);
  o.id  = vec4<f32>(0.0, 0.0, 1.0, 1.0);
  return o;
}

// ── Phase 2: screen-space compositor ─────────────────────────────────────────
fn matcap(n: vec3<f32>) -> vec3<f32> {
  var c = mix(vec3<f32>(0.05, 0.055, 0.07), vec3<f32>(0.26, 0.28, 0.34),
              smoothstep(-0.6, 0.25, -n.y));
  c += vec3<f32>(0.95, 0.98, 1.0)
       * pow(max(dot(n, normalize(vec3<f32>(-0.45, 0.6, 0.72))), 0.0), 2.6) * 0.9;
  c += vec3<f32>(0.16, 0.35, 0.9) * pow(1.0 - max(n.z, 0.0), 3.0) * 0.3;
  return c;
}
fn lum(n: vec3<f32>) -> f32 {
  return 0.5 + 0.5 * max(dot(n, vec3<f32>(0.0, 0.0, 1.0)), 0.0);
}
@fragment
fn fs_scr(v: VOut) -> @location(0) vec4<f32> {
  let uv = v.w.xy;
  let alb = textureSample(t_alb, s_lin, uv).rgb;
  let idc = textureSample(t_id,  s_lin, uv);
  let gid = round(idc.r * 255.0);
  var col = alb;
  if (idc.b < 0.5 && fld.mode < 0.5) {
    let n = textureSample(t_nrm, s_lin, uv).xyz * 2.0 - 1.0;
    col = matcap(n) * alb;
    if (fld.shade < 0.5) {
      // screen-space Eye-Dome Lighting — 4-tap normal luminance gradient
      let l  = lum(n);
      let px = 1.5 / cam.res;
      let l1 = lum(textureSample(t_nrm, s_lin, uv + vec2<f32>(px.x, 0.0)).xyz);
      let l2 = lum(textureSample(t_nrm, s_lin, uv - vec2<f32>(px.x, 0.0)).xyz);
      let l3 = lum(textureSample(t_nrm, s_lin, uv + vec2<f32>(0.0, px.y)).xyz);
      let l4 = lum(textureSample(t_nrm, s_lin, uv - vec2<f32>(0.0, px.y)).xyz);
      let e = min(min(abs(l - l1), abs(l - l2)), min(abs(l - l3), abs(l - l4)));
      col *= 1.0 - (1.0 - smoothstep(0.0, 0.085, e)) * 0.85;
    } else if (fld.shade < 1.5) {
      let cav = (1.0 - abs(n.z)) * (1.0 - abs(n.z));
      col *= 1.0 - 0.55 * cav;
    }
  }
  if (gid > 0.0 && gid < 254.0) {
    if (abs(gid - fld.sel.x) < 0.5) {
      col = mix(col, vec3<f32>(0.937, 0.137, 0.235), 0.32);
    } else if (abs(gid - fld.sel.y) < 0.5) {
      col = mix(col, vec3<f32>(0.725, 0.961, 0.0), 0.22);
    }
  }
  return vec4<f32>(col, 1.0);
}
