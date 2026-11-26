"""Embedded Expression Engine & SIMD Bytecode Evaluator (SUGGESTIONS_BACKEND.md §11.2).

Provides an embedded expression evaluator (Rhai / SIMD bytecode style) for:
- Generic analytical boundary condition profiles: u_in(x, y, z, t)
- Temperature-dependent fluid/solid properties: k(T), mu(T), cp(T)
- Custom volumetric source terms: S(x, y, z, t, phi)

Features:
- Full support for math operators (+, -, *, /, ^, **, unary minus)
- Standard math functions: sin, cos, tan, exp, log, ln, sqrt, abs, sinh, cosh, tanh, asin, acos, atan, min, max
- Constants: pi, e
- Vectorized SIMD execution over NumPy arrays (spatial coordinates x, y, z, time t, fields)
- Bytecode compiler and virtual machine (VM) stack execution
- Support for Rhai-style semicolon statements and 'let var = expr;' assignments
"""

from __future__ import annotations

import ast
from dataclasses import dataclass
from enum import IntEnum
import keyword
import math
import re
from typing import Any, Callable, Dict, List, Optional, Set, Tuple, Union
import numpy as np


MAX_EXPR_LENGTH = 16_384
MAX_STATEMENTS = 256
MAX_BYTECODE_OPS = 8_192
MAX_AST_DEPTH = 64


class OpCode(IntEnum):
    """Bytecode virtual machine instructions."""
    LOAD_CONST = 1
    LOAD_VAR = 2
    ADD = 3
    SUB = 4
    MUL = 5
    DIV = 6
    POW = 7
    NEG = 8
    CALL_FUNC = 9
    CMP_LT = 11
    CMP_LE = 12
    CMP_GT = 13
    CMP_GE = 14
    CMP_EQ = 15
    CMP_NE = 16
    BOOL_AND = 17
    BOOL_OR = 18
    BOOL_NOT = 19
    STORE_VAR = 20
    RETURN = 10


@dataclass
class Instruction:
    """A single bytecode instruction."""
    opcode: OpCode
    arg: Any = None


MATH_FUNCTIONS: Dict[str, Callable] = {
    "sin": np.sin,
    "cos": np.cos,
    "tan": np.tan,
    "exp": np.exp,
    "log": np.log,
    "ln": np.log,
    "sqrt": np.sqrt,
    "abs": np.abs,
    "sinh": np.sinh,
    "cosh": np.cosh,
    "tanh": np.tanh,
    "asin": np.arcsin,
    "acos": np.arccos,
    "atan": np.arctan,
    "min": np.minimum,
    "max": np.maximum,
}

MATH_CONSTANTS: Dict[str, float] = {
    "pi": math.pi,
    "PI": math.pi,
    "e": math.e,
    "E": math.e,
}


