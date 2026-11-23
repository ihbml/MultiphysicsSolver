# FLUX / CFD native cockpit reference

This crate is the native-side implementation reference for the React/WebGL2 preview. It contains the schema-driven egui block stack, strict SI parsing, an async stdio JSON-RPC bridge, revision-aware patch bindings, flat per-cell quality packing, and the two-pass wgpu viewport setup. It is intended to be embedded by a desktop host that owns window creation and surface lifetime.

The browser preview does not spawn a process. Its `DemoRpcClient` mirrors the Channel B contract and emits simulated `iter` / `timestep_written` messages. Replace that adapter with this crate's `IpcBridge` from the Tauri/native host.

The WGSL sources live in `reference/shaders/` and are loaded by `viewport.rs` with `include_str!`. The renderer uses camera-relative f64-to-f32 translation, a WebGPU 0..1 reverse-Z infinite projection, `Depth32Float`, `GreaterEqual`, an ID attachment, flat cell metrics, and an unlit field output path. Surface-LIC is a separate compute pass so it can be scheduled only for wall-shear views.

The native crate has not been compiled in this browser-only task environment. Pin dependency versions together when integrating with the host's chosen `wgpu`/`egui` release.