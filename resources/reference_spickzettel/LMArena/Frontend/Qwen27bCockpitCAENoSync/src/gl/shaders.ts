// ─────────────────────────────────────────────────────────────────────────────
// WebGL2 (ES 3.00) shader library — web mirror of rust/src/render/shaders.wgsl.
// Phase 1 (geometry → G/ID buffers):  solid, field(LIC), line, vcol, mesh, slice
// Phase 2 (screen):                  screen-space MatCap + EDL/cavity compositor
// Reverse-Z infinite projection, camera-relative f32 translation (uTr).
// ─────────────────────────────────────────────────────────────────────────────

export const VS_STD = /* glsl */ `#version 300 es
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNrm;
layout(location=2) in float aGrp;
uniform mat4 uMVP;
uniform vec3 uTr;
out vec3 vN;
out float vGrp;
out vec3 vW;
void main(){
  vec3 w = aPos - uTr;
  vN = aNrm; vGrp = aGrp; vW = w;
  gl_Position = uMVP * vec4(w, 1.0);
}`;

export const FS_SOLID = /* glsl */ `#version 300 es
precision highp float;
in vec3 vN; in float vGrp; in vec3 vW;
out vec4 oAlb; out vec4 oNrm; out vec4 oId;
void main(){
  vec3 n = normalize(vN);
  int g = int(vGrp + 0.5);
  vec3 c;
  if      (g==0) c = vec3(0.22, 0.38, 0.72);
  else if (g==1) c = vec3(0.42, 0.56, 0.26);
  else if (g==2) c = vec3(0.24, 0.25, 0.29);
  else if (g==3) c = vec3(0.21, 0.22, 0.26);
  else if (g==4) c = vec3(0.46, 0.27, 0.58);
  else if (g==5) c = vec3(0.36, 0.37, 0.42);
  else           c = vec3(0.16, 0.17, 0.20);
  oAlb = vec4(c, 1.0);
  oNrm = vec4(n * 0.5 + 0.5, 0.0);
  oId  = vec4(float(g) / 255.0, 0.0, 0.0, 1.0);
  void(vW);
}`;

