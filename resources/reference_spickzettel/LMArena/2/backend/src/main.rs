//! Rust core for the FVM / Eulerian-SPH mesher.
//!
//! Single binary, no Python/NumPy in the loop:
//!     cargo run --release
//! serves the same JSON contract consumed by the web client.
//!
//! Endpoints
//!   GET  /api/health
//!   POST /api/mesh              { geometry, params? }
//!   POST /api/sph               { geometry, params, core? }
//!   POST /api/couple            { geometry, dp, hFactor, kernel, core, scheme }
//!   GET  /api/export/mesh.vtk?geometry=cylinder&h0=0.05
//!   GET  /api/export/particles.vtk
//!   GET  /api/export/graph.json
//!
//! The crate is deliberately dependency-light: geometry + meshing + particle
//! relaxation + transfer operators are all implemented here, so it can be used
//! as a library (add `lib.rs`), a CLI, or a WASM module later.

mod couple;
mod export;
mod geometry;
mod mesh;
mod sph;

use axum::{
    extract::{Query, State},
    http::StatusCode,
    response::{IntoResponse, Response},
    routing::{get, post},
    Json, Router,
};
use serde::Deserialize;
use serde_json::json;
use std::{collections::HashMap, sync::Arc, time::Instant};
use tower_http::cors::CorsLayer;

use geometry::GeometryId;
use mesh::MeshParams;
use sph::SphParams;

#[derive(Clone)]
struct AppState {
    started: Instant,
}

fn err(status: StatusCode, msg: impl Into<String>) -> Response {
    (status, Json(json!({ "error": msg.into() }))).into_response()
}

fn parse_geo(name: &str) -> Option<GeometryId> {
    match name {
        "cylinder" => Some(GeometryId::Cylinder),
        "step" => Some(GeometryId::Step),
        "airfoil" => Some(GeometryId::Airfoil),
        "nozzle" => Some(GeometryId::Nozzle),
        "cavity" => Some(GeometryId::Cavity),
        "bubble" => Some(GeometryId::Bubble),
        _ => None,
    }
}

