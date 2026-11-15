import type { ReactNode } from "react";

/**
 * Minimaler, dependency-freier Tokenizer fuer C++/CUDA-Snippets.
 * Reihenfolge der Gruppen ist priorisiert: Kommentar > String > Praeprozessor >
 * Zahl > Keyword > Typ > Funktionsname.
 */
const KEYWORDS = [
  "__global__",
  "__device__",
  "__host__",
  "__restrict__",
  "alignas",
  "auto",
  "bool",
  "break",
  "case",
  "catch",
  "class",
  "const",
  "constexpr",
  "continue",
  "delete",
  "double",
  "else",
  "enum",
  "explicit",
  "false",
  "float",
  "for",
  "friend",
  "if",
  "inline",
  "int",
  "int32_t",
  "int64_t",
  "uint32_t",
  "uint64_t",
  "long",
  "namespace",
  "new",
  "nullptr",
  "operator",
  "private",
  "public",
  "return",
  "short",
  "signed",
  "sizeof",
  "static",
  "static_cast",
  "struct",
  "switch",
  "template",
  "this",
  "true",
  "typedef",
  "typename",
  "union",
  "unsigned",
  "using",
  "void",
  "volatile",
  "while",
];

const TOKEN_RE = new RegExp(
  [
    "(?<comment>\\/\\/[^\\n]*|\\/\\*[\\s\\S]*?\\*\\/)",
    "(?<str>\"(?:[^\"\\\\\\n]|\\\\.)*\"|'(?:[^'\\\\\\n]|\\\\.)*')",
    "(?<pre>^[ \\t]*#[a-zA-Z]+)",
    "(?<num>\\b(?:0[xX][0-9a-fA-F]+|\\d+(?:\\.\\d+)?(?:[eE][+-]?\\d+)?f?)\\b)",
    `(?<kw>\\b(?:${KEYWORDS.join("|")})\\b)`,
    "(?<type>\\b[A-Z][A-Za-z0-9_]*\\b)",
    "(?<fn>\\b[A-Za-z_]\\w*(?=\\s*\\())",
  ].join("|"),
  "gm",
);

const CLASS_BY_GROUP: Record<string, string> = {
  comment: "text-slate-500 italic",
  str: "text-amber-300",
  pre: "text-violet-300",
  num: "text-orange-300",
  kw: "text-cyan-300",
  type: "text-emerald-300",
  fn: "text-sky-200",
};

export function highlight(code: string): ReactNode[] {
  const out: ReactNode[] = [];
  let last = 0;
  let key = 0;
  let match: RegExpExecArray | null;

  TOKEN_RE.lastIndex = 0;
  while ((match = TOKEN_RE.exec(code)) !== null) {
    if (match.index > last) {
      out.push(<span key={key++}>{code.slice(last, match.index)}</span>);
    }
    const groups = match.groups ?? {};
    const groupName = Object.keys(groups).find((name) => groups[name] !== undefined);
    const cls = groupName ? CLASS_BY_GROUP[groupName] : undefined;
    out.push(
      <span key={key++} className={cls}>
        {match[0]}
      </span>,
    );
    last = match.index + match[0].length;
    if (match[0].length === 0) TOKEN_RE.lastIndex += 1;
  }

  if (last < code.length) {
    out.push(<span key={key++}>{code.slice(last)}</span>);
  }
  return out;
}
