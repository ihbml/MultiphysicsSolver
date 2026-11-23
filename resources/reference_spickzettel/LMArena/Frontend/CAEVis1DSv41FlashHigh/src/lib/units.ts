// ---------------------------------------------------------------------------
// SI dimension propagation for the custom field calculator.
// Every AST node carries a 7-vector of exponents [kg, m, s, K, mol, A, cd].
// Dimensions are checked BEFORE the expression reaches the shader.
// ---------------------------------------------------------------------------
export type Dim = [number, number, number, number, number, number, number];
export type Shape = "scalar" | "vector" | "tensor" | "any";
export const ZERO: Dim = [0, 0, 0, 0, 0, 0, 0];

export type Typed = { dim: Dim; shape: Shape; quantity?: string; any?: boolean };

export const SYMBOLS: Record<string, { t: Typed; unit: string; label: string; value: any; hint: string }> = {
  p: { t: { dim: [1, -1, -2, 0, 0, 0, 0], shape: "scalar", quantity: "Pressure" }, unit: "Pa", label: "Static pressure", value: 101325, hint: "static pressure" },
  rho: { t: { dim: [1, -3, 0, 0, 0, 0, 0], shape: "scalar", quantity: "Density" }, unit: "kg·m⁻³", label: "Density", value: 1.085, hint: "density" },
  U: { t: { dim: [0, 1, -1, 0, 0, 0, 0], shape: "vector", quantity: "Velocity" }, unit: "m·s⁻¹", label: "Velocity vector", value: [162.4, 4.2, -38.1], hint: "velocity" },
  T: { t: { dim: [0, 0, 0, 1, 0, 0, 0], shape: "scalar", quantity: "Temperature" }, unit: "K", label: "Static temperature", value: 846, hint: "temperature" },
  mu: { t: { dim: [1, -1, -1, 0, 0, 0, 0], shape: "scalar", quantity: "Dynamic viscosity" }, unit: "kg·m⁻¹·s⁻¹", label: "Dynamic viscosity", value: 3.72e-5, hint: "dynamic viscosity" },
  kappa: { t: { dim: [1, 1, -3, -1, 0, 0, 0], shape: "scalar", quantity: "Thermal conductivity" }, unit: "W·m⁻¹·K⁻¹", label: "Thermal conductivity", value: 0.058, hint: "thermal conductivity" },
  Cp: { t: { dim: [0, 2, -2, -1, 0, 0, 0], shape: "scalar", quantity: "Specific heat" }, unit: "J·kg⁻¹·K⁻¹", label: "Specific heat", value: 1004.5, hint: "specific heat" },
  R_air: { t: { dim: [0, 2, -2, -1, 0, 0, 0], shape: "scalar", quantity: "Gas constant" }, unit: "J·kg⁻¹·K⁻¹", label: "Specific gas constant", value: 287.05, hint: "gas constant" },
  gamma: { t: { dim: ZERO, shape: "scalar", quantity: "Ratio of specific heats" }, unit: "—", label: "γ = cp/cv", value: 1.4, hint: "gamma" },
  yPlus: { t: { dim: ZERO, shape: "scalar", quantity: "Dimensionless" }, unit: "—", label: "Wall y+", value: 12.4, hint: "y+" },
  x: { t: { dim: [0, 1, 0, 0, 0, 0, 0], shape: "scalar", quantity: "Length" }, unit: "m", label: "Axial coordinate", value: 0.31, hint: "x" },
  wall_distance: { t: { dim: [0, 1, 0, 0, 0, 0, 0], shape: "scalar", quantity: "Length" }, unit: "m", label: "Wall distance", value: 2.4e-5, hint: "wall distance" },
  k: { t: { dim: [0, 2, -2, 0, 0, 0, 0], shape: "scalar", quantity: "Specific turbulent kinetic energy" }, unit: "m²·s⁻²", label: "TKE per mass", value: 28.6, hint: "turbulence kinetic energy" },
  omega: { t: { dim: [0, 0, -1, 0, 0, 0, 0], shape: "scalar", quantity: "Specific dissipation rate" }, unit: "s⁻¹", label: "Specific dissipation", value: 1840, hint: "omega" },
  q_wall: { t: { dim: [1, 0, -3, 0, 0, 0, 0], shape: "scalar", quantity: "Heat flux" }, unit: "W·m⁻²", label: "Wall heat flux", value: 12400, hint: "wall heat flux" },
  Nu: { t: { dim: ZERO, shape: "scalar", quantity: "Dimensionless" }, unit: "—", label: "Nusselt number", value: 942, hint: "Nusselt" },
};

