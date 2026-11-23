//! Channel B — async JSON-RPC 2.0 / NDJSON over the backend's stdin/stdout.
//!
//! * `call(method, params)` → correlated request id, 30 s timeout, one-shot resolve
//! * stdout reader task routes messages into the pending map (responses) or the
//!   telemetry bus (`iter`, `timestep_written` streams)
//! * stderr is drained into a 4096-line diagnostic ring buffer ( surfaced in the
//!   NDJSON console strip )
//! * `geometry_revision` / `mesh_revision` are latched from results and published
//!   for the UI's stale-BC invalidation
//! * cooperative `job.cancel` is a plain RPC; the backend honors it between iters

use std::collections::{BTreeMap, HashMap, VecDeque};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Arc;
use std::time::Duration;

use anyhow::{anyhow, Context, Result};
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use tokio::io::{AsyncBufReadExt, AsyncWriteExt, BufReader};
use tokio::process::{Child, ChildStdin, Command};
use tokio::sync::{mpsc, oneshot, Mutex};
use tokio::time::timeout;

#[derive(Debug, Clone, Deserialize)]
pub struct IterFrame {
    pub iter: u64,
    #[serde(default)]
    pub residuals: BTreeMap<String, f64>,
    #[serde(default)]
    pub co_mean: f64,
    #[serde(default)]
    pub co_max: f64,
    #[serde(default)]
    pub cd: f64,
    #[serde(default)]
    pub cl: f64,
    #[serde(default)]
    pub t_max: f64,
    #[serde(default)]
    pub n_nan_cells: u32,
}

#[derive(Debug)]
pub enum Telemetry {
    Iter(IterFrame),
    TimestepWritten(f64),
    /// raw stderr line from the backend
    Diag(String),
    Dying,
}

pub struct IpcClient {
    child: Arc<Mutex<Child>>,
    stdin: Arc<Mutex<ChildStdin>>,
    pending: Arc<Mutex<HashMap<u64, oneshot::Sender<Result<Value>>>>>,
    next_id: AtomicU64,
    pub tx: mpsc::UnboundedSender<Telemetry>,
    pub rx: mpsc::UnboundedReceiver<Telemetry>,
    pub geometry_revision: AtomicU64,
    pub mesh_revision: AtomicU64,
    pub stderr_ring: Arc<Mutex<VecDeque<String>>>,
}

#[derive(Deserialize)]
struct NdMsg {
    #[serde(default)]
    id: Option<u64>,
    #[serde(default)]
    method: Option<String>,
    #[serde(default)]
    params: Option<Value>,
    #[serde(default)]
    result: Option<Value>,
    #[serde(default)]
    error: Option<Value>,
}

