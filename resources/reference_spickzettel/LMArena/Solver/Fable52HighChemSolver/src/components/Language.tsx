interface Block {
  name: string;
  does: string;
  url: string;
}

const CPP: Block[] = [
  {
    name: "Kokkos",
    does: "One loop abstraction that compiles to CUDA, HIP, SYCL or OpenMP.",
    url: "https://kokkos.org",
  },
  {
    name: "AMReX",
    does: "Block-structured adaptive mesh refinement with GPU support.",
    url: "https://amrex-codes.github.io",
  },
  {
    name: "MFEM",
    does: "High-order finite elements that run on GPUs.",
    url: "https://mfem.org",
  },
  {
    name: "SUNDIALS",
    does: "CVODE, IDA and ARKODE stiff integrators, with GPU vector backends.",
    url: "https://sundials.readthedocs.io",
  },
  {
    name: "PETSc",
    does: "Scalable linear and nonlinear solvers for the implicit parts.",
    url: "https://petsc.org",
  },
];

const RUST: Block[] = [
  {
    name: "diffsol",
    does: "Stiff ODE and DAE solvers (BDF, SDIRK) with sensitivity analysis.",
    url: "https://crates.io/crates/diffsol",
  },
  {
    name: "faer",
    does: "Dense and sparse linear algebra for Jacobian solves.",
    url: "https://crates.io/crates/faer",
  },
  {
    name: "rayon",
    does: "Data parallelism across cells with minimal code change.",
    url: "https://crates.io/crates/rayon",
  },
  {
    name: "mpi",
    does: "MPI bindings for distributed-memory runs.",
    url: "https://crates.io/crates/mpi",
  },
  {
    name: "cxx",
    does: "Safe interop, so a C++ solver can call a Rust kernel or the reverse.",
    url: "https://crates.io/crates/cxx",
  },
];

function BlockList({ blocks }: { blocks: Block[] }) {
  return (
    <ul className="mt-6 border-t border-rule">
      {blocks.map((block) => (
        <li
          key={block.name}
          className="grid gap-x-6 gap-y-0.5 border-b border-rule py-3 sm:grid-cols-[7rem_minmax(0,1fr)]"
        >
          <a
            href={block.url}
            target="_blank"
            rel="noopener noreferrer"
            className="justify-self-start rounded-sm font-mono text-sm font-medium underline decoration-rule underline-offset-4 transition-colors hover:decoration-ink"
          >
            {block.name}
            <span className="sr-only"> (opens in a new tab)</span>
          </a>
          <span className="text-pretty text-muted">{block.does}</span>
        </li>
      ))}
    </ul>
  );
}

export function Language() {
  return (
    <section id="language" className="border-t border-rule bg-card">
      <div className="mx-auto max-w-6xl px-5 py-16 sm:px-8 sm:py-24">
        <p className="font-mono text-xs tracking-wide text-muted uppercase">Rust or C++</p>
        <h2 className="mt-3 max-w-3xl font-display text-4xl leading-tight font-medium tracking-tight text-balance sm:text-5xl">
          Adopt in C++. Write new kernels in Rust if you want to.
        </h2>
        <p className="mt-4 max-w-2xl text-lg leading-relaxed text-pretty text-muted">
          Both languages compile to equally fast code. The difference is what already exists.
        </p>

        <div className="mt-12 grid gap-x-16 gap-y-14 lg:grid-cols-2">
          <div>
            <p className="font-mono text-xs tracking-wide text-muted uppercase">
              C++ · for the solver
            </p>
            <h3 className="mt-2 font-display text-3xl leading-tight font-medium tracking-tight">
              Decades of validated physics
            </h3>
            <p className="mt-3 leading-relaxed text-pretty">
              Every solver in the atlas is C++. That is where the validated models, the GPU
              backends and the people who can answer your questions are. If none of them fits and
              you write your own, start from these libraries, not from an empty file.
            </p>
            <BlockList blocks={CPP} />
          </div>

          <div>
            <p className="font-mono text-xs tracking-wide text-muted uppercase">
              Rust · for new kernels
            </p>
            <h3 className="mt-2 font-display text-3xl leading-tight font-medium tracking-tight">
              Good parts, no framework
            </h3>
            <p className="mt-3 leading-relaxed text-pretty">
              Rust has solid numerical crates but nothing that assembles them into a multiphysics
              solver. Fenris, a finite-element crate, says in its own documentation that it is not
              recommended for general use and has no plans for distributed or GPU computing.
            </p>
            <BlockList blocks={RUST} />
          </div>
        </div>

        <div className="mt-14 border-t border-ink pt-6 lg:grid lg:grid-cols-[14rem_minmax(0,1fr)] lg:gap-x-10">
          <p className="font-mono text-xs tracking-wide text-muted uppercase lg:pt-1.5">
            The practical bridge
          </p>
          <p className="mt-2 max-w-3xl font-display text-2xl leading-snug text-pretty lg:mt-0">
            Write the per-cell chemistry source term and Jacobian in Rust, expose it through cxx,
            and call it from the flow solver's chemistry sub-step. You keep the solver's mesh,
            parallelism and I/O, and get Rust's safety where the new code lives.
          </p>
        </div>
      </div>
    </section>
  );
}