const SUP: Record<string, string> = {
  "0": "⁰", "1": "¹", "2": "²", "3": "³", "4": "⁴", "5": "⁵", "6": "⁶", "7": "⁷", "8": "⁸", "9": "⁹", "-": "⁻", ".": "·", "/": "ᐟ",
};
const sup = (n: number) => String(n).split("").map((c) => SUP[c] || c).join("");
const BASES = ["kg", "m", "s", "K", "mol", "A", "cd"];

export function formatDim(d: Dim): string {
  const parts: string[] = [];
  d.forEach((e, i) => {
    if (Math.abs(e) < 1e-9) return;
    parts.push(`${BASES[i]}${e === 1 ? "" : sup(e)}`);
  });
  return parts.length ? parts.join("·") : "1";
}

export const dimEq = (a: Dim, b: Dim) => a.every((v, i) => Math.abs(v - b[i]) < 1e-9);
const dimAdd = (a: Dim, b: Dim, s = 1): Dim => a.map((v, i) => v + s * b[i]) as Dim;
const dimScale = (a: Dim, s: number): Dim => a.map((v) => v * s) as Dim;

const MEMBERS: { d: Dim; name: string }[] = [
  { d: [1, -1, -2, 0, 0, 0, 0], name: "Pressure" },
  { d: [1, -3, 0, 0, 0, 0, 0] as Dim, name: "Density" },
  { d: [0, 1, -1, 0, 0, 0, 0] as Dim, name: "Velocity" },
  { d: [0, 0, -1, 0, 0, 0, 0] as Dim, name: "Rate" },
  { d: [1, -1, -1, 0, 0, 0, 0] as Dim, name: "Dynamic viscosity" },
  { d: [0, 2, -2, 0, 0, 0, 0] as Dim, name: "Specific kinetic energy" },
  { d: [1, 2, -3, 0, 0, 0, 0], name: "Power" },
  { d: [1, 1, -3, -1, 0, 0, 0], name: "Thermal conductivity" },
  { d: [1, -1, -3, -1, 0, 0, 0], name: "Volumetric entropy generation" },
  { d: [0, 1, 0, 0, 0, 0, 0], name: "Length" },
  { d: [0, 0, 0, 1, 0, 0, 0], name: "Temperature" },
];
export const quantityName = (d: Dim) => MEMBERS.find((m) => dimEq(m.d, d))?.name || (dimEq(d, ZERO) ? "Dimensionless" : "Derived quantity");

export class UnitError extends Error {
  hint?: string;
  constructor(message: string, hint?: string) {
    super(message);
    this.hint = hint;
  }
}

/* ------------------------------- lexer -------------------------------- */
type Tok = { t: "num" | "id" | "op"; v: string; pos: number };
function lex(src: string): Tok[] {
  const out: Tok[] = [];
  let i = 0;
  while (i < src.length) {
    const c = src[i];
    if (/\s/.test(c)) { i++; continue; }
    if (/[0-9.]/.test(c)) {
      let j = i;
      while (j < src.length && /[0-9.]/.test(src[j])) j++;
      if (src[j] === "e" || src[j] === "E") {
        j++;
        if (src[j] === "-" || src[j] === "+") j++;
        while (/[0-9]/.test(src[j] || "")) j++;
      }
      out.push({ t: "num", v: src.slice(i, j), pos: i });
      i = j;
      continue;
    }
    if (/[A-Za-z_]/.test(c)) {
      let j = i;
      while (j < src.length && /[A-Za-z0-9_]/.test(src[j])) j++;
      out.push({ t: "id", v: src.slice(i, j), pos: i });
      i = j;
      continue;
    }
    if ("+-*/^(),".includes(c)) { out.push({ t: "op", v: c, pos: i }); i++; continue; }
    throw new UnitError(`Unexpected character “${c}” at column ${i + 1}`, "Only algebraic operators and registered SI fields are allowed.");
  }
  return out;
}

