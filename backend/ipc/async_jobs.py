"""async_jobs.py
==============
Thread-safe asynchronous job manager and subprocess-isolated job controller
for long-running backend tasks (geometry import, mesh generation, solver execution).
Supports clean cancellation, RSS memory limits, and partial artifact cleanup.
"""

from __future__ import annotations

import ctypes
import ctypes.util
import logging
import multiprocessing
import os
import queue
import sys
import threading
import time
import uuid
from dataclasses import dataclass, field
from enum import Enum
from typing import Any, Callable, Dict, List, Optional, Tuple

logger = logging.getLogger(__name__)


class JobStatus(str, Enum):
    QUEUED = "queued"
    RUNNING = "running"
    COMPLETED = "completed"
    CANCELLED = "cancelled"
    FAILED = "failed"


@dataclass
class JobRecord:
    """Tracks status, timing, cancellation, and artifacts of an asynchronous task."""
    job_id: str
    command: str
    status: JobStatus = JobStatus.QUEUED
    created_at: float = field(default_factory=time.time)
    started_at: Optional[float] = None
    finished_at: Optional[float] = None
    progress: float = 0.0
    result: Optional[Any] = None
    error: Optional[str] = None
    cancel_event: threading.Event = field(default_factory=threading.Event)
    cleanup_func: Optional[Callable[[], None]] = None
    isolated: bool = False
    max_ram_mb: Optional[float] = None
    _thread: Optional[threading.Thread] = None
    _process: Optional[Any] = None
    _cleanup_done: bool = False

    def is_cancelled(self) -> bool:
        return self.cancel_event.is_set()

    def to_dict(self) -> Dict[str, Any]:
        return {
            "job_id": self.job_id,
            "command": self.command,
            "status": self.status.value,
            "progress": round(self.progress, 3),
            "created_at": self.created_at,
            "started_at": self.started_at,
            "finished_at": self.finished_at,
            "result": self.result if self.status == JobStatus.COMPLETED else None,
            "error": self.error,
            "isolated": self.isolated,
        }


def _get_process_rss_mb(pid: Optional[int]) -> Optional[float]:
    """Return process working-set / RSS memory in MiB, or None if unavailable."""
    if not pid or pid <= 0:
        return None

    if sys.platform == "win32" or os.name == "nt":
        try:
            class _PROCESS_MEMORY_COUNTERS(ctypes.Structure):
                _fields_ = [
                    ("cb", ctypes.c_uint32),
                    ("PageFaultCount", ctypes.c_uint32),
                    ("PeakWorkingSetSize", ctypes.c_size_t),
                    ("WorkingSetSize", ctypes.c_size_t),
                    ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
                    ("QuotaPagedPoolUsage", ctypes.c_size_t),
                    ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
                    ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
                    ("PagefileUsage", ctypes.c_size_t),
                    ("PeakPagefileUsage", ctypes.c_size_t),
                ]

            PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
            PROCESS_VM_READ = 0x0010
            kernel32 = ctypes.windll.kernel32
            handle = kernel32.OpenProcess(
                PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, False, int(pid)
            )
            if not handle:
                handle = kernel32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, int(pid))
            if handle:
                try:
                    pmc = _PROCESS_MEMORY_COUNTERS()
                    pmc.cb = ctypes.sizeof(_PROCESS_MEMORY_COUNTERS)
                    get_mem = getattr(kernel32, "K32GetProcessMemoryInfo", None)
                    if get_mem is None:
                        get_mem = ctypes.windll.psapi.GetProcessMemoryInfo
                    ok = get_mem(handle, ctypes.byref(pmc), pmc.cb)
                    if ok:
                        return float(pmc.WorkingSetSize) / (1024.0 * 1024.0)
                finally:
                    kernel32.CloseHandle(handle)
        except Exception:
            pass

    try:
        import psutil  # type: ignore

        return float(psutil.Process(int(pid)).memory_info().rss) / (1024.0 * 1024.0)
    except Exception:
        return None


def _subprocess_worker_entry(
    target_fn: Callable[..., Any],
    args: Tuple[Any, ...],
    kwargs: Dict[str, Any],
    result_queue: Any,
) -> None:
    """Top-level spawn-safe worker entrypoint for ``SubprocessJobManager``."""
    try:
        res = target_fn(*args, **kwargs)
        result_queue.put(("ok", res))
    except Exception as exc:
        result_queue.put(("err", f"{type(exc).__name__}: {exc}"))


def _terminate_process(proc: Optional[Any], grace_s: float = 0.4) -> None:
    """Terminate an OS child process cleanly, escalating to SIGKILL if needed."""
    if proc is None:
        return
    try:
        if proc.is_alive():
            proc.terminate()
            proc.join(timeout=grace_s)
        if proc.is_alive():
            proc.kill()
            proc.join(timeout=grace_s)
    except (ValueError, OSError):
        pass


