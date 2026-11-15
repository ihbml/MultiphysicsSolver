# AXIOM (.axcad) format proposal

Status: experimental design, schema version 0.1.0. This is not an existing interchange standard. The accompanying browser app creates and validates a demonstrator archive; it does not evaluate CAD geometry.

## Goals and non-goals

AXIOM is a container for editable design intent, exact geometry, fast display, simulation handoff, rendering assets, and an auditable change record. The authoritative model is an exact boundary representation (B-rep) plus a feature dependency graph. Tessellations are derived caches. A file format alone cannot implement a CAD kernel, heal invalid solids, generate a solver mesh, or make a legal-grade audit trail.

## Container layout

Use a standard ZIP-compatible container with extension `.axcad`. Every path is relative, UTF-8, case-sensitive, and unique. Reject path traversal and unbounded decompression on read. The following entries are the proposed production layout:

```
manifest.json                 Required: schema, units, references, hashes
geometry/brep.bin             Exact B-rep encoded by a specified kernel adapter
geometry/lod/preview.glb      Optional glTF 2.0 display cache
geometry/lod/detail.glb       Optional finer display cache
history/operations.json       Feature DAG and operation parameters
simulation/regions.json      Named volumes, boundary face sets, mesh hints
materials/library.json       PBR appearance and physical properties
provenance/events.jsonl       Append-only event stream
textures/<sha256>.<ext>       Optional content-addressed textures
```

The downloadable demo intentionally uses `geometry/preview.json` (a watertight box triangle mesh) and `provenance/events.json`, not a B-rep or signed JSONL stream. Its operation graph is illustrative metadata; it is not a geometric evaluation of the box.

### Manifest

The manifest records `format`, `schemaVersion`, `modelId`, declared entrypoints, `units`, coordinate handedness/up axis, geometry capabilities, and a SHA-256 digest per payload. Use SI base units internally and include explicit unit metadata for imported and exported values. New readers must reject unsupported major schema versions and unknown required capabilities; preserve unknown optional fields when rewriting.

ZIP readers typically inspect the central directory at the end of the archive. For remote, low-latency access, use HTTP range requests to fetch that directory, then fetch the manifest and preview entries independently. A plain ZIP does not make its manifest literally the first byte readable. Do not solid-compress all entries together. For very large models, partition geometry by body/LOD; avoid repeated mesh data.

### Exact geometry

`brep.bin` needs an explicit, published encoding and kernel-version metadata. Prefer a documented, independently implementable representation or a STEP AP242 adapter; a proprietary kernel dump by itself is not interoperable. Preserve analytic surfaces, curves, trims, tolerances, assemblies, transforms, and closed-shell/solid classification. Maintain persistent IDs for bodies, faces, edges, and semantic regions. Face array position is not a stable identifier after a Boolean or fillet.

## Feature graph and edits between operations

Production operations are a directed acyclic graph, not merely a list. Each feature has a stable ID, typed operation, parameter values with units, ordered input feature/body IDs, tool operands for Booleans, author, timestamp, implementation/kernel version, and deterministic output references. A Boolean cut must reference both a target and a tool body; the demo's linear `parent` field is deliberately only a simplified UI example.

To insert operation `N` between existing operations `A` and `B`:

1. Validate `N`'s operands and ensure the new edges do not form a cycle. Set `N.inputs` to include `A` (and any additional operands).
2. Replace only the relevant `A` input edge in `B.inputs` with `N`; preserve the stable IDs of `A` and `B`.
3. Record a new event with the previous revision hash, edit intent, parameters, and actor. Never silently rewrite the old provenance record.
4. Mark `N`, `B`, and all transitive descendants dirty. Recompute them in topological order. Key evaluation caches by operation type, canonical parameters, kernel version, and content hashes of input results.
5. Remap semantic face/edge references using kernel-generated evolution maps plus geometric matching. If a one-to-many or ambiguous match remains, surface a conflict; never guess a solver boundary assignment.
6. Revalidate manifoldness, named regions, display meshes, and downstream simulation exports. Commit a new model revision atomically or roll back the edit.

Branching, assemblies, suppression, undo/redo, and external references should use the same immutable-revision principle. Canonical serialization and fixed numerical tolerance policies are needed for reproducible rebuilds across machines.

## CFD, FVM and SPH handoff

Store named solids and fluid volumes, persistent boundary face sets, adjacency, outward normals, material/domain roles, coordinate frame, and unit-scaled meshing hints. CFD/FVM export should create a watertight fluid domain and pass cells, zones, and named boundary conditions to adapters such as CGNS or VTK/VTU. STEP AP242 is a useful CAD exchange path, not a replacement for solver mesh metadata. SPH export should supply closed fluid volumes, boundary surfaces, spacing/seed hints, and boundary IDs; the SPH mesher generates particles. Keep solver fields and generated meshes in derived artifacts linked to the source model revision, not as the editable source of truth.

Before export, validate shell closure, normals, gap tolerance, self-intersections, volume sign, region membership, and mesh quality. A mesher adapter must report failures rather than silently dropping or renumbering named boundaries.

## Materials and rendering

Use stable material IDs shared by faces/bodies and simulation regions. Keep `appearance.pbr` separate from `physical` properties: base color, metallic, roughness, normal, emissive, opacity and texture references do not define density, viscosity, thermal conductivity, etc. Specify color spaces (for example, sRGB input textures and linear-light shading), texture transforms, normal conventions, and UV mappings. Encode previews as glTF/GLB when possible; regenerate them whenever authoritative geometry or appearance changes.

## Provenance and integrity

Each event should include event ID, timestamp, authenticated actor, action, affected stable IDs, input revision hash, output revision hash, tool/kernel version, and reason or document reference. Hash each payload with SHA-256 and verify before trusting it. For tamper-evident production records, hash-chain events and sign revision roots with managed keys; retain prior revisions in immutable storage. A hash alone only detects accidental or untrusted changes when the expected hash is trusted. The demo verifies its own manifest hashes but does not authenticate authors or provide tamper-proof history.

## Suggested implementation sequence

1. Freeze JSON schemas for manifest, graph, materials, and regions; publish reference fixtures and validators.
2. Implement a CAD-kernel adapter for exact B-rep serialization, evaluation, persistent topology evolution, and geometry healing.
3. Add preview LOD generation and lazy ZIP entry loading with size limits and hash verification.
4. Build STEP AP242, CGNS/VTK, and SPH-mesher adapters with round-trip tests for named regions and units.
5. Add immutable revision storage, authenticated actors, signatures, schema migration tests, and adversarial file tests.

The companion UI demonstrates archive packaging, import/export, hash verification, material metadata editing, and simplified graph insertion/removal. It is a prototype of the format's shape, not a certified CAD/CFD implementation.