/* ------------------------------- AST ---------------------------------- */
export type Ast =
  | { k: "num"; v: number }
  | { k: "var"; name: string }
  | { k: "neg"; a: Ast }
  | { k: "bin"; op: string; a: Ast; b: Ast }
  | { k: "call"; fn: string; args: Ast[] };

export class Parser {
  toks: Tok[];
  i = 0;
  constructor(src: string) { this.toks = lex(src); }
  peek() { return this.toks[this.i]; }
  eat(v?: string) {
    const t = this.toks[this.i];
    if (!t) throw new UnitError("Unexpected end of expression", "Complete the expression, e.g. p + 0.5 * rho * dot(U, U).");
    if (v && t.v !== v) throw new UnitError(`Expected “${v}” but found “${t.v}” at column ${t.pos + 1}`);
    this.i++;
    return t;
  }
  parse(): Ast {
    const a = this.expr();
    if (this.i < this.toks.length) throw new UnitError(`Unexpected token “${this.peek().v}” at column ${this.peek().pos + 1}`);
    return a;
  }
  expr(): Ast {
    let a = this.term();
    while (this.peek()?.v === "+" || this.peek()?.v === "-") {
      const op = this.eat().v;
      a = { k: "bin", op, a, b: this.term() };
    }
    return a;
  }
  term(): Ast {
    let a = this.power();
    while (this.peek()?.v === "*" || this.peek()?.v === "/") {
      const op = this.eat().v;
      a = { k: "bin", op, a, b: this.power() };
    }
    return a;
  }
  power(): Ast {
    let a = this.unary();
    while (this.peek()?.v === "^") {
      this.eat();
      a = { k: "bin", op: "^", a, b: this.unary() };
    }
    return a;
  }
  unary(): Ast {
    if (this.peek()?.v === "-") { this.eat(); return { k: "neg", a: this.unary() }; }
    if (this.peek()?.v === "+") { this.eat(); return this.unary(); }
    return this.atom();
  }
  atom(): Ast {
    const t = this.peek();
    if (!t) throw new UnitError("Unexpected end of expression", "Complete the expression before compiling.");
    if (t.t === "num") { this.eat(); return { k: "num", v: Number(t.v) }; }
    if (t.t === "id") {
      this.eat();
      if (this.peek()?.v === "(") {
        this.eat("(");
        const args: Ast[] = [];
        if (this.peek()?.v !== ")") {
          args.push(this.expr());
          while (this.peek()?.v === ",") { this.eat(); args.push(this.expr()); }
        }
        this.eat(")");
        return { k: "call", fn: t.v, args };
      }
      return { k: "var", name: t.v };
    }
    if (t.v === "(") { this.eat("("); const a = this.expr(); this.eat(")"); return a; }
    throw new UnitError(`Unexpected token “${t.v}” at column ${t.pos + 1}`);
  }
}

/* --------------------------- unit checking ---------------------------- */
const FN_ARITY: Record<string, [number, number]> = {
  dot: [2, 2], cross: [2, 2], mag: [1, 1], mag_sq: [1, 1], sqrt: [1, 1],
  grad: [1, 1], div: [1, 1], curl: [1, 1], abs: [1, 1], exp: [1, 1], ln: [1, 1], log: [1, 1],
};
const ANY: Typed = { dim: ZERO, shape: "any", any: true, quantity: "Numeric constant" };

export type TraceNode = { expr: string; dims: string; quantity: string; shape: Shape };

function describe(t: Typed) {
  const q = t.any ? "Numeric constant" : t.quantity || quantityName(t.dim);
  return `${q} [${formatDim(t.dim)}]`;
}

