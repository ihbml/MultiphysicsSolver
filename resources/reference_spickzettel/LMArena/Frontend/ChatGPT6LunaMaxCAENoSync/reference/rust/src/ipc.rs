use serde::Serialize;
use serde_json::Value;
use std::collections::{HashMap, VecDeque};
use std::process::Stdio;
use std::sync::{atomic::{AtomicU64, Ordering}, Arc, Mutex};
use thiserror::Error;
use tokio::io::{AsyncBufReadExt, AsyncWriteExt, BufReader, BufWriter};
use tokio::process::{Child, Command};
use tokio::sync::{broadcast, mpsc, oneshot};
use tokio::time::{timeout, Duration};

const STDERR_CAPACITY: usize = 256;

#[derive(Debug, Error)]
pub enum IpcError {
    #[error("could not start backend: {0}")]
    Spawn(#[from] std::io::Error),
    #[error("could not serialize JSON-RPC frame: {0}")]
    Serialize(#[from] serde_json::Error),
    #[error("request {0} timed out")]
    Timeout(u64),
    #[error("backend request channel closed")]
    Closed,
    #[error("backend returned an error: {0}")]
    Backend(String),
}

#[derive(Clone, Debug, Serialize)]
pub struct RpcRequest {
    pub jsonrpc: &'static str,
    pub id: u64,
    pub method: String,
    #[serde(default)]
    pub params: Value,
}

#[derive(Clone, Debug)]
pub enum IpcEvent {
    Iter(Value),
    TimestepWritten(Value),
    Notification { method: String, params: Value },
    Diagnostic(String),
    ProtocolError(String),
    BackendExited,
}

#[derive(Clone)]
pub struct IpcBridge {
    outbound: mpsc::Sender<RpcRequest>,
    pending: Arc<tokio::sync::Mutex<HashMap<u64, oneshot::Sender<Result<Value, IpcError>>>>>,
    next_id: Arc<AtomicU64>,
    events: broadcast::Sender<IpcEvent>,
    stderr: Arc<Mutex<VecDeque<String>>>,
    child: Arc<tokio::sync::Mutex<Child>>,
}

impl IpcBridge {
    /// Spawn the solver as a long-lived stdio JSON-RPC / NDJSON process.
    pub fn spawn(program: &str, args: &[String]) -> Result<Self, IpcError> {
        let mut child = Command::new(program)
            .args(args)
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .kill_on_drop(true)
            .spawn()?;
        let stdin = child.stdin.take().ok_or_else(|| std::io::Error::new(std::io::ErrorKind::Other, "missing child stdin"))?;
        let stdout = child.stdout.take().ok_or_else(|| std::io::Error::new(std::io::ErrorKind::Other, "missing child stdout"))?;
        let stderr_reader = child.stderr.take().ok_or_else(|| std::io::Error::new(std::io::ErrorKind::Other, "missing child stderr"))?;
        let child = Arc::new(tokio::sync::Mutex::new(child));
        let (outbound, mut outgoing) = mpsc::channel::<RpcRequest>(128);
        let (events, _) = broadcast::channel::<IpcEvent>(512);
        let pending = Arc::new(tokio::sync::Mutex::new(HashMap::new()));
        let stderr = Arc::new(Mutex::new(VecDeque::with_capacity(STDERR_CAPACITY)));

        tokio::spawn(async move {
            let mut writer = BufWriter::new(stdin);
            while let Some(request) = outgoing.recv().await {
                let mut frame = match serde_json::to_vec(&request) {
                    Ok(frame) => frame,
                    Err(_) => break,
                };
                frame.push(b'\n');
                if writer.write_all(&frame).await.is_err() || writer.flush().await.is_err() { break; }
            }
        });

        let reader_events = events.clone();
        let reader_pending = pending.clone();
        tokio::spawn(async move {
            let mut lines = BufReader::new(stdout).lines();
            loop {
                match lines.next_line().await {
                    Ok(Some(line)) => {
                        let value: Value = match serde_json::from_str(&line) {
                            Ok(value) => value,
                            Err(error) => { let _ = reader_events.send(IpcEvent::ProtocolError(format!("invalid NDJSON frame: {error}"))); continue; }
                        };
                        if let Some(method) = value.get("method").and_then(Value::as_str) {
                            let params = value.get("params").cloned().unwrap_or(Value::Null);
                            let event = match method {
                                "iter" => IpcEvent::Iter(params),
                                "timestep_written" => IpcEvent::TimestepWritten(params),
                                _ => IpcEvent::Notification { method: method.to_owned(), params },
                            };
                            let _ = reader_events.send(event);
                            continue;
                        }
                        if let Some(id) = value.get("id").and_then(Value::as_u64) {
                            if let Some(reply) = reader_pending.lock().await.remove(&id) {
                                let result = if let Some(error) = value.get("error") {
                                    Err(IpcError::Backend(error.to_string()))
                                } else {
                                    Ok(value.get("result").cloned().unwrap_or(Value::Null))
                                };
                                let _ = reply.send(result);
                            }
                        }
                    }
                    Ok(None) => { let _ = reader_events.send(IpcEvent::BackendExited); break; }
                    Err(error) => { let _ = reader_events.send(IpcEvent::ProtocolError(format!("stdout read failed: {error}"))); break; }
                }
            }
        });

        let stderr_ring = stderr.clone();
        let diagnostic_events = events.clone();
        tokio::spawn(async move {
            let mut lines = BufReader::new(stderr_reader).lines();
            while let Ok(Some(line)) = lines.next_line().await {
                if let Ok(mut ring) = stderr_ring.lock() {
                    if ring.len() == STDERR_CAPACITY { ring.pop_front(); }
                    ring.push_back(line.clone());
                }
                let _ = diagnostic_events.send(IpcEvent::Diagnostic(line));
            }
        });

        Ok(Self { outbound, pending, next_id: Arc::new(AtomicU64::new(1)), events, stderr, child })
    }

    pub fn subscribe(&self) -> broadcast::Receiver<IpcEvent> { self.events.subscribe() }

    pub async fn request(&self, method: impl Into<String>, params: Value, wait: Duration) -> Result<Value, IpcError> {
        let id = self.next_id.fetch_add(1, Ordering::Relaxed);
        let (sender, receiver) = oneshot::channel();
        self.pending.lock().await.insert(id, sender);
        let request = RpcRequest { jsonrpc: "2.0", id, method: method.into(), params };
        if self.outbound.send(request).await.is_err() {
            self.pending.lock().await.remove(&id);
            return Err(IpcError::Closed);
        }
        match timeout(wait, receiver).await {
            Ok(Ok(response)) => response,
            Ok(Err(_)) => Err(IpcError::Closed),
            Err(_) => { self.pending.lock().await.remove(&id); Err(IpcError::Timeout(id)) }
        }
    }

    /// Cooperative stop for the active backend job; the process stays available.
    pub async fn cancel_job(&self, job_id: &str) -> Result<Value, IpcError> {
        self.request("job.cancel", serde_json::json!({ "job_id": job_id, "cooperative": true }), Duration::from_secs(2)).await
    }

    pub fn stderr_snapshot(&self) -> Vec<String> {
        self.stderr.lock().map(|ring| ring.iter().cloned().collect()).unwrap_or_default()
    }

    pub async fn shutdown(&self) -> Result<(), IpcError> {
        let mut child = self.child.lock().await;
        child.kill().await.map_err(IpcError::Spawn)
    }
}

#[derive(Clone, Default)]
pub struct RevisionClock {
    geometry: Arc<AtomicU64>,
    mesh: Arc<AtomicU64>,
    patch_geometry: Arc<AtomicU64>,
    patch_mesh: Arc<AtomicU64>,
}

impl RevisionClock {
    pub fn geometry(&self) -> u64 { self.geometry.load(Ordering::Acquire) }
    pub fn mesh(&self) -> u64 { self.mesh.load(Ordering::Acquire) }
    pub fn advance_geometry(&self) -> u64 { self.geometry.fetch_add(1, Ordering::AcqRel) + 1 }
    pub fn advance_mesh(&self) -> u64 { self.mesh.fetch_add(1, Ordering::AcqRel) + 1 }

    pub fn bind_patch_revision(&self) {
        self.patch_geometry.store(self.geometry(), Ordering::Release);
        self.patch_mesh.store(self.mesh(), Ordering::Release);
    }

    pub fn patch_bindings_current(&self) -> bool {
        self.patch_geometry.load(Ordering::Acquire) == self.geometry()
            && self.patch_mesh.load(Ordering::Acquire) == self.mesh()
    }
}

#[cfg(test)]
mod tests {
    use super::RevisionClock;

    #[test]
    fn remesh_invalidates_old_face_group_bindings() {
        let revisions = RevisionClock::default();
        revisions.bind_patch_revision();
        assert!(revisions.patch_bindings_current());
        revisions.advance_mesh();
        assert!(!revisions.patch_bindings_current());
    }
}