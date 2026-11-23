// Simple recursive-descent expression parser for the Field Calculator.
// Evaluates DIMENSIONS ONLY on AST nodes (propagating [kg,m,s,K,mol,A,cd]).
// Also supports basic scalar evaluation for float preview over a sample point.
// Mirrors the Rust rhai integration.

import type { DimVec } from './types';
import type { FieldVar } from './types';
import { FIELD_VARS } from './cfd-data';
import {
  mulDim, divDim, sqrtDim, powDim,
  dotDim, magDim, magSqDim, gradDim, divDimVec, curlDimVec, crossDim,
  dimEqual, dimToHuman, suggestScaleFactor, DIMLESS,
} from './si-units';

export type Node =
  | { type: 'num'; value: number }
  | { type: 'var'; name: string }
  | { type: 'unary'; op: '-' | '+'; a: Node }
  | { type: 'bin'; op: '+' | '-' | '*' | '/' | '^'; a: Node; b: Node }
  | { type: 'call'; name: string; args: Node[] };

export interface TypeEnv {
  vars: Record<string, FieldVar>;
}

export interface TypeError {
  message: string;
  suggestion?: string;
}

export interface TypeResult {
  dim: DimVec;
  errors: TypeError[];
  // For scalar evaluation preview
  eval: (values: Record<string, number>) => number;
}

// Tokenizer
type Tok =
  | { k: 'num'; v: number }
  | { k: 'ident'; v: string }
  | { k: 'op'; v: string }
  | { k: 'lp' } | { k: 'rp' }
  | { k: 'comma' }
  | { k: 'eof' };

function tokenize(src: string): Tok[] {
  const tokens: Tok[] = [];
  let i = 0;
  while (i < src.length) {
    const c = src[i];
    if (/\s/.test(c)) { i++; continue; }
    if (/[0-9.]/.test(c)) {
      let j = i;
      while (j < src.length && /[0-9.eE+\-]/.test(src[j])) {
        // handle exponent sign only after e/E
        if ((src[j] === '+' || src[j] === '-') && !/eE/.test(src[j - 1] || '')) break;
        j++;
      }
      const num = parseFloat(src.slice(i, j));
      tokens.push({ k: 'num', v: num });
      i = j;
      continue;
    }
    if (/[a-zA-Z_]/.test(c)) {
      let j = i;
      while (j < src.length && /[a-zA-Z0-9_]/.test(src[j])) j++;
      tokens.push({ k: 'ident', v: src.slice(i, j) });
      i = j;
      continue;
    }
    if ('+-*/^'.includes(c)) { tokens.push({ k: 'op', v: c }); i++; continue; }
    if (c === '(') { tokens.push({ k: 'lp' }); i++; continue; }
    if (c === ')') { tokens.push({ k: 'rp' }); i++; continue; }
    if (c === ',') { tokens.push({ k: 'comma' }); i++; continue; }
    throw new Error(`Unexpected character '${c}' at position ${i}`);
  }
  tokens.push({ k: 'eof' });
  return tokens;
}

// Parser (standard precedence climbing)
class Parser {
  toks: Tok[];
  pos: number = 0;
  constructor(toks: Tok[]) { this.toks = toks; }
  peek() { return this.toks[this.pos]; }
  eat(): Tok { return this.toks[this.pos++]; }

  parseExpr(): Node { return this.parseAddSub(); }
  parseAddSub(): Node {
    let left = this.parseMulDiv();
    while (this.peek().k === 'op' && (this.peek() as any).v === '+' || (this.peek().k === 'op' && (this.peek() as any).v === '-')) {
      const op = (this.eat() as any).v as '+' | '-';
      const right = this.parseMulDiv();
      left = { type: 'bin', op, a: left, b: right };
    }
    return left;
  }
  parseMulDiv(): Node {
    let left = this.parseUnary();
    while (this.peek().k === 'op' && ('*/'.includes((this.peek() as any).v))) {
      const op = (this.eat() as any).v as '*' | '/';
      const right = this.parseUnary();
      left = { type: 'bin', op, a: left, b: right };
    }
    return left;
  }
  parseUnary(): Node {
    if (this.peek().k === 'op' && ((this.peek() as any).v === '-' || (this.peek() as any).v === '+')) {
      const op = (this.eat() as any).v as '-' | '+';
      return { type: 'unary', op, a: this.parseUnary() };
    }
    return this.parsePow();
  }
  parsePow(): Node {
    let left = this.parseAtom();
    while (this.peek().k === 'op' && (this.peek() as any).v === '^') {
      this.eat();
      const right = this.parseUnary(); // right-assoc
      left = { type: 'bin', op: '^', a: left, b: right };
    }
    return left;
  }
  parseAtom(): Node {
    const t = this.peek();
    if (t.k === 'num') { this.eat(); return { type: 'num', value: t.v }; }
    if (t.k === 'lp') {
      this.eat();
      const e = this.parseExpr();
      if (this.peek().k !== 'rp') throw new Error('expected )');
      this.eat();
      return e;
    }
    if (t.k === 'ident') {
      this.eat();
      if (this.peek().k === 'lp') {
        this.eat();
        const args: Node[] = [];
        if (this.peek().k !== 'rp') {
          args.push(this.parseExpr());
          while (this.peek().k === 'comma') {
            this.eat();
            args.push(this.parseExpr());
          }
        }
        if (this.peek().k !== 'rp') throw new Error('expected ) after args');
        this.eat();
        return { type: 'call', name: t.v, args };
      }
      return { type: 'var', name: t.v };
    }
    throw new Error(`Unexpected token ${JSON.stringify(t)}`);
  }
}

