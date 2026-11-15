import { useEffect, useMemo, useRef, useState } from 'react';
import { ArrowUpRight, BookOpen, Boxes, FileCode2, Search } from 'lucide-react';
import { articles, repositories, snippets } from '../data/notebook';
import { Modal } from './Modal';

export function SearchDialog({ onClose, onArticle, onSnippet, onLibraries }: { onClose: () => void; onArticle: (id: string) => void; onSnippet: (id: string) => void; onLibraries: (query: string) => void }) {
  const [query, setQuery] = useState('');
  const [active, setActive] = useState(0);
  const inputRef = useRef<HTMLInputElement>(null);
  const results = useMemo(() => {
    const q = query.toLocaleLowerCase().trim();
    const notes = articles.filter(a => `${a.title} ${a.summary} ${a.category} ${a.sections.map(s => s.heading + s.body.join(' ')).join(' ')}`.toLocaleLowerCase().includes(q)).slice(0, q ? 6 : 4).map(a => ({ id: a.id, label: a.title, description: a.summary, type: 'Engineering note', icon: BookOpen, select: () => onArticle(a.id) }));
    const code = snippets.filter(s => `${s.title} ${s.filename} ${s.description} ${s.label}`.toLocaleLowerCase().includes(q)).slice(0, q ? 5 : 3).map(s => ({ id: s.id, label: s.filename, description: s.title, type: 'Code snippet', icon: FileCode2, select: () => onSnippet(s.id) }));
    const repos = q ? repositories.filter(r => `${r.name} ${r.description} ${r.backend}`.toLocaleLowerCase().includes(q)).slice(0, 3).map(r => ({ id: r.name, label: r.name, description: r.description, type: 'Open-source library', icon: Boxes, select: () => onLibraries(r.name) })) : [];
    return [...notes, ...code, ...repos];
  }, [query, onArticle, onSnippet, onLibraries]);
  useEffect(() => { inputRef.current?.focus(); }, []);
  useEffect(() => { document.getElementById(`search-result-${active}`)?.scrollIntoView({ block: 'nearest' }); }, [active]);
  return <Modal onClose={onClose} className="search-dialog" label="Search the notebook">
    <div className="search-input-wrap"><Search size={21} /><input ref={inputRef} autoFocus value={query} onChange={event => { setQuery(event.target.value); setActive(0); }} placeholder="Search notes, code and libraries..." aria-label="Search notes, code and libraries" onKeyDown={event => {
      if (event.key === 'ArrowDown') { event.preventDefault(); setActive(i => Math.max(0, Math.min(i+1, results.length-1))); }
      if (event.key === 'ArrowUp') { event.preventDefault(); setActive(i => Math.max(i-1, 0)); }
      if (event.key === 'Enter' && results[active]) { results[active].select(); onClose(); }
    }} /></div>
    <div className="search-results"><div className="list-eyebrow">{query ? `${results.length} RESULTS` : 'A GOOD PLACE TO START'}</div>{results.length ? results.map((result, index) => <button id={`search-result-${index}`} key={`${result.type}-${result.id}`} className={`search-result ${index === active ? 'selected' : ''}`} onMouseEnter={() => setActive(index)} onClick={() => { result.select(); onClose(); }}><result.icon size={19} /><span><strong>{result.label}</strong><small>{result.description}</small></span><em>{result.type}</em><ArrowUpRight size={15} /></button>) : <div className="empty-search"><Search size={25} /><h3>No matching notes.</h3><p>Try &quot;halo&quot;, &quot;CUDA&quot; or &quot;Chemie&quot;.</p></div>}</div>
    <div className="search-footer"><span><kbd>&uarr;</kbd><kbd>&darr;</kbd> to navigate <kbd>&crarr;</kbd> to open</span><span><kbd>esc</kbd> to close</span></div>
  </Modal>;
}