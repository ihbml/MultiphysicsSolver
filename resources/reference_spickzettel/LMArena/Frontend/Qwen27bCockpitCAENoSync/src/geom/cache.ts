import type { DomainParams } from "../fields/analytics";
import type { DomainGeo } from "./domain";
import type { MeshData } from "./mesh";

/** Shared CPU-side geometry/mesh cache: written by the IPC backend, read by the GL engine. */
export const geoCache: {
  params: DomainParams | null;
  domain: DomainGeo | null;
  mesh: MeshData | null;
} = { params: null, domain: null, mesh: null };