// Analytic field chunk (CPU mirror: src/fields/analytics.ts)
export const FLD = /* glsl */ `#version 300 es
uniform vec4 uDom;  // (L, H, h1, stepX)
uniform vec4 uCyl;  // (cylX, cylY, cylR, 0)
uniform float uDev;

float localTop(vec3 p){ return mix(uDom.z, uDom.y, smoothstep(uDom.w-0.04, uDom.w+0.04, p.x)); }

vec3 velField(vec3 p){
  float hs = smoothstep(uDom.w-0.04, uDom.w+0.04, p.x);
  float h = mix(uDom.z, uDom.y, hs);
  float U0 = 8.0*uDev;
  float yn = clamp(p.y/h, -0.2, 1.15);
  float u = U0*(1.0-yn*yn)*(1.0-0.16*hs);
  float v = 0.2*U0*yn*(1.0-yn);
  vec2 rc = p.xy - vec2(uDom.w+1.4, (uDom.z+uDom.y)*0.47);
  float r2 = dot(rc,rc)+1e-4;
  float G = 2.4*uDev*exp(-r2/0.36)/r2;
  u += -G*rc.y; v += G*rc.x;
  vec2 qc = p.xy - vec2(uCyl.x, uCyl.y);
  float R2 = dot(qc,qc);
  if (R2 > uCyl.z*uCyl.z*1.0816) {
    float f = (uCyl.z*uCyl.z)/(R2*R2);
    u += U0*(1.0-f*(qc.x*qc.x-qc.y*qc.y)) - U0;
    v += -U0*2.0*f*qc.x*qc.y;
  }
  float wx=(p.x-uCyl.x)*2.2, wy=(p.y-uCyl.y)*3.0;
  float w = exp(-wx*wx)*smoothstep(uCyl.x, uCyl.x+1.0, p.x)*exp(-wy*wy);
  v += 1.6*w*uDev;
  u += 0.3*uDev*sin(6.0*p.y+3.0+2.0*p.z);
  v += 0.22*uDev*sin(4.0*p.x+5.0*p.z);
  float wz = 0.4*uDev*sin(3.0*p.x+2.0*p.y)*(1.0-p.z*p.z);
  return clamp(vec3(u,v,wz), vec3(-14.0,-10.0,-2.0), vec3(14.0,10.0,2.0));
}

float pGauge(vec3 p){
  vec3 u = velField(p);
  float U0 = 8.0*uDev;
  float pg = 0.5*1.204*(U0*U0*0.9 - dot(u,u));
  pg += 220.0*uDev*smoothstep(uDom.w, uDom.w+2.2, p.x);
  vec2 rc = p.xy - vec2(uDom.w+1.4, (uDom.z+uDom.y)*0.47);
  pg -= 160.0*uDev*exp(-dot(rc,rc)/0.5);
  vec2 qc = p.xy - vec2(uCyl.x, uCyl.y);
  pg += 140.0*uDev*exp(-dot(qc,qc)/0.3);
  return pg;
}

float tempF(vec3 p){
  vec3 u = velField(p);
  return 298.0 + 9.0*uDev*(1.0-min(1.0, length(u)/(9.0*uDev+1e-6)));
}

float kF(vec3 p){
  vec2 rc = p.xy - vec2(uDom.w+1.4, (uDom.z+uDom.y)*0.47);
  vec2 qc = p.xy - vec2(uCyl.x, uCyl.y);
  float e1 = exp(-dot(rc,rc)/1.6);
  float e2 = exp(-dot(qc,qc)/0.5);
  return 0.05*uDev*(0.5 + 2.4*e1 + 1.8*e2 + 0.35*p.y*p.x*0.1);
}

float omF(vec3 p){
  vec2 qc = p.xy - vec2(uCyl.x, uCyl.y);
  return 25.0*uDev*(0.3 + 1.5*exp(-dot(qc,qc)/0.7) + 0.5*smoothstep(uDom.w, uDom.w+2.0, p.x))
         * (0.4 + 0.6*abs(sin(p.x*0.7 + p.y)));
}

// τw magnitude + transport direction per patch (drives Surface-LIC)
vec3 tauW(vec3 p, int g){
  float U0 = 8.0*uDev;
  if (g==3){
    float h = localTop(p);
    float uw = U0*0.82*(1.0-pow(0.03/h, 2.0));
    float fl = (p.x > uDom.w+0.35 && p.x < uDom.w+3.4) ? -1.0 : 1.0;
    float mag = 16.0*uDev*pow(max(0.0, uw/U0), 1.6)*(fl<0.0?0.55:1.0)*(0.9+0.1*sin(p.x*2.1));
    return vec3(mag, fl, 0.0);
  }
  if (g==2){
    float uw = U0*0.62*(1.0-0.3*smoothstep(uDom.w, uDom.w+2.0, p.x));
    return vec3(15.0*uDev*pow(max(0.01,uw/U0), 1.6), 1.0, 0.0);
  }
  if (g==4){
    return vec3(1.1*uDev*(1.0-clamp((p.y-uDom.z)/0.4, 0.0, 1.0)), 0.0, 1.0);
  }
  if (g==5){
    vec2 qc = p.xy - vec2(uCyl.x, uCyl.y);
    float R = length(qc)+1e-9;
    float th = abs(atan(qc.y, qc.x));
    float sh = exp(-pow(th-0.75, 2.0)/0.5);
    float sep = max(0.03, 1.0-exp(-pow(th-1.42, 2.0)/0.015));
    float mag = 17.0*uDev*(0.25+1.15*sh)*sep;
    return vec3(mag, -qc.y/R, qc.x/R);
  }
  return vec3(0.0, 1.0, 0.0);
}

float hash12(vec2 p){
  vec3 p3 = fract(vec3(p.xyx)*0.1031);
  p3 += dot(p3, p3.yzx+33.33);
  return fract((p3.x+p3.y)*p3.z);
}
float vnoise(vec2 p){
  vec2 i = floor(p), f = fract(p);
  f = f*f*(3.0-2.0*f);
  float a=hash12(i), b=hash12(i+vec2(1,0)), c=hash12(i+vec2(0,1)), d=hash12(i+vec2(1,1));
  return mix(mix(a,b,f.x), mix(c,d,f.x), f.y);
}
float lic(vec2 q, vec2 st){
  float s = 0.0; float amp = 1.0;
  for (int i=0;i<8;i++){
    s += amp*vnoise(q*2.3 + vec2(float(i)*0.71, float(i)*0.13));
    q += st*(0.05*amp);
    amp *= 0.8;
  }
  return clamp(s/2.6, 0.0, 1.0);
}`;

