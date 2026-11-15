import { AXIS_HI, AXIS_LO, SCALE_GRADIENT, TICKS, gradientWindow, pos } from "../lib/scale";

const LANDMARKS = [
  { exp: -9.96, name: "C–H bond", size: "1.1 Å" },
  { exp: -8.3, name: "Catalyst particle", size: "~5 nm" },
  { exp: -5, name: "Electrode pore", size: "~10 µm" },
  { exp: -3.3, name: "Flame front", size: "~0.5 mm" },
  { exp: 0, name: "Reactor", size: "~1 m" },
];

const ANSWERS = [
  {
    when: "If you want one framework",
    pick: "MOOSE",
    body: "Implicit finite elements with chemistry, phase-field and porous-flow modules. Its MultiApp system runs fine-scale sub-simulations inside a coarse one.",
    meta: "C++ · LGPL-2.1",
  },
  {
    when: "If you want maximum speed",
    pick: "Pele + LAMMPS",
    body: "Pele for reacting flow on adaptive meshes, LAMMPS for reactive molecular dynamics. Both run natively on GPUs and scale to the largest machines.",
    meta: "C++ · BSD-3 and GPL-2.0",
  },
  {
    when: "If you want Rust",
    pick: "Not as the solver, yet",
    body: "No Rust framework matches the C++ codes today. Rust is a sound choice for a new chemistry kernel that those codes call.",
    meta: "See Rust or C++ below",
  },
];

function Ruler() {
  const decades = Array.from({ length: AXIS_HI - AXIS_LO + 1 }, (_, i) => AXIS_LO + i);
  const major = new Set(TICKS.map((t) => t.exp));

  return (
    <figure className="mt-14 sm:mt-20">
      <div className="relative">
        <div className="h-2 rounded-full" style={{ backgroundImage: SCALE_GRADIENT }} />
        {/* Decade ticks */}
        <div className="relative h-3" aria-hidden="true">
          {decades.map((exp) => (
            <span
              key={exp}
              className={
                "absolute top-0 w-px bg-ink " + (major.has(exp) ? "h-3 opacity-70" : "h-1.5 opacity-30")
              }
              style={{ left: `${pos(exp) * 100}%` }}
            />
          ))}
        </div>
        {/* Landmark markers sit on the bar */}
        {LANDMARKS.map((mark) => (
          <span
            key={mark.name}
            aria-hidden="true"
            className="absolute top-1 size-3.5 -translate-x-1/2 -translate-y-1/2 rounded-full border-2 border-paper bg-ink"
            style={{ left: `${pos(mark.exp) * 100}%` }}
          />
        ))}
        <div className="relative mt-1 h-4 font-mono text-[11px] text-muted" aria-hidden="true">
          {TICKS.map((tick, i) => (
            <span
              key={tick.exp}
              className={
                "absolute top-0 whitespace-nowrap " +
                (i === 0
                  ? ""
                  : i === TICKS.length - 1
                    ? "-translate-x-full"
                    : "-translate-x-1/2") +
                (tick.exp === -9 ? " hidden sm:inline" : "")
              }
              style={{ left: `${pos(tick.exp) * 100}%` }}
            >
              {tick.label}
            </span>
          ))}
        </div>
      </div>
      <figcaption className="mt-5">
        <span className="sr-only">
          Thirteen orders of magnitude separate a chemical bond from a reactor:
        </span>
        <ul className="flex flex-wrap gap-x-6 gap-y-2 text-sm">
          {LANDMARKS.map((mark) => (
            <li key={mark.name} className="flex items-center gap-2">
              <span className="relative block h-2.5 w-2.5 overflow-hidden rounded-full" aria-hidden="true">
                <span
                  className="absolute inset-0"
                  style={{ ...gradientWindow(mark.exp, mark.exp + 0.01), left: 0, width: "100%" }}
                />
              </span>
              <span>{mark.name}</span>
              <span className="font-mono text-xs text-muted">{mark.size}</span>
            </li>
          ))}
        </ul>
      </figcaption>
    </figure>
  );
}

export function Hero() {
  return (
    <section id="top" className="mx-auto max-w-6xl px-5 pt-14 pb-16 sm:px-8 sm:pt-24 sm:pb-24">
      <p className="font-mono text-xs tracking-wide text-muted uppercase">
        Multiphysics chemistry solvers · C++ and Rust
      </p>
      <h1 className="mt-5 max-w-4xl font-display text-[2.5rem] leading-[1.05] font-medium tracking-tight text-balance sm:text-6xl lg:text-7xl">
        No single solver runs from ångströms to metres.{" "}
        <em className="font-normal text-muted">These combinations do.</em>
      </h1>
      <p className="mt-6 max-w-2xl text-lg leading-relaxed text-pretty text-muted">
        Atomistic chemistry and reactor-scale transport need different numerics, so every fast
        multiscale setup is two or three specialised codes passing data between scales. This guide
        covers which to pick, how they fit together, and where Rust stands.
      </p>

      <Ruler />

      <div className="mt-14 grid gap-x-10 gap-y-10 border-t border-ink pt-8 sm:mt-20 md:grid-cols-3">
        {ANSWERS.map((answer) => (
          <div key={answer.pick}>
            <p className="text-sm text-muted">{answer.when}</p>
            <p className="mt-1 font-display text-3xl leading-tight font-medium tracking-tight">
              {answer.pick}
            </p>
            <p className="mt-3 leading-relaxed text-pretty">{answer.body}</p>
            <p className="mt-3 font-mono text-xs text-muted">{answer.meta}</p>
          </div>
        ))}
      </div>
    </section>
  );
}
