const LEVERS = [
  {
    title: "Profile before you port",
    body: "In reacting flow, integrating the stiff chemistry is commonly the largest share of run time. Confirm that for your case before deciding what goes on the GPU.",
  },
  {
    title: "Split chemistry from transport",
    body: "Operator splitting turns the kinetics into an independent ODE system per cell. That is trivially parallel and batches well on GPUs. Pele and TChem are built around it.",
  },
  {
    title: "Generate the mechanism code",
    body: "Compiled source terms and analytic Jacobians for one specific mechanism outrun a general-purpose kinetics interpreter. PelePhysics takes this approach.",
  },
  {
    title: "Shrink the mechanism first",
    body: "Fewer species cut both the chemistry and the transport cost. Reduce or tabulate the mechanism, and validate the result against the full one in Cantera.",
  },
  {
    title: "Refine only where it reacts",
    body: "Reaction zones are thin. Adaptive mesh refinement, as in the AMReX-based codes, keeps fine cells at the front and coarse cells everywhere else.",
  },
  {
    title: "Couple offline when you can",
    body: "Passing fitted parameters upward as files costs almost nothing. Keep run-time coupling for cases where the fine scale depends on the evolving coarse state.",
  },
];

export function Speed() {
  return (
    <section id="speed" className="border-t border-rule">
      <div className="mx-auto max-w-6xl px-5 py-16 sm:px-8 sm:py-24">
        <p className="font-mono text-xs tracking-wide text-muted uppercase">Performance</p>
        <h2 className="mt-3 max-w-3xl font-display text-4xl leading-tight font-medium tracking-tight text-balance sm:text-5xl">
          Where the speed comes from
        </h2>
        <p className="mt-4 max-w-2xl text-lg leading-relaxed text-pretty text-muted">
          The language matters less than these six decisions. They apply whichever codes you choose.
        </p>

        <dl className="mt-12 grid gap-x-10 gap-y-10 sm:grid-cols-2 lg:grid-cols-3">
          {LEVERS.map((lever) => (
            <div key={lever.title} className="border-t border-ink pt-4">
              <dt className="text-lg leading-snug font-semibold">{lever.title}</dt>
              <dd className="mt-2 leading-relaxed text-pretty text-muted">{lever.body}</dd>
            </div>
          ))}
        </dl>
      </div>
    </section>
  );
}
