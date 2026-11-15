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

    def __init__(self, expression_str: str):
        self.raw_expression = expression_str
        self.cleaned_expression = self._preprocess_rhai_syntax(expression_str)
        self.bytecode: List[Instruction] = []
        self.variables: Set[str] = set()
        self._compile()

    @staticmethod
    def _preprocess_rhai_syntax(expr: str) -> str:
        """Handle Rhai/C-style syntax: strips 'let' statements and semicolons."""
        cleaned = expr.strip()
        # Remove trailing semicolons
        cleaned = cleaned.rstrip(";")
        # If expression has multiple statements e.g. "let u = ...; u"
        lines = [line.strip() for line in cleaned.split(";") if line.strip()]
        if len(lines) > 1:
            # Last statement is return value
            return_expr = lines[-1]
            # Replace variable definitions backwards
            for line in reversed(lines[:-1]):
                m = re.match(r"(?:let\s+)?([a-zA-Z_][a-zA-Z0-9_]*)\s*=\s*(.+)", line)
                if m:
                    var_name, val_expr = m.group(1), m.group(2)
                    return_expr = re.sub(rf"\b{var_name}\b", f"({val_expr})", return_expr)
            cleaned = return_expr
        elif len(lines) == 1:
            m = re.match(r"(?:let\s+)?([a-zA-Z_][a-zA-Z0-9_]*)\s*=\s*(.+)", lines[0])
            if m:
                cleaned = m.group(2)
            else:
                cleaned = lines[0]

        # Replace '^' with '**' for exponentiation
        cleaned = cleaned.replace("^", "**")
        return cleaned

    def _compile(self) -> None:
        """Compile expression string into bytecode instructions via AST."""
        try:
            tree = ast.parse(self.cleaned_expression, mode="eval")
        except SyntaxError as e:
            raise SyntaxError(f"Invalid expression syntax in '{self.raw_expression}': {e}") from e

        instructions: List[Instruction] = []
        variables: Set[str] = set()

        def visit(node: ast.AST) -> None:
            if isinstance(node, ast.Expression):
                visit(node.body)
                instructions.append(Instruction(OpCode.RETURN))

            elif isinstance(node, ast.Constant):
                instructions.append(Instruction(OpCode.LOAD_CONST, float(node.value)))

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
                elif isinstance(node.op, ast.UAdd):
                    pass  # No-op
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

    def evaluate(self, env: Dict[str, Any]) -> Union[float, np.ndarray]:
        """Execute bytecode using vectorized array operands in `env`."""
        # Check required variables
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
        x: np.ndarray,
        y: np.ndarray,
        z: np.ndarray,
        t: float = 0.0,
        extra_vars: Optional[Dict[str, Any]] = None,
    ) -> np.ndarray:
        """Evaluate a boundary field profile u(x, y, z, t)."""
        env: Dict[str, Any] = {
            "x": np.asarray(x, dtype=np.float64),
            "y": np.asarray(y, dtype=np.float64),
            "z": np.asarray(z, dtype=np.float64),
            "t": float(t),
        }
        if extra_vars:
            env.update(extra_vars)

        result = self.evaluate(env)
        if np.isscalar(result):
            # Broadcast scalar to length of x
            return np.full_like(x, float(result), dtype=np.float64)
        return np.asarray(result, dtype=np.float64)

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