class ExpressionEngine:
    """Compiler and vectorized bytecode evaluator for boundary profiles and source terms."""

    def __init__(self, expression_str: Optional[str] = None):
        self.raw_expression = expression_str or ""
        self.cleaned_expression = ""
        self.bytecode: List[Instruction] = []
        self.variables: Set[str] = set()
        if expression_str is not None:
            self.compile_expression(expression_str)

    def compile_expression(self, expression_str: str) -> None:
        """Preprocess and compile an expression string into bytecode."""
        if not isinstance(expression_str, str):
            raise TypeError(f"Expression must be a string, got {type(expression_str).__name__}")
        if len(expression_str) > MAX_EXPR_LENGTH:
            raise ValueError(
                f"Expression length {len(expression_str)} exceeds maximum allowed {MAX_EXPR_LENGTH}"
            )
        self.raw_expression = expression_str
        self.bytecode = []
        self.variables = set()
        self._compile()

    @staticmethod
    def _normalize_statement(stmt: str) -> str:
        """Normalize Rhai operators (^, &&, ||) to Python AST operators."""
        s = stmt.replace("^", "**")
        s = s.replace("&&", " and ").replace("||", " or ")
        return s.strip()

    @staticmethod
    def _preprocess_rhai_syntax(expr: str) -> str:
        """Normalize single/multi-statement Rhai syntax for diagnostics without exponential expansion."""
        cleaned = expr.strip().rstrip(";")
        if not cleaned:
            return ""
        lines = [line.strip() for line in cleaned.split(";") if line.strip()]
        if not lines:
            return ""
        return ExpressionEngine._normalize_statement("; ".join(lines))

    def _compile(self) -> None:
        """Compile expression string (including chained 'let' assignments) into linear-size bytecode."""
        raw_stripped = self.raw_expression.strip()
        if not raw_stripped:
            raise SyntaxError("Empty expression syntax.")

        statements = [s.strip() for s in raw_stripped.split(";") if s.strip()]
        if not statements:
            raise SyntaxError("Empty expression syntax.")
        if len(statements) > MAX_STATEMENTS:
            raise ValueError(
                f"Statement count {len(statements)} exceeds maximum allowed {MAX_STATEMENTS}"
            )

        self.cleaned_expression = self._normalize_statement("; ".join(statements))

        instructions: List[Instruction] = []
        external_vars: Set[str] = set()
        bound_locals: Set[str] = set()

        _CMP_OP_MAP = {
            ast.Lt: OpCode.CMP_LT,
            ast.LtE: OpCode.CMP_LE,
            ast.Gt: OpCode.CMP_GT,
            ast.GtE: OpCode.CMP_GE,
            ast.Eq: OpCode.CMP_EQ,
            ast.NotEq: OpCode.CMP_NE,
        }

        _ASSIGN_RE = re.compile(r"^(?:let\s+)?([a-zA-Z_][a-zA-Z0-9_]*)\s*=(?!=)\s*(.+)$", re.DOTALL)

        def _emit(instr: Instruction) -> None:
            if len(instructions) >= MAX_BYTECODE_OPS:
                raise ValueError(
                    f"Compiled bytecode size exceeds maximum allowed {MAX_BYTECODE_OPS} instructions"
                )
            instructions.append(instr)

        def visit(node: ast.AST, depth: int = 0) -> None:
            if depth > MAX_AST_DEPTH:
                raise ValueError(
                    f"Expression AST depth {depth} exceeds maximum allowed {MAX_AST_DEPTH}"
                )

            if isinstance(node, ast.Expression):
                visit(node.body, depth + 1)

            elif isinstance(node, ast.Constant):
                if isinstance(node.value, bool):
                    val: Any = bool(node.value)
                elif isinstance(node.value, (int, float)):
                    val = float(node.value)
                else:
                    raise ValueError(f"Unsupported constant type: {type(node.value).__name__}")
                _emit(Instruction(OpCode.LOAD_CONST, val))

            elif isinstance(node, ast.Name):
                name = node.id
                if name in MATH_CONSTANTS:
                    _emit(Instruction(OpCode.LOAD_CONST, MATH_CONSTANTS[name]))
                else:
                    if name not in bound_locals:
                        external_vars.add(name)
                    _emit(Instruction(OpCode.LOAD_VAR, name))

            elif isinstance(node, ast.UnaryOp):
                visit(node.operand, depth + 1)
                if isinstance(node.op, ast.USub):
                    _emit(Instruction(OpCode.NEG))
                elif isinstance(node.op, ast.Not):
                    _emit(Instruction(OpCode.BOOL_NOT))
                elif isinstance(node.op, ast.UAdd):
                    pass
                else:
                    raise ValueError(f"Unsupported unary operator: {type(node.op).__name__}")

            elif isinstance(node, ast.BinOp):
                visit(node.left, depth + 1)
                visit(node.right, depth + 1)
                if isinstance(node.op, ast.Add):
                    _emit(Instruction(OpCode.ADD))
                elif isinstance(node.op, ast.Sub):
                    _emit(Instruction(OpCode.SUB))
                elif isinstance(node.op, ast.Mult):
                    _emit(Instruction(OpCode.MUL))
                elif isinstance(node.op, ast.Div):
                    _emit(Instruction(OpCode.DIV))
                elif isinstance(node.op, ast.Pow):
                    _emit(Instruction(OpCode.POW))
                else:
                    raise ValueError(f"Unsupported binary operator: {type(node.op).__name__}")

            elif isinstance(node, ast.Compare):
                if len(node.ops) != len(node.comparators) or len(node.ops) == 0:
                    raise SyntaxError("Malformed comparison expression.")
                left_node = node.left
                for op_idx, (op_obj, comp_node) in enumerate(zip(node.ops, node.comparators)):
                    op_code = _CMP_OP_MAP.get(type(op_obj))
                    if op_code is None:
                        raise ValueError(f"Unsupported comparison operator: {type(op_obj).__name__}")
                    visit(left_node, depth + 1)
                    visit(comp_node, depth + 1)
                    _emit(Instruction(op_code))
                    if op_idx > 0:
                        _emit(Instruction(OpCode.BOOL_AND))
                    left_node = comp_node

            elif isinstance(node, ast.BoolOp):
                if isinstance(node.op, ast.And):
                    bool_code = OpCode.BOOL_AND
                elif isinstance(node.op, ast.Or):
                    bool_code = OpCode.BOOL_OR
                else:
                    raise ValueError(f"Unsupported boolean operator: {type(node.op).__name__}")
                visit(node.values[0], depth + 1)
                for val_node in node.values[1:]:
                    visit(val_node, depth + 1)
                    _emit(Instruction(bool_code))

            elif isinstance(node, ast.Call):
                func_name = getattr(node.func, "id", None)
                if not func_name or func_name not in MATH_FUNCTIONS:
                    raise ValueError(f"Unsupported math function: '{func_name}'")
                if node.keywords:
                    raise ValueError("Keyword arguments are not supported in expression function calls.")
                for arg in node.args:
                    visit(arg, depth + 1)
                _emit(Instruction(OpCode.CALL_FUNC, (func_name, len(node.args))))

            else:
                raise ValueError(f"Unsupported AST node: {type(node).__name__}")

        n_stmts = len(statements)
        for idx, stmt in enumerate(statements):
            is_last = idx == n_stmts - 1
            m = _ASSIGN_RE.match(stmt)
            if not is_last:
                if not m:
                    raise SyntaxError(f"Invalid assignment syntax in '{stmt}'")
                var_name, rhs_raw = m.group(1), m.group(2)
                if keyword.iskeyword(var_name):
                    raise SyntaxError(f"Invalid variable name '{var_name}' in '{stmt}'")
                rhs_norm = self._normalize_statement(rhs_raw)
                try:
                    tree = ast.parse(rhs_norm, mode="eval")
                except SyntaxError as e:
                    raise SyntaxError(f"Invalid expression syntax in '{self.raw_expression}': {e}") from e
                visit(tree, 0)
                _emit(Instruction(OpCode.STORE_VAR, var_name))
                bound_locals.add(var_name)
            else:
                if m:
                    var_name, rhs_raw = m.group(1), m.group(2)
                    if keyword.iskeyword(var_name):
                        raise SyntaxError(f"Invalid variable name '{var_name}' in '{stmt}'")
                    expr_norm = self._normalize_statement(rhs_raw)
                elif stmt.startswith("let ") or re.match(r"^let\b", stmt):
                    raise SyntaxError(f"Invalid assignment syntax in '{stmt}'")
                else:
                    expr_norm = self._normalize_statement(stmt)
                try:
                    tree = ast.parse(expr_norm, mode="eval")
                except SyntaxError as e:
                    raise SyntaxError(f"Invalid expression syntax in '{self.raw_expression}': {e}") from e
                visit(tree, 0)
                _emit(Instruction(OpCode.RETURN))

        self.bytecode = instructions
        self.variables = external_vars

    def evaluate(self, env: Dict[str, Any]) -> Union[float, bool, np.ndarray]:
        """Execute bytecode using vectorized array operands in `env`."""
        if not self.bytecode:
            raise ValueError("No expression compiled on ExpressionEngine.")
        for var in self.variables:
            if var not in env:
                raise ValueError(f"Expression requires variable '{var}', but it was not provided.")

        local_env: Dict[str, Any] = dict(env)
        stack: List[Any] = []

        for instr in self.bytecode:
            op = instr.opcode

            if op == OpCode.LOAD_CONST:
                stack.append(instr.arg)

            elif op == OpCode.LOAD_VAR:
                var_name = instr.arg
                if var_name not in local_env:
                    raise ValueError(f"Expression requires variable '{var_name}', but it was not provided.")
                stack.append(local_env[var_name])

            elif op == OpCode.STORE_VAR:
                local_env[instr.arg] = stack.pop()

            elif op == OpCode.ADD:
                b = stack.pop()
                a = stack.pop()
                stack.append(a + b)

            elif op == OpCode.SUB:
                b = stack.pop()
                a = stack.pop()
                stack.append(a - b)

            elif op == OpCode.MUL:
                b = stack.pop()
                a = stack.pop()
                stack.append(a * b)

            elif op == OpCode.DIV:
                b = stack.pop()
                a = stack.pop()
                a_is_vec = isinstance(a, np.ndarray) and a.ndim > 0
                b_is_vec = isinstance(b, np.ndarray) and b.ndim > 0
                if not a_is_vec and not b_is_vec and float(b) == 0.0:
                    raise ZeroDivisionError("division by zero")
                stack.append(a / b)

            elif op == OpCode.POW:
                b = stack.pop()
                a = stack.pop()
                stack.append(a ** b)

            elif op == OpCode.NEG:
                a = stack.pop()
                stack.append(-a)

            elif op == OpCode.CMP_LT:
                b = stack.pop()
                a = stack.pop()
                stack.append(a < b)

            elif op == OpCode.CMP_LE:
                b = stack.pop()
                a = stack.pop()
                stack.append(a <= b)

            elif op == OpCode.CMP_GT:
                b = stack.pop()
                a = stack.pop()
                stack.append(a > b)

            elif op == OpCode.CMP_GE:
                b = stack.pop()
                a = stack.pop()
                stack.append(a >= b)

            elif op == OpCode.CMP_EQ:
                b = stack.pop()
                a = stack.pop()
                stack.append(a == b)

            elif op == OpCode.CMP_NE:
                b = stack.pop()
                a = stack.pop()
                stack.append(a != b)

            elif op == OpCode.BOOL_AND:
                b = stack.pop()
                a = stack.pop()
                stack.append(np.logical_and(a, b))

            elif op == OpCode.BOOL_OR:
                b = stack.pop()
                a = stack.pop()
                stack.append(np.logical_or(a, b))

            elif op == OpCode.BOOL_NOT:
                a = stack.pop()
                stack.append(np.logical_not(a))

            elif op == OpCode.CALL_FUNC:
                func_name, num_args = instr.arg
                fn = MATH_FUNCTIONS[func_name]
                args = [stack.pop() for _ in range(num_args)]
                args.reverse()
                res = fn(*args)
                stack.append(res)

            elif op == OpCode.RETURN:
                break

        if len(stack) != 1:
            raise RuntimeError(f"Corrupted VM stack, expected 1 item, got {len(stack)}")
        return stack[0]

    def evaluate_boundary_profile(
        self,
        *args: Any,
        expr: Optional[str] = None,
        x: Any = None,
        y: Any = None,
        z: Any = None,
        t: float = 0.0,
        extra_vars: Optional[Dict[str, Any]] = None,
    ) -> np.ndarray:
        """Evaluate a boundary field profile ``u(x, y, z, t)`` or predicate mask.

        Supports ``evaluate_boundary_profile(expr, x, y, z, ...)``,
        ``ExpressionEngine(expr).evaluate_boundary_profile(x, y, z, ...)``,
        and keyword arguments ``evaluate_boundary_profile(x=..., y=..., z=..., t=...)``.
        """
        if args:
            if isinstance(args[0], str):
                expr = args[0]
                rest = args[1:]
                if len(rest) >= 1 and x is None:
                    x = rest[0]
                if len(rest) >= 2 and y is None:
                    y = rest[1]
                if len(rest) >= 3 and z is None:
                    z = rest[2]
                if len(rest) >= 4:
                    t = rest[3]
                if len(rest) >= 5 and extra_vars is None:
                    extra_vars = rest[4]
                if len(rest) > 5:
                    raise TypeError(f"evaluate_boundary_profile takes at most 6 positional arguments, got {len(args)}")
            else:
                if len(args) >= 1 and x is None:
                    x = args[0]
                if len(args) >= 2 and y is None:
                    y = args[1]
                if len(args) >= 3 and z is None:
                    z = args[2]
                if len(args) >= 4:
                    t = args[3]
                if len(args) >= 5 and extra_vars is None:
                    extra_vars = args[4]
                if len(args) > 5:
                    raise TypeError(f"evaluate_boundary_profile takes at most 5 positional coordinate arguments, got {len(args)}")

        if expr is not None:
            self.compile_expression(expr)
        if x is None or y is None or z is None:
            raise TypeError("evaluate_boundary_profile requires x, y, and z coordinates")

        x_arr = np.asarray(x, dtype=np.float64)
        y_arr = np.asarray(y, dtype=np.float64)
        z_arr = np.asarray(z, dtype=np.float64)
        t_val = float(t)

        env: Dict[str, Any] = {
            "x": x_arr,
            "y": y_arr,
            "z": z_arr,
            "t": t_val,
        }
        if extra_vars:
            env.update(extra_vars)

        result = self.evaluate(env)
        if np.isscalar(result):
            if isinstance(result, (bool, np.bool_)):
                return np.full_like(x_arr, bool(result), dtype=bool)
            return np.full_like(x_arr, float(result), dtype=np.float64)
        res_arr = np.asarray(result)
        if res_arr.dtype == np.bool_:
            return res_arr
        return res_arr.astype(np.float64)

    def evaluate_source_term(
        self,
        x: np.ndarray,
        y: np.ndarray,
        z: np.ndarray,
        t: float = 0.0,
        fields: Optional[Dict[str, np.ndarray]] = None,
    ) -> np.ndarray:
        """Evaluate volumetric source term S(x, y, z, t, fields...)."""
        env: Dict[str, Any] = {
            "x": np.asarray(x, dtype=np.float64),
            "y": np.asarray(y, dtype=np.float64),
            "z": np.asarray(z, dtype=np.float64),
            "t": float(t),
        }
        if fields:
            env.update(fields)

        result = self.evaluate(env)
        if np.isscalar(result):
            return np.full_like(x, float(result), dtype=np.float64)
        return np.asarray(result, dtype=np.float64)