impl IpcClient {
    pub async fn spawn(cmd: &str) -> Result<Self> {
        let mut child = Command::new(cmd)
            .stdin(std::process::Stdio::piped())
            .stdout(std::process::Stdio::piped())
            .stderr(std::process::Stdio::piped())
            .spawn()
            .with_context(|| format!("spawn backend `{cmd}`"))?;

        let stdin = Arc::new(Mutex::new(child.stdin.take().expect("stdin")));
        let stdout = child.stdout.take().expect("stdout");
        let stderr = child.stderr.take().expect("stderr");
        let pending: Arc<Mutex<HashMap<u64, oneshot::Sender<Result<Value>>>>> =
            Arc::new(Mutex::new(HashMap::new()));
        let stderr_ring: Arc<Mutex<VecDeque<String>>> = Arc::new(Mutex::new(VecDeque::with_capacity(4096)));

        let (tx, rx) = mpsc::unbounded_channel::<Telemetry>();

        // stdout: responses + telemetry streams
        {
            let pending = pending.clone();
            let tx = tx.clone();
            tokio::spawn(async move {
                let mut lines = BufReader::new(stdout).lines();
                while let Ok(Some(line)) = lines.next_line().await {
                    let line = line.trim().to_string();
                    if line.is_empty() { continue; }
                    let msg: NdMsg = match serde_json::from_str(&line) {
                        Ok(m) => m,
                        Err(_) => {
                            let _ = tx.send(Telemetry::Diag(format!("[ndjson parse] {line}")));
                            continue;
                        }
                    };
                    if let Some(id) = msg.id {
                        let entry = pending.lock().await.remove(&id);
                        if let Some(send) = entry {
                            let out = match (msg.result, msg.error) {
                                (Some(r), _) => Ok(r),
                                (None, Some(e)) => Err(anyhow!("RPC error: {e}")),
                                _ => Err(anyhow!("malformed response id={id}")),
                            };
                            let _ = send.send(out);
                        }
                    } else if let (Some(method), Some(params)) = (msg.method, msg.params) {
                        match method.as_str() {
                            "iter" => {
                                if let Ok(frame) = serde_json::from_value::<IterFrame>(params) {
                                    let _ = tx.send(Telemetry::Iter(frame));
                                }
                            }
                            "timestep_written" => {
                                if let Some(t) = params.get("time").and_then(Value::as_f64) {
                                    let _ = tx.send(Telemetry::TimestepWritten(t));
                                }
                            }
                            _ => { /* unknown server push */ }
                        }
                    }
                }
                let _ = tx.send(Telemetry::Dying);
            });
        }

        // stderr → ring buffer
        {
            let ring = stderr_ring.clone();
            let tx = tx.clone();
            tokio::spawn(async move {
                let mut lines = BufReader::new(stderr).lines();
                while let Ok(Some(line)) = lines.next_line().await {
                    ring.lock().await.push_back(line.clone());
                    if ring.lock().await.len() > 4096 {
                        ring.lock().await.pop_front();
                    }
                    let _ = tx.send(Telemetry::Diag(line));
                }
            });
        }

        let me = Self {
            child: Arc::new(Mutex::new(child)),
            stdin,
            pending: pending.clone(),
            next_id: AtomicU64::new(1),
            tx,
            rx,
            geometry_revision: AtomicU64::new(0),
            mesh_revision: AtomicU64::new(0),
            stderr_ring,
        };

        // handshake
        let hello = me.call("system.hello", json!({})).await?;
        log::info!("backend: {}", hello.get("name").and_then(Value::as_str).unwrap_or("?"));
        Ok(me)
    }

    /// Correlated JSON-RPC 2.0 request over NDJSON with timeout.
    pub async fn call(&self, method: &str, params: Value) -> Result<Value> {
        let id = self.next_id.fetch_add(1, Ordering::Relaxed);
        let (send, recv) = oneshot::channel();
        self.pending.lock().await.insert(id, send);

        let line = json!({ "jsonrpc": "2.0", "id": id, "method": method, "params": params }).to_string();
        {
            let mut stdin = self.stdin.lock().await;
            stdin
                .write_all(line.as_bytes())
                .await
                .context("write stdin")?;
            stdin.write_all(b"\n").await?;
            stdin.flush().await?;
        }

        let out = timeout(Duration::from_secs(30), recv)
            .await
            .map_err(|_| anyhow!("RPC timeout: {method}"))?
            .map_err(|_| anyhow!("RPC dropped: {method}"))??;

        // latch revisions for stale-BC invalidation
        if let Some(rev) = out.get("geometry_revision").and_then(Value::as_u64) {
            self.geometry_revision.store(rev, Ordering::Relaxed);
        }
        if let Some(rev) = out.get("mesh_revision").and_then(Value::as_u64) {
            self.mesh_revision.store(rev, Ordering::Relaxed);
        }
        Ok(out)
    }

    /// Cooperative cancel — honored by the backend between PISO iterations.
    pub async fn cancel_job(&self) -> Result<Value> {
        self.call("job.cancel", json!({ "job": "solve-1" })).await
    }

    pub fn geometry_revision(&self) -> u64 {
        self.geometry_revision.load(Ordering::Relaxed)
    }
    pub fn mesh_revision(&self) -> u64 {
        self.mesh_revision.load(Ordering::Relaxed)
    }

    pub fn drain_stderr(&self, n: usize) -> Vec<String> {
        self.stderr_ring.lock().await.iter().rev().take(n).map(String::from).collect()
    }
}

#[derive(Serialize)]
struct UnusedMarker; // keeps serde::Serialize import honest for downstream code