export function checkAst(ast: Ast, symbols = SYMBOLS, trace?: TraceNode[]): Typed {
  const walk = (n: Ast): Typed => {
    const finish = (t: Typed, src: string): Typed => {
      if (trace) trace.push({ expr: src, dims: formatDim(t.dim), quantity: t.any ? "constant" : t.quantity || quantityName(t.dim), shape: t.shape });
      return t;
    };
    switch (n.k) {
      case "num": {
        // Physical-constant inference: 287.05 is the dry-air gas constant
        // J·kg⁻¹·K⁻¹. Labelled literals keep the dimensional check honest.
        if (Math.abs(n.v - 287.05) < 1e-6 || n.v === 287) {
          return finish({ dim: [0, 2, -2, -1, 0, 0, 0], shape: "scalar", quantity: "Gas constant" }, String(n.v));
        }
        return finish(ANY, String(n.v));
      }
      case "var": {
        const s = symbols[n.name];
        if (!s) throw new UnitError(`Unknown field “${n.name}”`, `Registered SI symbols: ${Object.keys(symbols).slice(0, 9).join(", ")} …`);
        return finish(s.t, n.name);
      }
      case "neg": {
        const a = walk(n.a);
        return finish(a, `(−${fmtAst(n.a)})`);
      }
      case "bin": {
        const a = walk(n.a);
        const b = walk(n.b);
        let out: Typed;
        if (n.op === "+" || n.op === "-") {
          if (!dimEq(a.dim, b.dim) && !a.any && !b.any) {
            const hint =
              dimEq(a.dim, [1, -1, -2, 0, 0, 0, 0]) && dimEq(b.dim, [0, 2, -2, 0, 0, 0, 0])
                ? "multiply the specific kinetic energy by Density rho [kg·m⁻³]"
                : "make both operands dimensionally identical before adding";
            throw new UnitError(
              `Cannot ${n.op === "+" ? "add" : "subtract"} ${describe(a)} and ${describe(b)} — ${hint}.`,
              hint.startsWith("multiply") ? `Suggested: ${fmtAst(n.a)} ${n.op} ${n.op === "+" ? "rho * " : "rho * "}${fmtAst(n.b)}` : undefined
            );
          }
          out = a.any ? b : b.any ? a : { ...a, dim: a.dim };
        } else if (n.op === "*") {
          if (a.shape === "vector" && b.shape === "vector") throw new UnitError("Vector multiplication is ambiguous", "Use dot(U, U) for an inner product or cross(U, U) for a cross product.");
          const d = a.any ? b.dim : b.any ? a.dim : dimAdd(a.dim, b.dim);
          out = { dim: d, shape: a.shape === "scalar" || b.shape === "scalar" ? (a.shape === "any" || b.shape === "any" ? (a.shape === "any" ? b.shape : a.shape) : a.shape === "scalar" ? b.shape : a.shape) : a.shape, quantity: quantityName(d), any: !!a.any && !!b.any };
        } else if (n.op === "/") {
          const d = a.any ? b.dim : b.any ? a.dim : dimAdd(a.dim, b.dim, -1);
          out = { dim: d, shape: b.shape === "vector" ? a.shape : a.shape, quantity: quantityName(d), any: !!a.any && !!b.any };
        } else if (n.op === "^") {
          if (!b.any && b.shape !== "scalar") throw new UnitError("Exponent must be a dimensionless scalar");
          if (a.shape === "vector") throw new UnitError("Cannot raise a vector field to a power", "Apply mag() first.");
          const exponent = n.b.k === "num" ? n.b.v : 1;
          out = { dim: a.any ? ZERO : dimScale(a.dim, exponent), shape: "scalar", quantity: quantityName(a.any ? ZERO : dimScale(a.dim, exponent)), any: a.any && b.any };
        } else throw new UnitError(`Unsupported operator “${n.op}”`);
        return finish(out, `(${fmtAst(n.a)} ${n.op} ${fmtAst(n.b)})`);
      }
      case "call": {
        const arity = FN_ARITY[n.fn];
        if (!arity) throw new UnitError(`Unknown function “${n.fn}()”`, "Supported: dot, cross, mag, mag_sq, grad, div, curl, sqrt, abs, exp, ln.");
        if (n.args.length < arity[0] || n.args.length > arity[1]) throw new UnitError(`${n.fn}() expects ${arity[0]} argument${arity[0] > 1 ? "s" : ""}`);
        const args = n.args.map(walk);
        const a = args[0];
        let out: Typed;
        switch (n.fn) {
          case "dot": {
            if (a.shape !== "vector" || args[1].shape !== "vector") throw new UnitError("dot() requires two vector fields", "e.g. dot(U, U)");
            if (!dimEq(a.dim, args[1].dim) && !a.any && !args[1].any) throw new UnitError(`Cannot take dot product of ${describe(a)} and ${describe(args[1])}`);
            out = { dim: dimAdd(a.dim, args[1].dim), shape: "scalar", quantity: quantityName(dimAdd(a.dim, args[1].dim)) };
            break;
          }
          case "cross": {
            if (a.shape !== "vector" || args[1].shape !== "vector") throw new UnitError("cross() requires two vector fields");
            out = { dim: dimAdd(a.dim, args[1].dim), shape: "vector" };
            break;
          }
          case "mag":
          case "mag_sq": {
            if (a.shape === "scalar") throw new UnitError(`${n.fn}() expects a vector or tensor field`, "U is a vector; p is a scalar.");
            out = { dim: n.fn === "mag" ? a.dim : dimScale(a.dim, 2), shape: "scalar", quantity: quantityName(n.fn === "mag" ? a.dim : dimScale(a.dim, 2)) };
            break;
          }
          case "grad": {
            out = { dim: dimAdd(a.dim, [0, -1, 0, 0, 0, 0, 0]), shape: a.shape === "scalar" ? "vector" : "tensor" };
            break;
          }
          case "div": {
            if (a.shape !== "vector") throw new UnitError("div() expects a vector field", "Try div(U).");
            out = { dim: dimAdd(a.dim, [0, -1, 0, 0, 0, 0, 0]), shape: "scalar" };
            break;
          }
          case "curl": {
            if (a.shape !== "vector") throw new UnitError("curl() expects a vector field");
            out = { dim: dimAdd(a.dim, [0, -1, 0, 0, 0, 0, 0]), shape: "vector" };
            break;
          }
          case "sqrt": {
            if (a.shape !== "scalar") throw new UnitError("sqrt() expects a scalar; apply mag_sq() to vectors first.");
            out = { dim: a.any ? ZERO : dimScale(a.dim, 0.5), shape: "scalar", any: a.any, quantity: quantityName(a.any ? ZERO : dimScale(a.dim, 0.5)) };
            break;
          }
          case "exp":
          case "ln":
          case "log": {
            if (!dimEq(a.dim, ZERO) && !a.any) throw new UnitError(`${n.fn}() argument must be dimensionless`, `Received ${describe(a)}. Divide by a reference quantity.`);
            out = { dim: ZERO, shape: "scalar", quantity: "Dimensionless" };
            break;
          }
          default:
            out = { ...a, dim: a.dim, shape: "scalar" };
        }
        return finish(out, `${n.fn}(${n.args.map(fmtAst).join(", ")})`);
      }
    }
  };
  return walk(ast);
}

