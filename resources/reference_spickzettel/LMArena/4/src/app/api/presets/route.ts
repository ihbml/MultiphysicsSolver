import { jsonOk } from "@/lib/http";
import { LIMITS, PRESETS } from "@/lib/params";

export const dynamic = "force-dynamic";

export async function GET() {
  return jsonOk({
    presets: Object.values(PRESETS),
    limits: LIMITS,
    scheme: {
      mesh: "Voronoi dual of an SPH particle cloud (half-plane clipping + Lloyd relaxation)",
      solver: "First/second order finite volume, HLLC Riemann solver, explicit SSP-RK2",
      boundary: "Eulerian-SPH zero-order consistency wall closure with mirror extrapolation",
      interpolation: "Support-normalised conservative particle-mesh / mesh-particle transfer",
      graph: "CSR cell-face adjacency, face-flux array, race-free gather/scatter",
      differentiable: "Central-difference sensitivities of the goal functional through the graph kernel",
    },
  });
}