def _run_cleanup_once(record: JobRecord) -> None:
    """Invoke ``record.cleanup_func`` at most once."""
    if record._cleanup_done or record.cleanup_func is None:
        return
    record._cleanup_done = True
    try:
        record.cleanup_func()
    except Exception as exc:
        logger.warning("Error running cleanup for job %s: %s", record.job_id, exc)


class SubprocessJobManager:
    """Executes picklable tasks in isolated ``spawn`` OS child processes."""

    def __init__(self, poll_interval_s: float = 0.02) -> None:
        self._ctx = multiprocessing.get_context("spawn")
        self._poll_interval_s = float(poll_interval_s)
        self._jobs: Dict[str, JobRecord] = {}
        self._lock = threading.Lock()

    def create_job(
        self,
        command: str,
        target_fn: Callable[..., Any],
        args: Tuple[Any, ...] = (),
        kwargs: Optional[Dict[str, Any]] = None,
        cleanup_func: Optional[Callable[[], None]] = None,
        max_ram_mb: Optional[float] = None,
    ) -> JobRecord:
        """Spawn a child process for ``target_fn(*args, **kwargs)`` with RAM supervision."""
        job_id = f"job_{uuid.uuid4().hex[:12]}"
        call_kwargs = dict(kwargs) if kwargs is not None else {}
        result_queue = self._ctx.Queue()
        proc = self._ctx.Process(
            target=_subprocess_worker_entry,
            args=(target_fn, tuple(args), call_kwargs, result_queue),
            daemon=True,
        )
        record = JobRecord(
            job_id=job_id,
            command=command,
            status=JobStatus.QUEUED,
            cleanup_func=cleanup_func,
            isolated=True,
            max_ram_mb=max_ram_mb,
            _process=proc,
        )

        def _supervisor() -> None:
            record.started_at = time.time()
            with self._lock:
                if record.is_cancelled():
                    record.status = JobStatus.CANCELLED
                    _run_cleanup_once(record)
                    record.finished_at = time.time()
                    result_queue.close()
                    return
                record.status = JobStatus.RUNNING
                proc.start()

            payload: Optional[Tuple[str, Any]] = None
            ram_exceeded_msg: Optional[str] = None

            try:
                while proc.is_alive():
                    if record.is_cancelled():
                        _terminate_process(proc)
                        break

                    if max_ram_mb is not None:
                        rss_mb = _get_process_rss_mb(proc.pid)
                        if rss_mb is not None and rss_mb > max_ram_mb:
                            ram_exceeded_msg = (
                                f"Memory limit exceeded: child RSS {rss_mb:.1f} MB > {max_ram_mb:.1f} MB"
                            )
                            _terminate_process(proc)
                            break

                    try:
                        payload = result_queue.get_nowait()
                        break
                    except queue.Empty:
                        pass

                    time.sleep(self._poll_interval_s)

                if payload is None and not record.is_cancelled() and ram_exceeded_msg is None:
                    try:
                        payload = result_queue.get(timeout=0.5)
                    except queue.Empty:
                        payload = None

                if proc.is_alive() and (record.is_cancelled() or ram_exceeded_msg is not None):
                    _terminate_process(proc)
                else:
                    try:
                        proc.join(timeout=1.0)
                    except (ValueError, OSError):
                        pass

                with self._lock:
                    if record.is_cancelled() or record.status == JobStatus.CANCELLED:
                        record.status = JobStatus.CANCELLED
                        record.result = None
                        _run_cleanup_once(record)
                    elif ram_exceeded_msg is not None:
                        record.status = JobStatus.FAILED
                        record.error = ram_exceeded_msg
                        record.result = None
                        _run_cleanup_once(record)
                    elif payload is not None and payload[0] == "ok":
                        record.result = payload[1]
                        record.status = JobStatus.COMPLETED
                        record.progress = 1.0
                    elif payload is not None and payload[0] == "err":
                        record.status = JobStatus.FAILED
                        record.error = str(payload[1])
                        record.result = None
                        _run_cleanup_once(record)
                    else:
                        record.status = JobStatus.FAILED
                        record.error = f"Subprocess exited unexpectedly (exitcode={proc.exitcode})"
                        record.result = None
                        _run_cleanup_once(record)
            finally:
                with self._lock:
                    if record.finished_at is None:
                        record.finished_at = time.time()
                try:
                    result_queue.close()
                    result_queue.join_thread()
                except Exception:
                    pass
                try:
                    proc.close()
                except Exception:
                    pass

        th = threading.Thread(target=_supervisor, name=f"CFDSubproc-{job_id}", daemon=True)
        record._thread = th
        with self._lock:
            self._jobs[job_id] = record
        th.start()
        return record

    def get_job(self, job_id: str) -> Optional[JobRecord]:
        with self._lock:
            return self._jobs.get(job_id)

    def cancel_job(self, job_id: str) -> bool:
        """Terminate child process immediately and run cleanup without publishing results."""
        with self._lock:
            record = self._jobs.get(job_id)
            if not record:
                return False
            if record.status in (JobStatus.COMPLETED, JobStatus.CANCELLED, JobStatus.FAILED):
                return True
            record.cancel_event.set()
            record.status = JobStatus.CANCELLED
            record.result = None
            record.finished_at = time.time()
            proc = record._process

        _terminate_process(proc)
        with self._lock:
            _run_cleanup_once(record)
        return True

    def list_jobs(self) -> List[Dict[str, Any]]:
        with self._lock:
            return [j.to_dict() for j in self._jobs.values()]

    def close(self, timeout: float = 2.0) -> None:
        """Terminate any active child processes and wait for supervisor threads."""
        with self._lock:
            job_ids = list(self._jobs.keys())
            threads = [j._thread for j in self._jobs.values() if j._thread is not None]
        for jid in job_ids:
            self.cancel_job(jid)
        deadline = time.time() + timeout
        for th in threads:
            remaining = max(0.05, deadline - time.time())
            th.join(timeout=remaining)