#[derive(Deserialize)]
struct MeshReq {
    geometry: String,
    #[serde(default)]
    params: Option<MeshParams>,
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
struct SphReq {
    geometry: String,
    params: SphParams,
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
struct CoupleReq {
    geometry: String,
    #[serde(default)]
    mesh: Option<MeshParams>,
    #[serde(default)]
    sph: Option<SphParams>,
    dp: f64,
    h_factor: f64,
    kernel: sph::KernelId,
    core: couple::CoreReq,
    #[serde(default = "opmis")]
    scheme: String,
}

fn opmis() -> String {
    "opmis".to_string()
}

async fn health(State(st): State<Arc<AppState>>) -> impl IntoResponse {
    Json(json!({
        "status": "ok",
        "engine": "rust",
        "version": env!("CARGO_PKG_VERSION"),
        "uptimeSec": st.started.elapsed().as_secs(),
        "algorithms": ["distmesh-level-set", "fvm-topology", "rcm", "eulerian-sph-relaxation",
                       "boundary-surface", "opmis", "adaptive-regions"],
    }))
}

async fn mesh(Json(req): Json<MeshReq>) -> Response {
    let Some(id) = parse_geo(&req.geometry) else {
        return err(StatusCode::BAD_REQUEST, "unknown geometry");
    };
    let params = req.params.unwrap_or_default();
    if !(0.005..=0.5).contains(&params.h0) {
        return err(StatusCode::BAD_REQUEST, "h0 must lie in [0.005, 0.5]");
    }
    let out = mesh::build(id, &params);
    Json(out).into_response()
}

async fn particles(Json(req): Json<SphReq>) -> Response {
    let Some(id) = parse_geo(&req.geometry) else {
        return err(StatusCode::BAD_REQUEST, "unknown geometry");
    };
    if req.params.dp <= 0.0 {
        return err(StatusCode::BAD_REQUEST, "dp must be positive");
    }
    let out = sph::run(id, &req.params);
    Json(out).into_response()
}

async fn couple_summary(Json(req): Json<CoupleReq>) -> Response {
    let Some(id) = parse_geo(&req.geometry) else {
        return err(StatusCode::BAD_REQUEST, "unknown geometry");
    };
    let mp = req.mesh.unwrap_or_default();
    let sp = req.sph.unwrap_or(SphParams {
        dp: req.dp,
        h_factor: req.h_factor,
        iters: 60,
        kernel: req.kernel,
        refine_ratio: 1.0,
        grading: 0.6,
        volume_reinit: true,
        lattice: "square".to_string(),
        core: None,
    });
    let m = mesh::build(id, &mp);
    let s = sph::run(
        id,
        &SphParams {
            core: Some([req.core.x, req.core.y, req.core.r]),
            ..sp
        },
    );
    let out = couple::summary(
        id,
        &m,
        &s,
        &req.core,
        req.dp,
        req.h_factor,
        req.kernel,
        &req.scheme,
    );
    Json(out).into_response()
}

async fn export_mesh(Query(q): Query<HashMap<String, String>>) -> Response {
    let id = match parse_geo(q.get("geometry").map(String::as_str).unwrap_or("cylinder")) {
        Some(v) => v,
        None => return err(StatusCode::BAD_REQUEST, "unknown geometry"),
    };
    let h0 = q
        .get("h0")
        .and_then(|v| v.parse::<f64>().ok())
        .unwrap_or(0.05);
    let m = mesh::build(
        id,
        &MeshParams {
            h0,
            ..Default::default()
        },
    );
    (
        [(axum::http::header::CONTENT_TYPE, "text/plain; charset=utf-8")],
        export::mesh_vtk(&m),
    )
        .into_response()
}

async fn export_particles(Query(q): Query<HashMap<String, String>>) -> Response {
    let id = match parse_geo(q.get("geometry").map(String::as_str).unwrap_or("cylinder")) {
        Some(v) => v,
        None => return err(StatusCode::BAD_REQUEST, "unknown geometry"),
    };
    let s = sph::run(
        id,
        &SphParams {
            dp: 0.055,
            h_factor: 1.3,
            iters: 60,
            kernel: sph::KernelId::Wendland,
            refine_ratio: 1.0,
            grading: 0.6,
            volume_reinit: true,
            lattice: "square".to_string(),
            core: None,
        },
    );
    (
        [(axum::http::header::CONTENT_TYPE, "text/plain; charset=utf-8")],
        export::particles_vtk(&s),
    )
        .into_response()
}

async fn export_graph(Query(q): Query<HashMap<String, String>>) -> Response {
    let id = match parse_geo(q.get("geometry").map(String::as_str).unwrap_or("cylinder")) {
        Some(v) => v,
        None => return err(StatusCode::BAD_REQUEST, "unknown geometry"),
    };
    let m = mesh::build(id, &MeshParams::default());
    Json(export::graph_json(&m, id)).into_response()
}

#[tokio::main]
async fn main() {
    let state = Arc::new(AppState {
        started: Instant::now(),
    });
    let app = Router::new()
        .route("/api/health", get(health))
        .route("/api/mesh", post(mesh))
        .route("/api/sph", post(particles))
        .route("/api/couple", post(couple_summary))
        .route("/api/export/mesh.vtk", get(export_mesh))
        .route("/api/export/particles.vtk", get(export_particles))
        .route("/api/export/graph.json", get(export_graph))
        .layer(CorsLayer::permissive())
        .with_state(state);
    let addr = std::env::var("MESHER_ADDR").unwrap_or_else(|_| "0.0.0.0:8080".to_string());
    let listener = tokio::net::TcpListener::bind(&addr)
        .await
        .expect("cannot bind MESHER_ADDR");
    println!("FVM/SPH mesher Rust core listening on http://{addr}");
    axum::serve(listener, app).await.expect("server error");
}
