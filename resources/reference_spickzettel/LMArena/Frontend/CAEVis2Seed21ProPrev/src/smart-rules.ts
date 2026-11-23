// Smart Spatial Rule Synthesizer — Rust module port
// Inspects a picked FacePatch and generates a topology-independent rule
// with normal cone, bbox, cylindrical radius, and STEP tag predicates.

import type { FacePatch, SpatialRule, BcType } from './types';

// If normal cone is very tight (flat face), produce a normal-based predicate.
// If patch is axis-aligned to bbox face, add a bbox wall predicate.
// If cylindrical radius is tightly bounded, add a cyl_radius_yz predicate.
// Always include a STEP tag predicate for documentation (even if redundant).

function fmt(n: number): string {
  // Clean number formatting for code
  if (Math.abs(n) < 1e-10) return '0';
  if (Math.abs(n) > 1e3 || Math.abs(n) < 1e-3) return n.toExponential(2);
  return n.toFixed(n < 1 ? 4 : 3);
}

function normalAxis(n: [number, number, number]): { axis: 'x' | 'y' | 'z'; sign: 1 | -1; value: number } | null {
  const ax: Array<'x' | 'y' | 'z'> = ['x', 'y', 'z'];
  for (let i = 0; i < 3; i++) {
    const v = n[i];
    if (Math.abs(Math.abs(v) - 1) < 0.02) {
      return { axis: ax[i], sign: v > 0 ? 1 : -1, value: v };
    }
  }
  return null;
}

function onBboxFace(patch: FacePatch): { axis: 'x' | 'y' | 'z'; side: 'min' | 'max'; faceValue: number } | null {
  const { centroid, bbox } = patch;
  const axes: Array<'x' | 'y' | 'z'> = ['x', 'y', 'z'];
  for (let i = 0; i < 3; i++) {
    const c = centroid[i];
    if (Math.abs(c - bbox.min[i]) < 1e-4) return { axis: axes[i], side: 'min', faceValue: bbox.min[i] };
    if (Math.abs(c - bbox.max[i]) < 1e-4) return { axis: axes[i], side: 'max', faceValue: bbox.max[i] };
  }
  return null;
}

export function synthesizeRule(patch: FacePatch): Omit<SpatialRule, 'id' | 'matchedFaceIds' | 'conflicting'> {
  const clauses: string[] = [];
  const human: string[] = [];

  // 1) Normal cone predicate
  const axInfo = normalAxis(patch.normal);
  if (axInfo && patch.normalConeHalfAngle < 0.05) {
    const thr = 0.995;
    if (axInfo.sign === -1) {
      clauses.push(`normal.${axInfo.axis} < -${fmt(thr)}`);
      human.push(`faces −${axInfo.axis.toUpperCase()} (n̂·ê_${axInfo.axis} < −${fmt(thr)})`);
    } else {
      clauses.push(`normal.${axInfo.axis} > ${fmt(thr)}`);
      human.push(`faces +${axInfo.axis.toUpperCase()} (n̂·ê_${axInfo.axis} > ${fmt(thr)})`);
    }
  } else {
    clauses.push(`cone_contains(normal, [${patch.normal.map(fmt).join(',')}], ${fmt(patch.normalConeHalfAngle)})`);
    human.push(`normal in cone(${(patch.normalConeHalfAngle * 57.3).toFixed(1)}°) around [${patch.normal.map(fmt).join(',')}]`);
  }

  // 2) BBox face alignment
  const bf = onBboxFace(patch);
  if (bf) {
    const faceVal = bf.faceValue;
    clauses.push(`abs(${bf.axis} - ${fmt(faceVal)}) < 1e-4`);
    human.push(`lies on domain ${bf.axis}${bf.side === 'min' ? 'ₘᵢₙ' : 'ₘₐₓ'} = ${fmt(faceVal)}`);
  }

  // 3) Cylindrical radius (hub/shroud detect)
  if (patch.cylRadius > 0 && patch.cylRadius < 1.0 && patch.area > 0) {
    const tol = 0.002;
    clauses.push(`cyl_radius_yz(0,0) in [${fmt(patch.cylRadius - tol)}, ${fmt(patch.cylRadius + tol)}]`);
    human.push(`cylindrical radius r_yz ∈ [${fmt(patch.cylRadius - tol)}, ${fmt(patch.cylRadius + tol)}] m`);
  }

  // 4) STEP tag — always, as fallback/tag
  clauses.push(`step_tag == "${patch.stepTag}"`);
  human.push(`STEP tag: ${patch.stepTag}`);

  // Determine a sensible BC type
  let bcType: BcType = 'wall';
  if (axInfo) {
    if (patch.stepTag.includes('INLET')) bcType = 'inlet';
    else if (patch.stepTag.includes('OUTLET')) bcType = 'outlet';
    else if (patch.stepTag.includes('FAR')) bcType = 'farfield';
    else if (patch.stepTag.includes('SYMM')) bcType = 'symmetry';
    else if (patch.stepTag.includes('HUB') || patch.stepTag.includes('SHROUD')) bcType = 'wall';
    else if (bf) bcType = 'wall';
  }

  const expr = clauses.join('  and  ');
  const humanReadable = human.join('\n    ∧ ');

  // Colour by BC type defaults
  const colorByBc: Record<BcType, string> = {
    wall: '#9AA0A6',
    inlet: '#22C55E',
    outlet: '#F59E0B',
    farfield: '#60A5FA',
    symmetry: '#A78BFA',
    mrf: '#004AFD',
  };

  // But the "Electric Blue" highlight is used when a rule matches; red for conflicts
  // Default label
  const label =
    bcType === 'inlet' ? 'Velocity Inlet'
    : bcType === 'outlet' ? 'Pressure Outlet'
    : bcType === 'farfield' ? 'Farfield'
    : bcType === 'symmetry' ? 'Symmetry Plane'
    : patch.stepTag.includes('BLADE') ? 'Airfoil Wall'
    : patch.stepTag.includes('STEP') ? 'Step Wall'
    : patch.stepTag.includes('HUB') ? 'Hub Wall'
    : 'Wall';

  return {
    label,
    expr,
    humanReadable,
    bcType,
    color: colorByBc[bcType],
  };
}

// Create a new rule (with a fresh id) for a picked patch
let ruleCounter = 0;
export function createRuleFromPatch(patch: FacePatch): SpatialRule {
  ruleCounter += 1;
  const synth = synthesizeRule(patch);
  return {
    id: `rule_${Date.now().toString(36)}_${ruleCounter}`,
    ...synth,
    matchedFaceIds: [patch.id],
    conflicting: false,
  };
}