class AsyncJobManager:
    """Central registry and executor for asynchronous background tasks."""

    def __init__(self) -> None:
        self._jobs: Dict[str, JobRecord] = {}
        self._lock = threading.Lock()
        self._subprocess_mgr = SubprocessJobManager()

    def create_job(
        self,
        command: str,
        target_func: Callable[..., Any],
        cleanup_func: Optional[Callable[[], None]] = None,
        *,
        isolated: bool = False,
        max_ram_mb: Optional[float] = None,
        args: Tuple[Any, ...] = (),
        kwargs: Optional[Dict[str, Any]] = None,
    ) -> JobRecord:
        """Creates and launches a background thread job or isolated subprocess job."""
        if isolated:
            return self._subprocess_mgr.create_job(
                command=command,
                target_fn=target_func,
                args=args,
                kwargs=kwargs,
                cleanup_func=cleanup_func,
                max_ram_mb=max_ram_mb,
            )

        job_id = f"job_{uuid.uuid4().hex[:12]}"
        record = JobRecord(
            job_id=job_id,
            command=command,
            status=JobStatus.QUEUED,
            cleanup_func=cleanup_func,
            isolated=False,
            max_ram_mb=max_ram_mb,
        )

        def _runner() -> None:
            record.started_at = time.time()
            record.status = JobStatus.RUNNING
            try:
                if record.is_cancelled():
                    record.status = JobStatus.CANCELLED
                    _run_cleanup_once(record)
                    return

                res = target_func(record)

                if record.is_cancelled():
                    record.status = JobStatus.CANCELLED
                    record.result = None
                    _run_cleanup_once(record)
                else:
                    record.result = res
                    record.status = JobStatus.COMPLETED
                    record.progress = 1.0
            except Exception as e:
                logger.error("Job %s (%s) failed: %s", job_id, command, e, exc_info=True)
                record.error = str(e)
                record.status = JobStatus.FAILED
                record.result = None
                _run_cleanup_once(record)
            finally:
                record.finished_at = time.time()

        th = threading.Thread(target=_runner, name=f"CFDJob-{job_id}", daemon=True)
        record._thread = th

        with self._lock:
            self._jobs[job_id] = record

        th.start()
        return record

    def get_job(self, job_id: str) -> Optional[JobRecord]:
        with self._lock:
            rec = self._jobs.get(job_id)
        if rec is not None:
            return rec
        return self._subprocess_mgr.get_job(job_id)

    def cancel_job(self, job_id: str) -> bool:
        """Cancels a job, sets cancel event, and executes cleanup function.

        Guarantees that a cancelled job NEVER publishes partial valid results.
        """
        with self._lock:
            record = self._jobs.get(job_id)
            if record is not None:
                if record.status in (JobStatus.COMPLETED, JobStatus.CANCELLED, JobStatus.FAILED):
                    return True
                record.cancel_event.set()
                record.status = JobStatus.CANCELLED
                record.result = None
                record.finished_at = time.time()
                _run_cleanup_once(record)
                return True

        return self._subprocess_mgr.cancel_job(job_id)

    def list_jobs(self) -> List[Dict[str, Any]]:
        with self._lock:
            thread_jobs = [j.to_dict() for j in self._jobs.values()]
        return thread_jobs + self._subprocess_mgr.list_jobs()

    def close(self, timeout: float = 2.0) -> None:
        """Cleanly shut down all managed subprocesses."""
        self._subprocess_mgr.close(timeout=timeout)
