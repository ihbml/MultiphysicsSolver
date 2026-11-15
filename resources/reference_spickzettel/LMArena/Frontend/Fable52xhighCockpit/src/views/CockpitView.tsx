import BlockStack from '../components/BlockStack';
import Inspector from '../components/Inspector';
import SolverPanel from '../components/SolverPanel';
import ViewportPanel from '../components/ViewportPanel';

/**
 * Arbeitsbereich nach dem Vorbild der beiden UI-Referenzen:
 * links der Block-Stack (nTop), rechts der Inspector, unten Telemetrie und
 * Zeitleiste (Blender), in der Mitte der Viewport.
 * Unter 1024 px stapeln sich die Bereiche; der Viewport bleibt oben.
 */
export default function CockpitView() {
  return (
    <div className="grid h-full min-h-0 grid-cols-1 max-lg:overflow-y-auto lg:grid-cols-[264px_minmax(0,1fr)_296px] lg:grid-rows-[minmax(0,1fr)_minmax(252px,30%)] xl:grid-cols-[288px_minmax(0,1fr)_312px]">
      <aside
        aria-label="Block-Stack"
        className="border-line max-lg:order-4 lg:min-h-0 max-lg:border-t lg:col-start-1 lg:row-span-2 lg:row-start-1 lg:border-r"
      >
        <BlockStack />
      </aside>

      <section
        aria-label="Viewport"
        className="max-lg:order-1 lg:min-h-0 max-lg:h-[58svh] max-lg:min-h-[340px] max-lg:shrink-0 lg:col-start-2 lg:row-start-1"
      >
        <ViewportPanel />
      </section>

      <aside
        aria-label="Inspector"
        className="border-line max-lg:order-3 lg:min-h-0 max-lg:border-t lg:col-start-3 lg:row-span-2 lg:row-start-1 lg:border-l"
      >
        <Inspector />
      </aside>

      <section
        aria-label="Solver"
        className="border-t border-line max-lg:order-2 lg:min-h-0 lg:col-start-2 lg:row-start-2"
      >
        <SolverPanel />
      </section>
    </div>
  );
}