export function parseExpr(src: string): Node {
  const tokens = tokenize(src);
  const p = new Parser(tokens);
  const n = p.parseExpr();
  if (p.peek().k !== 'eof') throw new Error(`Unexpected token at position ${p.pos}`);
  return n;
}

// Build default env with known variables plus physical constants as dimensionless (with unit scaling encoded)
export function defaultEnv(): TypeEnv {
  const vars: Record<string, FieldVar> = {};
  for (const v of FIELD_VARS) vars[v.name] = v;
  // Physical constants (dimensionless in SI base units)
  const addConst = (name: string, _val: number, desc: string) => {
    vars[name] = { name, label: name, dim: DIMLESS, description: desc };
  };
  addConst('gamma', 1.4, 'Ratio of specific heats γ for air');
  addConst('R_air', 287.05, 'Specific gas constant R_air [J/(kg·K)]');
  addConst('kappa_vk', 0.41, 'Von Kármán constant κ');
  addConst('E_log', 9.793, 'Log-law intercept E');
  return { vars };
}

// Return the dim of each argument (for vector-argument functions: args represent vectors
// but in this scalar-field DSL we treat U_x etc., and vector functions like dot(U,V) accept vectors
// represented as components — we'll simply model dot/cross/mag as scalar-on-scalar with
// appropriate dim arithmetic).
// For simplicity in the web preview we treat every var as scalar. Vector functions:
//   mag(U) = sqrt(Ux^2 + Uy^2 + Uz^2)   dim: dim(U components)
//   mag_sq(U)                           dim: 2*dim(U)
//   dot(U,V)                            dim: dim(U)+dim(V)
//   cross(U,V)                          dim: dim(U)+dim(V)
//   grad(phi)  (vector of scalar grad)  dim: dim(phi)·m⁻¹
//   div(U)     (scalar)                 dim: dim(U)·m⁻¹
//   curl(U)    (vector)                 dim: dim(U)·m⁻¹
//   sqrt(x)                            dim: dim(x)/2
//   ln, log10, exp, sin, cos: dimensionless arguments → dimensionless
//   abs: same dim

function numEval(n: Node, vals: Record<string, number>): number {
  switch (n.type) {
    case 'num': return n.value;
    case 'var': {
      if (n.name in vals) return vals[n.name];
      // default scalar for constants (gamma, R_air, etc.)
      const env = defaultEnv().vars;
      if (n.name in env) {
        // physical constants are scalar dimensionless — but in our eval we still return numeric 1 for unitless? no, return actual values
        const c: any = { gamma: 1.4, R_air: 287.05, kappa_vk: 0.41, E_log: 9.793 };
        if (n.name in c) return (c as any)[n.name];
      }
      return NaN;
    }
    case 'unary': return n.op === '-' ? -numEval(n.a, vals) : numEval(n.a, vals);
    case 'bin': {
      const a = numEval(n.a, vals), b = numEval(n.b, vals);
      switch (n.op) {
        case '+': return a + b;
        case '-': return a - b;
        case '*': return a * b;
        case '/': return a / b;
        case '^': return Math.pow(a, b);
      }
    }
    case 'call': {
      const vs = n.args.map((a) => numEval(a, vals));
      switch (n.name) {
        case 'sqrt': return Math.sqrt(vs[0]);
        case 'abs': return Math.abs(vs[0]);
        case 'sin': return Math.sin(vs[0]);
        case 'cos': return Math.cos(vs[0]);
        case 'exp': return Math.exp(vs[0]);
        case 'ln': return Math.log(vs[0]);
        case 'log10': return Math.log10(vs[0]);
        // For scalar-only eval:
        case 'mag': return Math.hypot(vs[0], vs[1] ?? 0, vs[2] ?? 0);
        case 'mag_sq': { const m = Math.hypot(vs[0], vs[1] ?? 0, vs[2] ?? 0); return m * m; }
        case 'dot': return vs[0] * vs[0] + (vs[1] ?? 0) ** 2 + (vs[2] ?? 0) ** 2; // crude
        case 'cross': return vs[0]; // dummy
        case 'grad': return vs[0]; // dummy
        case 'div': return vs[0];
        case 'curl': return vs[0];
      }
      throw new Error(`Unknown function ${n.name}`);
    }
  }
}