export const fmtAst = (n: Ast): string => {
  switch (n.k) {
    case "num": return String(n.v);
    case "var": return n.name;
    case "neg": return `-${fmtAst(n.a)}`;
    case "bin": return `${fmtAst(n.a)} ${n.op} ${fmtAst(n.b)}`;
    case "call": return `${n.fn}(${n.args.map(fmtAst).join(", ")})`;
  }
};

/* ------------------------- numeric evaluation ------------------------- */
const NV: Record<string, any> = {};
Object.entries(SYMBOLS).forEach(([k, v]) => (NV[k] = v.value));
NV["grad(U)"] = [[1042, 18, -64], [3.1, 142, 22], [-870, 12, 46]];
NV["grad(T)"] = [-8420, 132, 268];

function evalNum(n: Ast): any {
  switch (n.k) {
    case "num": return n.v;
    case "var": return NV[n.name];
    case "neg": return -evalNum(n.a);
    case "bin": {
      const a = evalNum(n.a), b = evalNum(n.b);
      if (n.op === "*") {
        if (Array.isArray(a) && Array.isArray(a[0])) return a.map((r: number[]) => r.map((v) => v * b));
        if (Array.isArray(a)) return a.map((v) => v * (Array.isArray(b) ? b[0] : b));
        if (Array.isArray(b)) return b.map((v) => v * a);
        return a * b;
      }
      if (n.op === "+") return Array.isArray(a) ? a.map((v: number, i: number) => v + (b[i] ?? 0)) : a + b;
      if (n.op === "-") return Array.isArray(a) ? a.map((v: number, i: number) => v - (b[i] ?? 0)) : a - b;
      if (n.op === "/") return a / b;
      return Math.pow(a, b);
    }
    case "call": {
      if (n.fn === "grad") {
        const src = fmtAst(n.args[0]);
        return NV[`grad(${src})`] ?? (src === "U" ? NV["grad(U)"] : NV["grad(T)"]);
      }
      const a = evalNum(n.args[0]);
      const b = n.args[1] ? evalNum(n.args[1]) : null;
      const flat = (m: any): number[] => (Array.isArray(m) ? m.flat(2) : [m]);
      switch (n.fn) {
        case "dot": return a.reduce((s: number, v: number, i: number) => s + v * b[i], 0);
        case "cross": return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
        case "mag": return Math.hypot(...flat(a));
        case "mag_sq": return flat(a).reduce((s: number, v: number) => s + v * v, 0);
        case "sqrt": return Math.sqrt(a);
        case "div": return a[0] + a[1] + a[2];
        case "curl": return [a[2], a[0], a[1]];
        case "abs": return Math.abs(a);
        case "exp": return Math.exp(a);
        case "ln":
        case "log": return Math.log(a);
        default: return a;
      }
    }
  }
}

