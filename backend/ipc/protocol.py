"""protocol.py
===========
JSON-RPC 2.0 specification implementation and NDJSON line serialization for CFDSolver IPC.
"""

from __future__ import annotations

import json
from dataclasses import dataclass
from typing import Any, Dict, List, Optional, Tuple, Union

# Standard JSON-RPC 2.0 Error Codes
PARSE_ERROR = -32700
INVALID_REQUEST = -32600
METHOD_NOT_FOUND = -32601
INVALID_PARAMS = -32602
INTERNAL_ERROR = -32603

# Application-specific error codes (-32000 to -32099)
ERR_GEOMETRY = -32001
ERR_MESHING = -32002
ERR_SOLVER = -32003
ERR_SI_DIMENSION = -32004
ERR_NOT_FOUND = -32005
ERR_CANCELLED = -32006


def make_success_response(result: Any, request_id: Any) -> Dict[str, Any]:
    """Builds a compliant JSON-RPC 2.0 success response object."""
    return {
        "jsonrpc": "2.0",
        "result": result,
        "id": request_id,
    }


def make_error_response(code: int, message: str, data: Any = None, request_id: Any = None) -> Dict[str, Any]:
    """Builds a compliant JSON-RPC 2.0 error response object."""
    err_obj: Dict[str, Any] = {
        "code": code,
        "message": message,
    }
    if data is not None:
        err_obj["data"] = data
    return {
        "jsonrpc": "2.0",
        "error": err_obj,
        "id": request_id,
    }


def parse_jsonrpc_request(
    line: str,
) -> Tuple[bool, Optional[str], Optional[Union[Dict[str, Any], List[Any]]], Any, Optional[Dict[str, Any]]]:
    """Parses an incoming NDJSON line according to JSON-RPC 2.0 specifications.

    Returns:
        (is_valid, method, params, request_id, error_response_dict)
    """
    clean = line.strip()
    if not clean:
        return False, None, None, None, None

    try:
        data = json.loads(clean)
    except json.JSONDecodeError as exc:
        return False, None, None, None, make_error_response(PARSE_ERROR, f"Parse error: {exc}", request_id=None)

    if not isinstance(data, dict):
        return False, None, None, None, make_error_response(INVALID_REQUEST, "Invalid Request: root must be JSON object", request_id=None)

    if data.get("jsonrpc") != "2.0":
        req_id = data.get("id")
        return False, None, None, req_id, make_error_response(INVALID_REQUEST, "Invalid Request: missing or invalid 'jsonrpc' field ('2.0' required)", request_id=req_id)

    method = data.get("method")
    if not isinstance(method, str) or not method:
        req_id = data.get("id")
        return False, None, None, req_id, make_error_response(INVALID_REQUEST, "Invalid Request: 'method' must be a non-empty string", request_id=req_id)

    req_id = data.get("id")
    params = data.get("params", {})
    if params is not None and not isinstance(params, (dict, list)):
        return False, None, None, req_id, make_error_response(INVALID_PARAMS, "Invalid params: must be object or array", request_id=req_id)

    return True, method, params, req_id, None


def serialize_ndjson(msg: Dict[str, Any]) -> str:
    """Serializes a Python dict into a single-line NDJSON string terminated by newline."""
    return json.dumps(msg, separators=(",", ":")) + "\n"