// Field mode surface: unlit. Walls get Surface-LIC from τw, others ghost.
export const FS_FIELD = FLD + /* glsl */ `
precision highp float;
in vec3 vN; in float vGrp; in vec3 vW;
uniform sampler2D uLUT;
uniform int uLic;
out vec4 oAlb; out vec4 oNrm; out vec4 oId;
void main(){
  vec3 n = normalize(vN);
  int g = int(vGrp + 0.5);
  vec3 c;
  if (g==2 || g==3 || g==4 || g==5) {
    vec3 tw = tauW(vW, g);
    float mag = tw.x;
    vec2 dir = tw.yz;
    float s = 0.0;
    if (uLic == 1) {
      vec2 q2; vec2 st;
      if (g==5)      { q2 = vW.xy;           st = dir; }
      else if (g==4) { q2 = vec2(vW.y, vW.z); st = vec2(dir.y, dir.z); }
      else           { q2 = vec2(vW.x, vW.z); st = vec2(dir.x, 0.0); }
      s = lic(q2, st);
    }
    vec3 lutc = texture(uLUT, vec2(clamp(mag/17.0, 0.0, 1.0), 0.5)).rgb;
    vec3 base = mix(vec3(0.085, 0.09, 0.105), vec3(0.22, 0.23, 0.26), s*0.85);
    c = mix(base, lutc*(0.30+1.25*s), clamp(mag/5.5, 0.0, 1.0)*0.95 + 0.05);
  } else if (g==0)      c = vec3(0.10, 0.16, 0.30);
  else if (g==1)        c = vec3(0.13, 0.19, 0.11);
  else                  c = vec3(0.075, 0.08, 0.095);
  oAlb = vec4(c, 1.0);
  oNrm = vec4(n*0.5+0.5, 0.0);
  oId  = vec4(float(g)/255.0, 0.0, 0.0, 1.0);
}`;

export const VS_LINE = /* glsl */ `#version 300 es
layout(location=0) in vec3 aPos;
uniform mat4 uMVP;
uniform vec3 uTr;
void main(){ gl_Position = uMVP * vec4(aPos - uTr, 1.0); }`;

export const FS_LINE = /* glsl */ `#version 300 es
precision highp float;
uniform vec3 uColor;
out vec4 oAlb; out vec4 oNrm; out vec4 oId;
void main(){
  oAlb = vec4(uColor, 1.0);
  oNrm = vec4(0.0);
  oId  = vec4(0.0, 0.0, 1.0, 1.0); // overlay → not pickable
}`;

export const VS_VCOL = /* glsl */ `#version 300 es
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aCol;
uniform mat4 uMVP;
uniform vec3 uTr;
out vec3 vC;
void main(){ vC = aCol; gl_Position = uMVP * vec4(aPos - uTr, 1.0); }`;

export const FS_VCOL = /* glsl */ `#version 300 es
precision highp float;
in vec3 vC;
out vec4 oAlb; out vec4 oNrm; out vec4 oId;
void main(){
  oAlb = vec4(vC, 1.0);
  oNrm = vec4(0.0);
  oId  = vec4(0.0, 0.0, 1.0, 1.0);
}`;

export const VS_MESH = /* glsl */ `#version 300 es
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aCol;
layout(location=2) in float aVal;
uniform mat4 uMVP;
uniform vec3 uTr;
out vec3 vC; out float vV;
void main(){
  vC = aCol; vV = aVal;
  gl_Position = uMVP * vec4(aPos - uTr, 1.0);
}`;

export const FS_MESH = /* glsl */ `#version 300 es
precision highp float;
in vec3 vC; in float vV;
uniform float uThr;
uniform float uTime;
uniform float uNanActive;
uniform float uShowNaN;
out vec4 oAlb; out vec4 oNrm; out vec4 oId;
void main(){
  vec3 c = vC; // per-triangle-constant: flat faceting, zero interpolation
  if (vV < 0.0) {
    if (uNanActive > 0.5 && uShowNaN > 0.5) {
      float pulse = 0.5 + 0.5*sin(uTime*6.0);
      c = vec3(0.937, 0.137, 0.235)*pulse + vec3(0.24, 0.02, 0.05)*(1.0-pulse);
    } else {
      c = vec3(0.09, 0.09, 0.11);
    }
  } else if (vV < uThr) {
    c = vec3(0.075, 0.075, 0.09);
  }
  oAlb = vec4(c, 1.0);
  oNrm = vec4(0.0);
  oId  = vec4(0.0, 0.0, 1.0, 1.0);
}`;

export const VS_SLICE = /* glsl */ `#version 300 es
layout(location=0) in vec2 aPuv;
uniform mat4 uMVP;
uniform vec3 uTr;
uniform vec3 uO; // plane origin
uniform vec3 uU; // +U half-extent (world)
uniform vec3 uV; // +V half-extent (world)
out vec3 vW;
void main(){
  vec3 w = uO + uU*aPuv.x + uV*aPuv.y;
  vW = w - uTr;
  gl_Position = uMVP * vec4(vW, 1.0);
}`;