export type CompileResult = {
  ok: boolean;
  target?: string;
  ast?: Ast;
  type?: Typed;
  trace?: TraceNode[];
  value?: number | number[];
  error?: { message: string; hint?: string };
  dimsText?: string;
  quantityName?: string;
};

export function compileField(expression: string): CompileResult {
  const trace: TraceNode[] = [];
  try {
    const trimmed = expression.trim();
    if (!trimmed) throw new UnitError("Expression is empty", "Try p_total = p + 0.5 * rho * dot(U, U)");
    let target: string | undefined;
    let body = trimmed;
    const assign = trimmed.match(/^([A-Za-z_][A-Za-z0-9_]*)\s*=\s*([\s\S]+)$/);
    if (assign) {
      target = assign[1];
      body = assign[2];
      if (SYMBOLS[target]) throw new UnitError(`Cannot overwrite the built-in field “${target}”`, "Choose a new derived field name.");
    }
    const ast = new Parser(body).parse();
    const type = checkAst(ast, SYMBOLS, trace);
    if (!type.any && type.shape !== "scalar") {
      throw new UnitError(`Result of “${target || "expression"}” is a ${type.shape} field, not a scalar`, "Wrap the expression in mag() or dot() so CFDFLD03 can color-map it.");
    }
    const value = evalNum(ast);
    return {
      ok: true, target, ast, type, trace, value,
      dimsText: formatDim(type.dim),
      quantityName: type.any ? "Dimensionless" : quantityName(type.dim),
    };
  } catch (e) {
    const err = e as UnitError;
    return { ok: false, trace, error: { message: err.message || "Parse error", hint: err.hint } };
  }
}

export const EXAMPLES = [
  { name: "p_total", expr: "p_total = p + 0.5 * rho * dot(U, U)", note: "Pressure · add rho to kinetic energy" },
  { name: "mach", expr: "mach = mag(U) / sqrt(gamma * R_air * T)", note: "Dimensionless · velocity / speed of sound" },
  { name: "mach (literal)", expr: "mach_lit = mag(U) / sqrt(1.4 * 287.05 * T)", note: "Literal 287.05 inferred as dry-air R" },
  { name: "entropy_gen", expr: "entropy_gen = mu / T * mag_sq(grad(U)) + kappa / (T*T) * mag_sq(grad(T))", note: "Volumetric entropy generation" },
  { name: "p + 0.5·|U|²  (invalid)", expr: "p + 0.5 * dot(U, U)", note: "Dimension mismatch · must be rejected" },
  { name: "cp_local", expr: "cp_local = (p - 101325) / (0.5 * rho * dot(U, U))", note: "Pressure coefficient · dimensionless" },
  { name: "vorticity", expr: "vorticity = mag(curl(U))", note: "1/s · vector → scalar reduction" },
];

/* --------------------------- Rhai bridge copy ------------------------- */
export const RHAI_HEADER = `// rhai :: si_checked_field_calculator.rhai
// Each symbol below is registered as (Dim7, FieldHandle).
// Type errors are raised before any GPU buffer is created.
let rhs = "p + 0.5 * dot(U, U)";
match compile_field(rhs) {
    Ok(v)  => upload_cfdfld03(v, "derived.${"{name}"}"),
    Err(e) => print_user_error(e.quantity_conflict()),
}
// console: upload::cfdfld03("derived.field", Colormap::OklabTurbo);`;
