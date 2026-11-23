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
import math
import re
from typing import Any, Callable, Dict, List, Optional, Set, Tuple, Union
import numpy as np


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
        self.raw_expression = expression_str
        self.cleaned_expression = self._preprocess_rhai_syntax(expression_str)
        if not self.cleaned_expression:
            raise SyntaxError("Empty expression syntax.")
        self.bytecode = []
        self.variables = set()
        self._compile()

    @staticmethod
    def _preprocess_rhai_syntax(expr: str) -> str:
        """Handle Rhai/C-style syntax: strips 'let' statements, boolean ops, and semicolons."""
        cleaned = expr.strip()
        # Remove trailing semicolons
        cleaned = cleaned.rstrip(";")
        # If expression has multiple statements e.g. "let u = ...; u"
        lines = [line.strip() for line in cleaned.split(";") if line.strip()]
        if len(lines) > 1:
            return_expr = lines[-1]
            for line in reversed(lines[:-1]):
                m = re.match(r"(?:let\s+)?([a-zA-Z_][a-zA-Z0-9_]*)\s*=(?!=)\s*(.+)", line)
                if m:
                    var_name, val_expr = m.group(1), m.group(2)
                    return_expr = re.sub(rf"\b{var_name}\b", f"({val_expr})", return_expr)
                elif line.startswith("let "):
                    raise SyntaxError(f"Invalid assignment syntax in '{line}'")
            cleaned = return_expr
        elif len(lines) == 1:
            m = re.match(r"(?:let\s+)?([a-zA-Z_][a-zA-Z0-9_]*)\s*=(?!=)\s*(.+)", lines[0])
            if m:
                cleaned = m.group(2)
            else:
                cleaned = lines[0]

        # Replace '^' with '**' and C-style boolean operators
        cleaned = cleaned.replace("^", "**")
        cleaned = cleaned.replace("&&", " and ").replace("||", " or ")
        return cleaned

    def _compile(self) -> None:
        """Compile expression string into bytecode instructions via AST."""
        try:
            tree = ast.parse(self.cleaned_expression, mode="eval")
        except SyntaxError as e:
            raise SyntaxError(f"Invalid expression syntax in '{self.raw_expression}': {e}") from e

        instructions: List[Instruction] = []
        variables: Set[str] = set()

        _CMP_OP_MAP = {
            ast.Lt: OpCode.CMP_LT,
            ast.LtE: OpCode.CMP_LE,
            ast.Gt: OpCode.CMP_GT,
            ast.GtE: OpCode.CMP_GE,
            ast.Eq: OpCode.CMP_EQ,
            ast.NotEq: OpCode.CMP_NE,
        }

        def visit(node: ast.AST) -> None:
            if isinstance(node, ast.Expression):
                visit(node.body)
                instructions.append(Instruction(OpCode.RETURN))

            elif isinstance(node, ast.Constant):
                val = bool(node.value) if isinstance(node.value, bool) else float(node.value)
                instructions.append(Instruction(OpCode.LOAD_CONST, val))

            elif isinstance(node, ast.Name):
                name = node.id
                if name in MATH_CONSTANTS:
                    instructions.append(Instruction(OpCode.LOAD_CONST, MATH_CONSTANTS[name]))
                else:
                    variables.add(name)
                    instructions.append(Instruction(OpCode.LOAD_VAR, name))

            elif isinstance(node, ast.UnaryOp):
                visit(node.operand)
                if isinstance(node.op, ast.USub):
                    instructions.append(Instruction(OpCode.NEG))
                elif isinstance(node.op, ast.Not):
                    instructions.append(Instruction(OpCode.BOOL_NOT))
                elif isinstance(node.op, ast.UAdd):
                    pass
                else:
                    raise ValueError(f"Unsupported unary operator: {type(node.op).__name__}")

            elif isinstance(node, ast.BinOp):
                visit(node.left)
                visit(node.right)
                if isinstance(node.op, ast.Add):
                    instructions.append(Instruction(OpCode.ADD))
                elif isinstance(node.op, ast.Sub):
                    instructions.append(Instruction(OpCode.SUB))
                elif isinstance(node.op, ast.Mult):
                    instructions.append(Instruction(OpCode.MUL))
                elif isinstance(node.op, ast.Div):
                    instructions.append(Instruction(OpCode.DIV))
                elif isinstance(node.op, ast.Pow):
                    instructions.append(Instruction(OpCode.POW))
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
                    visit(left_node)
                    visit(comp_node)
                    instructions.append(Instruction(op_code))
                    if op_idx > 0:
                        instructions.append(Instruction(OpCode.BOOL_AND))
                    left_node = comp_node

            elif isinstance(node, ast.BoolOp):
                if isinstance(node.op, ast.And):
                    bool_code = OpCode.BOOL_AND
                elif isinstance(node.op, ast.Or):
                    bool_code = OpCode.BOOL_OR
                else:
                    raise ValueError(f"Unsupported boolean operator: {type(node.op).__name__}")
                visit(node.values[0])
                for val_node in node.values[1:]:
                    visit(val_node)
                    instructions.append(Instruction(bool_code))

            elif isinstance(node, ast.Call):
                func_name = getattr(node.func, "id", None)
                if not func_name or func_name not in MATH_FUNCTIONS:
                    raise ValueError(f"Unsupported math function: '{func_name}'")
                for arg in node.args:
                    visit(arg)
                instructions.append(Instruction(OpCode.CALL_FUNC, (func_name, len(node.args))))

            else:
                raise ValueError(f"Unsupported AST node: {type(node).__name__}")

        visit(tree)
        self.bytecode = instructions
        self.variables = variables

    def evaluate(self, env: Dict[str, Any]) -> Union[float, bool, np.ndarray]:
        """Execute bytecode using vectorized array operands in `env`."""
        if not self.bytecode:
            raise ValueError("No expression compiled on ExpressionEngine.")
        for var in self.variables:
            if var not in env:
                raise ValueError(f"Expression requires variable '{var}', but it was not provided.")

        stack: List[Any] = []

        for instr in self.bytecode:
            op = instr.opcode

            if op == OpCode.LOAD_CONST:
                stack.append(instr.arg)

            elif op == OpCode.LOAD_VAR:
                val = env[instr.arg]
                stack.append(val)

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
