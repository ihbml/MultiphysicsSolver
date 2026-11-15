// ----------------------------------------------------------------- GLSL ----
// Der Colormap-Teil entspricht 1:1 dem späteren WGSL: Wert normieren, optional
// in Iso-Bänder quantisieren, 1D-LUT an Texelmitte abtasten. Kein Licht, kein
// Tone-Mapping – der Legendenwert ist exakt der Bildschirmwert.

export const GLSL_MAP = /* glsl */ `
uniform sampler2D uLut;
uniform vec2 uRange;
uniform float uBands;
uniform vec3 uNanColor;
uniform float uHatch;
uniform float uPx;
vec3 mapColor(float v) {
  float t = (v - uRange.x) / max(uRange.y - uRange.x, 0.000001);
  float tc = clamp(t, 0.0, 1.0);
  if (uBands > 0.5) tc = (min(floor(tc * uBands), uBands - 1.0) + 0.5) / uBands;
  vec3 c = texture2D(uLut, vec2((tc * 255.0 + 0.5) / 256.0, 0.5)).rgb;
  if (uHatch > 0.5 && (t < 0.0 || t > 1.0)) {
    float stripe = step(0.5, fract((gl_FragCoord.x + gl_FragCoord.y) / (10.0 * uPx)));
    vec3 alt = t > 1.0 ? vec3(1.0) : vec3(0.0);
    c = mix(c, alt, stripe * 0.6);
  }
  return c;
}
`;

export const GLSL_MATCAP = /* glsl */ `
vec3 matcap(vec3 n) {
  float lit = 0.5 + 0.5 * dot(n, normalize(vec3(-0.4, 0.6, 0.7)));
  vec3 c = mix(vec3(0.12, 0.13, 0.16), vec3(0.78, 0.80, 0.84), pow(lit, 1.5));
  float spec = pow(max(dot(n, normalize(vec3(-0.35, 0.55, 0.76))), 0.0), 48.0);
  float rim = pow(1.0 - clamp(n.z, 0.0, 1.0), 2.5);
  return clamp(c + spec * 0.25 - rim * 0.10, 0.0, 1.0);
}
`;

export const SURF_VS = /* glsl */ `
attribute float aScalar;
attribute float aNan;
varying float vScalar;
varying float vNan;
varying vec3 vN;
void main() {
  vScalar = aScalar;
  vNan = aNan;
  vN = normalMatrix * normal;
  gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
}
`;

export const SURF_FS =
  GLSL_MAP +
  GLSL_MATCAP +
  /* glsl */ `
uniform float uMode;
uniform float uNanOn;
varying float vScalar;
varying float vNan;
varying vec3 vN;
void main() {
  vec3 n = normalize(vN);
  if (!gl_FrontFacing) n = -n;
  vec3 col = matcap(n);
  if (uMode > 0.5) col = mapColor(vScalar);
  if (vNan * uNanOn > 0.5) col = uNanColor;
  gl_FragColor = vec4(col, 1.0);
}
`;

export const PLAIN_VS = /* glsl */ `
varying vec3 vN;
void main() {
  vN = normalMatrix * normal;
  gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
}
`;

export const PLAIN_FS =
  GLSL_MATCAP +
  /* glsl */ `
varying vec3 vN;
void main() {
  gl_FragColor = vec4(matcap(normalize(vN)) * 0.82, 1.0);
}
`;

export const HULL_VS = /* glsl */ `
uniform float uWidth;
void main() {
  gl_Position = projectionMatrix * modelViewMatrix * vec4(position + normal * uWidth, 1.0);
}
`;

export const HULL_FS = /* glsl */ `
void main() {
  gl_FragColor = vec4(0.02, 0.022, 0.03, 1.0);
}
`;

export const SLICE_VS = /* glsl */ `
varying vec2 vXZ;
void main() {
  vXZ = position.xz;
  gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
}
`;

// Spiegelbild von cpAt()/sliceValue() in lib/flow.ts
export const SLICE_FS =
  GLSL_MAP +
  /* glsl */ `
uniform float uField;
uniform vec4 uSec;
uniform vec4 uXf;
uniform float uNanOn;
uniform float uNanHalf;
uniform vec2 uNanCells[4];
varying vec2 vXZ;

vec2 cmul(vec2 a, vec2 b) { return vec2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x); }
vec2 cdiv(vec2 a, vec2 b) { return vec2(a.x * b.x + a.y * b.y, a.y * b.x - a.x * b.y) / max(dot(b, b), 0.000000000001); }
vec2 csqrt(vec2 z) {
  float r = sqrt(length(z));
  float a = atan(z.y, z.x) * 0.5;
  return r * vec2(cos(a), sin(a));
}

float cpAt(vec2 z, out float inside) {
  vec2 zc = vec2(-uSec.x, uSec.y);
  float R = uSec.z;
  float beta = uSec.w;
  float alpha = uXf.w;
  vec2 sq = csqrt(cmul(z, z) - vec2(4.0, 0.0));
  vec2 z1 = 0.5 * (z + sq);
  vec2 z2 = 0.5 * (z - sq);
  vec2 zeta = z1;
  if (length(z2 - zc) > length(z1 - zc)) zeta = z2;
  vec2 d = zeta - zc;
  inside = 0.0;
  if (length(d) < R - 0.0001) inside = 1.0;
  vec2 ea = vec2(cos(alpha), sin(alpha));
  vec2 w = vec2(ea.x, -ea.y) - R * R * cdiv(ea, cmul(d, d)) + cdiv(vec2(0.0, 2.0 * R * sin(alpha + beta)), d);
  vec2 dz = vec2(1.0, 0.0) - cdiv(vec2(1.0, 0.0), cmul(zeta, zeta));
  float sp = min(length(w) / max(length(dz), 0.0001), 8.0);
  return 1.0 - sp * sp;
}

void main() {
  vec2 z = vec2(vXZ.x * uXf.y + vXZ.y * uXf.z, -vXZ.x * uXf.z + vXZ.y * uXf.y) / uXf.x;
  float inside;
  float cp = cpAt(z, inside);
  if (inside > 0.5) discard;
  float v = cp;
  if (uField > 0.5 && uField < 1.5) {
    v = sqrt(max(1.0 - cp, 0.0));
  } else if (uField > 1.5) {
    float h = 0.012;
    float ia;
    float ib;
    float cx = cpAt(z + vec2(h, 0.0), ia);
    float cy = cpAt(z + vec2(0.0, h), ib);
    vec2 g = vec2(cx - cp, cy - cp) / h;
    if (ia > 0.5) g.x = 0.0;
    if (ib > 0.5) g.y = 0.0;
    v = length(g) * 4.0;
  }
  vec3 col = mapColor(v);
  if (uNanOn > 0.5) {
    for (int i = 0; i < 4; i++) {
      vec2 dd = abs(vXZ - uNanCells[i]);
      if (max(dd.x, dd.y) < uNanHalf) col = uNanColor;
    }
  }
  gl_FragColor = vec4(col, 1.0);
}
`;