export const FS_SLICE = FLD + /* glsl */ `
precision highp float;
in vec3 vW;
uniform sampler2D uLUT;
uniform int uField;
uniform float uMin;
uniform float uMax;
uniform float uBands;
out vec4 oAlb; out vec4 oNrm; out vec4 oId;
void main(){
  // inside-domain test
  float qc2 = dot(vW.xy - vec2(uCyl.x, uCyl.y), vW.xy - vec2(uCyl.x, uCyl.y));
  if (vW.y < 0.0 || vW.y > localTop(vW) || qc2 < uCyl.z*uCyl.z) discard;
  float val;
  if (uField==0)      val = length(velField(vW));
  else if (uField==1) val = pGauge(vW);
  else if (uField==2) val = tempF(vW);
  else if (uField==3) val = kF(vW);
  else if (uField==4) val = omF(vW);
  else { vec3 tw = tauW(vW, (vW.y < uCyl.z+0.02) ? 3 : 5); val = tw.x; }
  float t = clamp((val-uMin)/(uMax-uMin), 0.0, 1.0);
  vec3 c = texture(uLUT, vec2(t, 0.5)).rgb;
  if (uBands > 1.0) {
    float f = fract(t*uBands);
    if (f < 0.035) c *= 0.55; // iso-contour isoline
  }
  // subtle 2-D flow streaks (contour-flow texture)
  vec3 u = velField(vW);
  vec2 d = normalize(u.xy + vec2(1e-4));
  float s = lic(vec2(vW.x, vW.y), d)*0.55;
  c *= (0.5 + 0.85*s);
  oAlb = vec4(c, 1.0);
  oNrm = vec4(0.0);
  oId  = vec4(0.0, 0.0, 1.0, 1.0);
}`;

export const VS_TRIAD = /* glsl */ `#version 300 es
layout(location=0) in vec2 aP;
uniform vec2 uRes;
void main(){
  vec2 c = aP*2.0/uRes - 1.0;
  gl_Position = vec4(c.x, -c.y, 0.9999, 1.0);
}`;

export const FS_TRIAD = /* glsl */ `#version 300 es
precision highp float;
uniform vec3 uCol;
out vec4 oCol;
void main(){ oCol = vec4(uCol, 1.0); }`;

export const VS_SCR = /* glsl */ `#version 300 es
layout(location=0) in vec2 aPuv;
out vec2 vUv;
void main(){ vUv = aPuv*0.5+0.5; gl_Position = vec4(aPuv, 0.0, 1.0); }`;

export const FS_SCR = /* glsl */ `#version 300 es
precision highp float;
in vec2 vUv;
uniform sampler2D uAlb;
uniform sampler2D uNrm;
uniform sampler2D uId;
uniform int uMode;   // 0 cad (process), 1 raw (unlit passthrough)
uniform int uShade;  // 0 EDL, 1 cavity, 2 flat
uniform int uSel;
uniform int uHover;
uniform vec2 uRes;
out vec4 oCol;

vec3 matcap(vec3 n){
  float h = -n.y;
  vec3 c = mix(vec3(0.05, 0.055, 0.07), vec3(0.26, 0.28, 0.34), smoothstep(-0.6, 0.25, h));
  c += vec3(0.95, 0.98, 1.0) * pow(max(dot(n, normalize(vec3(-0.45, 0.6, 0.72))), 0.0), 2.6) * 0.9;
  c += vec3(0.16, 0.35, 0.9) * pow(1.0 - max(n.z, 0.0), 3.0) * 0.3;
  return c;
}
float lum(vec3 n){ return 0.5 + 0.5*max(dot(n, vec3(0.0, 0.0, 1.0)), 0.0); }

void main(){
  vec3 alb = texture(uAlb, vUv).rgb;
  vec4 idc = texture(uId, vUv);
  float gid = round(idc.r*255.0);
  // background texels are encoded 255; overlays (lines) flag b=1
  bool overlay = idc.b > 0.5 || gid > 253.5;
  vec3 col;
  if (overlay || uMode == 1) {
    col = alb;
  } else {
    vec3 n = texture(uNrm, vUv).xyz*2.0-1.0;
    col = matcap(n) * alb;
    if (uShade == 0) {
      // Screen-space Eye-Dome Lighting (4-tap normal EDL)
      float L = lum(n);
      vec2 px = 1.5/uRes;
      float L1 = lum(texture(uNrm, vUv + vec2(px.x, 0.0)).xyz);
      float L2 = lum(texture(uNrm, vUv - vec2(px.x, 0.0)).xyz);
      float L3 = lum(texture(uNrm, vUv + vec2(0.0, px.x)).xyz);
      float L4 = lum(texture(uNrm, vUv - vec2(0.0, px.x)).xyz);
      float e = min(min(abs(L-L1), abs(L-L2)), min(abs(L-L3), abs(L-L4)));
      float edge = 1.0 - smoothstep(0.0, 0.085, e);
      col *= 1.0 - edge*0.85;
    } else if (uShade == 1) {
      float cav = pow(1.0 - abs(n.z), 2.0);
      col *= 1.0 - 0.55*cav;
    }
  }
  if (gid < 254.0) {
    if (int(gid) == uSel)      col = mix(col, vec3(0.937, 0.137, 0.235), 0.32);
    else if (int(gid) == uHover) col = mix(col, vec3(0.725, 0.961, 0.0), 0.22);
  }
  oCol = vec4(col, 1.0);
}`;
