import { useEffect, useState } from 'react';
import { articles, corrections, snippets, type Article, type ViewId } from '../data/notebook';
import { viewNames } from '../components/NotebookUI';

export interface Route { view: ViewId; article?: string; snippet?: string }
export function parseRoute(): Route {
  const [root, id] = window.location.hash.slice(1).split('/');
  if (root === 'note') {
    const article = articles.find(note => note.id === id);
    if (article) return { view: article.category, article: article.id };
  }
  if (root === 'snippets') return { view: 'snippets', snippet: snippets.some(s => s.id === id) ? id : undefined };
  if (Object.prototype.hasOwnProperty.call(viewNames, root)) return { view: root as ViewId };
  return { view: 'overview' };
}
export function useLocalState<T,>(key: string, initial: T) {
  const [value, setValue] = useState<T>(() => {
    try { const stored = localStorage.getItem(key); return stored ? JSON.parse(stored) as T : initial; }
    catch { return initial; }
  });
  useEffect(() => {
    try { localStorage.setItem(key, JSON.stringify(value)); }
    catch { /* The notebook remains usable when browser storage is unavailable. */ }
  }, [key, value]);
  return [value, setValue] as const;
}
export function downloadFile(filename: string, contents: string, type = 'text/plain') {
  const url = URL.createObjectURL(new Blob([contents], { type: `${type};charset=utf-8` }));
  const anchor = document.createElement('a');
  anchor.href = url; anchor.download = filename;
  document.body.appendChild(anchor); anchor.click(); anchor.remove();
  window.setTimeout(() => URL.revokeObjectURL(url), 1000);
}
export function noteMarkdown(article: Article) {
  return `# ${article.title}\n\n${article.summary}\n\n${article.sections.map(section => `## ${section.heading}\n\n${section.body.join('\n\n')}\n\n${section.formula ? `\`${section.formula}\`\n\n` : ''}${section.points?.map(point => `- ${point}`).join('\n') || ''}${section.snippet ? `\n\nSee code: ${snippets.find(s => s.id === section.snippet)?.filename}\n` : ''}`).join('\n\n')}\n\n## Sources\n${article.sources.map(source => `- [${source.label}](${source.url})`).join('\n')}\n`;
}
export function notebookMarkdown() {
  return `# continuum / Solver engineering notebook\n\nA technical review, not a complete CFD solver. CPU reference tests were compiled and executed; CUDA examples are not nvcc-verified.\n\n${articles.map(noteMarkdown).join('\n---\n\n')}\n\n# Technical corrections\n\n${corrections.map(c => `## ${c.title}\n\nOriginal: ${c.before}\n\nCorrection: ${c.after}`).join('\n\n')}\n\n# Code library\n\n${snippets.map(s => `## ${s.filename}\n\n${s.description}\n\nStatus: ${s.status}. ${s.note}\n\n\`\`\`${s.language}\n${s.code}\`\`\`\n`).join('\n')}`;
}