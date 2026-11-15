export function Footer() {
  return (
    <footer className="border-t border-rule">
      <div className="mx-auto grid max-w-6xl gap-x-10 gap-y-6 px-5 py-12 sm:px-8 md:grid-cols-[14rem_minmax(0,1fr)]">
        <p className="font-mono text-[15px] font-medium tracking-tight">Å→m</p>
        <div className="max-w-2xl space-y-3 text-sm leading-relaxed text-muted">
          <p>
            All projects listed are open source. Licences, GPU coverage and module lists change
            between releases, so check each project's current documentation before you commit to
            it.
          </p>
          <p>
            Scale ranges are a guide to typical use. What a code can resolve on your problem
            depends on the physics, the mechanism size and the hardware you run on.
          </p>
          <p>
            <a
              href="#top"
              className="rounded-sm text-ink underline decoration-rule underline-offset-4 hover:decoration-ink"
            >
              Back to top
            </a>
          </p>
        </div>
      </div>
    </footer>
  );
}
