const KEYWORDS = new Set([
  "fn", "let", "mut", "pub", "struct", "enum", "impl", "trait", "for", "while", "loop", "if",
  "else", "match", "return", "use", "mod", "crate", "self", "Self", "const", "static", "where",
  "as", "in", "ref", "move", "unsafe", "dyn", "async", "await", "break", "continue", "type",
  "extern", "super", "true", "false",
]);

const TYPES = new Set([
  "u8", "u16", "u32", "u64", "usize", "i8", "i32", "i64", "f32", "f64", "bool", "str", "String",
  "Vec", "HashMap", "HashSet", "Option", "Result", "Arc", "RwLock", "Mutex", "App", "World",
  "Plugin", "Commands", "Query", "Res", "ResMut", "Resource", "Component", "Event", "Systems",
  "Vec3", "Vec4", "Mat4", "Surface", "Device", "Queue", "Instance", "AdapterInfo", "Backends",
  "Limits", "Manifest", "PluginId", "PluginCtx", "PanelSpec", "RenderContext", "Node", "SectionPlane",
  "ClipParams", "MeshHandle", "ClippedMesh", "GpuDevice", "PipelineCache", "RenderConfig", "Api",
  "SystemState", "EguiCtx", "Viewport", "DrawList", "IndexFormat", "MemoryHints", "DeviceDescriptor",
  "InstanceDescriptor", "RequestAdapterOptions", "PowerPreference", " anyhow",
]);

const esc = (s: string) =>
  s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");

const TOKEN =
  /(\/\/[^\n]*)|("(?:\\.|[^"\\])*"|r#"(?:.|\n)*?"#)|(#\!?\[[^\]\n]*\])|('[A-Za-z_]\w*)|(\b[A-Za-z_]\w*!)|(\b[A-Za-z_]\w*\b)|(\b\d[\d_]*(?:\.\d+)?\b)|([{}()[\];,.:&*<>=+\-|!?@%^~/\\'"])/g;

export function highlightRust(src: string): string {
  let out = "";
  let last = 0;
  let m: RegExpExecArray | null;
  TOKEN.lastIndex = 0;
  while ((m = TOKEN.exec(src))) {
    out += esc(src.slice(last, m.index));
    const [tok, comment, str, attr, life, macro, word, num] = m;
    if (comment) out += span("c", comment);
    else if (str) out += span("s", str);
    else if (attr) out += span("a", attr);
  else if (life) out += span("l", life);
    else if (macro) out += span("m", macro);
    else if (word) {
      if (KEYWORDS.has(word)) out += span("k", word);
      else if (TYPES.has(word)) out += span("t", word);
      else if (/^[A-Z]/.test(word)) out += span("t", word);
      else out += esc(word);
    } else if (num) out += span("n", num);
    else out += span("p", tok);
    last = m.index + tok.length;
  }
  out += esc(src.slice(last));
  return out;
}

const CLS: Record<string, string> = {
  c: "text-[#4e5f72] italic",
  s: "text-[#93dd6d]",
  a: "text-[#93a4b6]",
  m: "text-[#ff8f7a]",
  k: "text-[#f5a524]",
  t: "text-[#4fd2ee]",
  n: "text-[#a294e8]",
  l: "text-[#e0a86b] italic",
  p: "text-[#8fa3b5]",
};

function span(kind: string, text: string) {
  return `<span class="${CLS[kind]}">${esc(text)}</span>`;
}