export function checkDim(
  node: Node,
  env: TypeEnv,
  errors: TypeError[] = [],
): DimVec {
  switch (node.type) {
    case 'num':
      return [0, 0, 0, 0, 0, 0, 0]; // pure number -> dimensionless
    case 'var': {
      const v = env.vars[node.name];
      if (!v) {
        errors.push({ message: `Unknown identifier: \`${node.name}\`` });
        return [0, 0, 0, 0, 0, 0, 0];
      }
      return v.dim;
    }
    case 'unary':
      return checkDim(node.a, env, errors);
    case 'bin': {
      const da = checkDim(node.a, env, errors);
      const db = checkDim(node.b, env, errors);
      if (node.op === '+' || node.op === '-') {
        if (!dimEqual(da, db)) {
          const have = node.op === '+' ? db : da;
          const want = node.op === '+' ? da : db;
          const sug = suggestScaleFactor(have, want);
          const msg =
            `Cannot add/subtract ${dimToHuman(da)} and ${dimToHuman(db)}.\n` +
            `Left has dimension ${dimToHuman(da)}, right has ${dimToHuman(db)}.`;
          let suggestion: string | undefined;
          if (sug) {
            suggestion = `Hint: multiply the ${dimToHuman(have)} side by ${sug.label} (${dimToHuman(sug.factor)}) to make them match.`;
          }
          errors.push({ message: msg, suggestion });
        }
        return da;
      }
      if (node.op === '*') return mulDim(da, db);
      if (node.op === '/') return divDim(da, db);
      if (node.op === '^') {
        // exponent must be dimensionless, and we assume an integer-like constant
        if (node.b.type === 'num') {
          return powDim(da, node.b.value);
        } else {
          errors.push({ message: `Exponent must be a dimensionless constant (found ${dimToHuman(db)}).` });
          return da;
        }
      }
      return da;
    }
    case 'call': {
      const argDims = node.args.map((a) => checkDim(a, env, errors));
      switch (node.name) {
        case 'sqrt': return sqrtDim(argDims[0]);
        case 'abs': return argDims[0];
        case 'sin': case 'cos': case 'tan': case 'exp': case 'ln': case 'log10': {
          if (!dimEqual(argDims[0], DIMLESS)) {
            errors.push({ message: `Argument to ${node.name}() must be dimensionless (got ${dimToHuman(argDims[0])}).` });
          }
          return DIMLESS;
        }
        case 'mag': return magDim(argDims[0]);
        case 'mag_sq': return magSqDim(argDims[0]);
        case 'dot': return dotDim(argDims[0], argDims[1] ?? argDims[0]);
        case 'cross': return crossDim(argDims[0], argDims[1] ?? argDims[0]);
        case 'grad': return gradDim(argDims[0]);
        case 'div': return divDimVec(argDims[0]);
        case 'curl': return curlDimVec(argDims[0]);
        default:
          errors.push({ message: `Unknown function: ${node.name}()` });
          return DIMLESS;
      }
    }
  }
}

export function analyzeExpr(src: string): TypeResult {
  const env = defaultEnv();
  let node: Node;
  const errors: TypeError[] = [];
  try {
    node = parseExpr(src);
  } catch (e: any) {
    errors.push({ message: `Parse error: ${e.message}` });
    return {
      dim: DIMLESS,
      errors,
      eval: () => NaN,
    };
  }
  const dim = checkDim(node, env, errors);
  return {
    dim,
    errors,
    eval: (vals) => {
      try { return numEval(node, vals); } catch { return NaN; }
    },
  };
}

// Assignment form: name = expr
export interface AssignResult {
  name: string;
  expr: string;
  dim: DimVec;
  errors: TypeError[];
  eval: (values: Record<string, number>) => number;
}
export function analyzeAssignment(src: string): AssignResult | null {
  // strip whitespace
  const m = src.match(/^\s*([a-zA-Z_][a-zA-Z0-9_]*)\s*=\s*(.+)$/);
  if (!m) return null;
  const name = m[1];
  const expr = m[2];
  const res = analyzeExpr(expr);
  return { name, expr, dim: res.dim, errors: res.errors, eval: res.eval };
}
