import { useEffect, useState } from 'react';
import { AnimatePresence, motion } from 'framer-motion';
import { ArrowLeft, ArrowRight, ArrowUpRight, Bookmark, BookOpen, Boxes, Braces, Check, ChevronDown, ChevronRight, Clock3, Download, ExternalLink, FileCode2, Search, ShieldCheck, Sparkles, TriangleAlert } from 'lucide-react';
import { articles, categories, corrections, repositories, roadmap, snippets, type Article, type Snippet, type ViewId } from '../data/notebook';
import { CodeBlock } from './CodeBlock';
import { categoryIcons, EmptyState, GithubIcon, PageHeader, viewNames, WorkspaceTabs } from './NotebookUI';

export function ReviewPage({ onArticle, onNavigate }: { onArticle: (id: string) => void; onNavigate: (view: ViewId) => void }) {
  const [filter, setFilter] = useState('Alle');
  const [open, setOpen] = useState<string[]>(['base-all']);
  const shown = corrections.filter(c => filter === 'Alle' || c.severity === filter);
  return <>
    <PageHeader eyebrow="THE TECHNICAL REVIEW" title="Keep the ideas. Correct the assumptions." description="Die Konzepte sind vielversprechend. Diese 12 Korrekturen machen daraus eine belastbare Grundlage." />
    <WorkspaceTabs active="review" onNavigate={onNavigate} />
    <div className="filter-bar"><div className="filter-buttons">{['Alle', 'Kritisch', 'Wichtig', 'Einordnen'].map(item => <button key={item} className={filter === item ? 'active' : ''} onClick={() => setFilter(item)}>{item}<span>{item === 'Alle' ? corrections.length : corrections.filter(c => c.severity === item).length}</span></button>)}</div><span className="muted-count">{shown.length} corrections</span></div>
    <div className="correction-list">{shown.map(correction => {
      const expanded = open.includes(correction.id);
      return <section key={correction.id} className={`correction-item ${expanded ? 'expanded' : ''}`}>
        <button className="correction-toggle" onClick={() => setOpen(value => value.includes(correction.id) ? value.filter(id => id !== correction.id) : [...value, correction.id])} aria-expanded={expanded}>
          <span className="correction-number">{String(corrections.indexOf(correction)+1).padStart(2, '0')}</span><TriangleAlert size={18} className={correction.severity === 'Kritisch' ? 'orange-text' : ''} /><h3>{correction.title}</h3><span className={`severity severity-${correction.severity.toLowerCase()}`}>{correction.severity}</span><ChevronDown size={17} className="accordion-chevron" />
        </button>
        <AnimatePresence initial={false}>{expanded && <motion.div initial={{ height: 0, opacity: 0 }} animate={{ height: 'auto', opacity: 1 }} exit={{ height: 0, opacity: 0 }} transition={{ duration: 0.22 }} className="correction-body">
          <div className="correction-comparison"><div><div className="list-eyebrow">ORIGINAL ASSUMPTION</div><p>{correction.before}</p></div><div><div className="list-eyebrow orange-text">THE CORRECTION</div><p>{correction.after}</p></div></div>
          <button className="subtle-link orange-text" onClick={() => onArticle(correction.article)}>Read the engineering note<ArrowUpRight size={15} /></button>
        </motion.div>}</AnimatePresence>
      </section>;
    })}</div>
  </>;
}

export function SnippetsPage({ selectedId, onSelect, onDownload }: { selectedId?: string; onSelect: (id: string) => void; onDownload: (snippet: Snippet) => void }) {
  const [filter, setFilter] = useState('All snippets');
  const [query, setQuery] = useState('');
  const shown = snippets.filter(snippet => (filter === 'All snippets' || snippet.label.includes(filter)) && `${snippet.title} ${snippet.filename}`.toLocaleLowerCase().includes(query.toLocaleLowerCase()));
  const selected = shown.find(snippet => snippet.id === selectedId) || shown[0];
  useEffect(() => {
    if (selectedId && !shown.some(snippet => snippet.id === selectedId)) {
      setFilter('All snippets'); setQuery('');
    }
  }, [selectedId]);
  return <>
    <PageHeader eyebrow="COPY. ADAPT. VERIFY." title="Small code. Clear contracts." description={'Neun fokussierte Beispiele. Mit Annahmen, Grenzen und einer ausf\u00fchrbaren CPU-Referenz.'} />
    <div className="filter-bar snippet-filter-bar"><div className="filter-buttons">{['All snippets', 'C++20', 'CUDA', 'MPI', 'PETSc'].map(item => <button key={item} className={filter === item ? 'active' : ''} onClick={() => setFilter(item)}>{item}</button>)}</div><label className="inline-search"><Search size={15} /><input placeholder="Find a snippet..." value={query} onChange={e => setQuery(e.target.value)} aria-label="Filter code snippets" /></label></div>
    {selected ? <div className="snippet-workspace">
      <div className="snippet-list">{shown.map(snippet => <button key={snippet.id} className={`snippet-list-item ${selected.id === snippet.id ? 'active' : ''}`} onClick={() => onSelect(snippet.id)}><FileCode2 size={17} /><span><strong>{snippet.filename}</strong><small>{snippet.title}</small></span>{selected.id === snippet.id && <ChevronRight size={14} />}</button>)}<div className="snippet-list-note"><ShieldCheck size={16} /><p>CPU reference verified.<br />CUDA examples are explicitly labeled.</p></div></div>
      <div className="snippet-detail"><div className="snippet-detail-heading"><div><span className={`snippet-status ${selected.status === 'CPU-getestet' ? 'verified' : ''}`}><i />{selected.status}</span><h2>{selected.title}</h2><p>{selected.description}</p></div><button className="icon-button" onClick={() => onDownload(selected)} aria-label={`Download ${selected.filename}`} title="Download file"><Download size={18} /></button></div><CodeBlock code={selected.code} filename={selected.filename} language={selected.language} /><div className="snippet-contract"><TriangleAlert size={17} /><p>{selected.note}</p></div></div>
    </div> : <EmptyState icon={Search} title="No snippets found." description="Try another language or a shorter search." action={<button className="secondary-button" onClick={() => { setFilter('All snippets'); setQuery(''); }}>Reset filters</button>} />}
  </>;
}

export function LibrariesPage({ initialQuery }: { initialQuery: string }) {
  const [filter, setFilter] = useState('Alle Bibliotheken');
  const [query, setQuery] = useState(initialQuery);
  const [expanded, setExpanded] = useState<string[]>([]);
  useEffect(() => { setQuery(initialQuery); }, [initialQuery]);
  const shown = repositories.filter(repo => (filter === 'Alle Bibliotheken' || repo.category === filter) && `${repo.name} ${repo.description} ${repo.backend}`.toLocaleLowerCase().includes(query.toLocaleLowerCase()));
  return <>
    <PageHeader eyebrow="THE OPEN-SOURCE TOOLBOX" title="Build on good work." description={'Gezielt ausgew\u00e4hlte Bausteine f\u00fcr Kopplung, Kinetik, Numerik und Geometrie. Keine universellen Drop-in-L\u00f6sungen.'} />
    <div className="filter-bar"><div className="filter-buttons">{['Alle Bibliotheken', 'Kopplung', 'Kinetik', 'Numerik', 'Geometrie'].map(item => <button key={item} className={filter === item ? 'active' : ''} onClick={() => setFilter(item)}>{item}</button>)}</div><label className="inline-search"><Search size={15} /><input value={query} onChange={e => setQuery(e.target.value)} placeholder="Find a library..." aria-label="Search libraries" /></label></div>
    <div className="library-table"><div className="library-table-head"><span>LIBRARY / REPOSITORY</span><span>WHAT IT BRINGS</span><span>BACKEND</span><span /></div>{shown.map(repo => <div className="library-row" key={repo.name}>
      <div className="library-row-main"><div className="repository-name"><GithubIcon size={20} /><div><a href={repo.url} target="_blank" rel="noreferrer">{repo.name}<ArrowUpRight size={13} /></a><span>{repo.owner}</span></div></div><p>{repo.description}</p><span className="backend-label">{repo.backend}</span><button className="icon-button" onClick={() => setExpanded(value => value.includes(repo.name) ? value.filter(name => name !== repo.name) : [...value, repo.name])} aria-label={`Integration notes for ${repo.name}`} aria-expanded={expanded.includes(repo.name)}><ChevronDown size={17} /></button></div>
      {expanded.includes(repo.name) && <div className="repository-caution"><TriangleAlert size={15} /><p>{repo.caution}</p><a href={repo.url} target="_blank" rel="noreferrer">Upstream repository<ExternalLink size={12} /></a></div>}
    </div>)}{shown.length === 0 && <EmptyState icon={Boxes} title="No matching libraries." description="Try a different category or search term." action={<button className="secondary-button" onClick={() => { setQuery(''); setFilter('Alle Bibliotheken'); }}>Reset filters</button>} />}</div>
    <p className="table-footnote"><BookOpen size={15} />{shown.length} libraries. Release support, licenses and maintenance should be checked upstream before integration.</p>
  </>;
}

export function ArchitecturePage({ completed, onToggle, onReset, onArticle, onExport }: { completed: string[]; onToggle: (id: string) => void; onReset: () => void; onArticle: (id: string) => void; onExport: () => void }) {
  return <>
    <PageHeader eyebrow="FROM REFERENCE TO RESEARCH" title="A path, not a wish list." description={'Eine sinnvolle Reihenfolge f\u00fcr die Implementierung. Erst Erhaltung und Tests, dann die ambitionierten Ideen.'} />
    <div className="roadmap-toolbar"><div><span className="progress-label">YOUR IMPLEMENTATION PATH</span><p><strong>{completed.length}</strong> of {roadmap.length} milestones complete<span>Saved on this device</span></p></div><div className="roadmap-actions">{completed.length > 0 && <button className="text-button" onClick={onReset}>Reset progress</button>}<button className="secondary-button" onClick={onExport}><Download size={15} />Export architecture</button></div></div>
    <div className="roadmap-progress"><motion.div animate={{ width: `${completed.length / roadmap.length * 100}%` }} transition={{ duration: 0.4 }} /></div>
    <div className="roadmap-list">{roadmap.map(step => <div key={step.id} className={`roadmap-step ${completed.includes(step.id) ? 'complete' : ''}`}><div className="roadmap-line" /><button className="milestone-checkbox" onClick={() => onToggle(step.id)} aria-label={`${completed.includes(step.id) ? 'Mark incomplete' : 'Mark complete'}: ${step.title}`} aria-pressed={completed.includes(step.id)}>{completed.includes(step.id) ? <Check size={17} /> : <span>{step.stage}</span>}</button><div className="roadmap-step-text"><span className="list-eyebrow">{step.scope}</span><h3>{step.title}</h3><p>{step.description}</p></div><button className="subtle-link" onClick={() => onArticle(step.article)}>Read note<ArrowUpRight size={15} /></button></div>)}</div>
    <div className="certainty-note"><Sparkles size={18} /><div><h3>Research is a phase, not a dependency.</h3><p>Dein Solver sollte ohne neuronale Metriken und Mixed Precision bereits korrekt funktionieren.</p></div></div>
  </>;
}

export function ArticleList({ items, saved, onArticle, onSave }: { items: Article[]; saved: string[]; onArticle: (id: string) => void; onSave: (id: string) => void }) {
  return <div className="article-list">{items.map((article, index) => {
    const Icon = categoryIcons[article.category];
    return <div key={article.id} className="article-list-row"><button className="article-list-link" onClick={() => onArticle(article.id)}><span className="article-list-index">{String(index+1).padStart(2,'0')}</span><Icon size={22} /><div><div className="note-kicker">{article.type}<span className="meta-dot" />{article.minutes} MIN READ</div><h2>{article.title}</h2><p>{article.summary}</p></div><ArrowUpRight size={19} /></button><button className={`icon-button bookmark-button ${saved.includes(article.id) ? 'is-saved' : ''}`} onClick={() => onSave(article.id)} aria-label={`${saved.includes(article.id) ? 'Unsave' : 'Save'} ${article.title}`}><Bookmark size={17} fill={saved.includes(article.id) ? 'currentColor' : 'none'} /></button></div>;
  })}</div>;
}

export function ArticlePage({ article, saved, onSave, onBack, onArticle, onSnippet, onExport }: { article: Article; saved: boolean; onSave: () => void; onBack: () => void; onArticle: (id: string) => void; onSnippet: (id: string) => void; onExport: () => void }) {
  const next = articles[(articles.indexOf(article)+1)%articles.length];
  return <>
    <div className="article-topbar"><button className="subtle-link" onClick={onBack}><ArrowLeft size={15} />Back to {viewNames[article.category]}</button><div><button className={`text-button ${saved ? 'orange-text' : ''}`} onClick={onSave}><Bookmark size={15} fill={saved ? 'currentColor' : 'none'} />{saved ? 'Saved' : 'Save note'}</button><button className="icon-button" onClick={onExport} aria-label="Export this note"><Download size={16} /></button></div></div>
    <div className="article-layout"><article className="article-document"><div className="eyebrow"><span />{categories.find(category => category.id === article.category)?.short}</div><h1>{article.title}</h1><p className="article-summary">{article.summary}</p><div className="article-byline"><span><Clock3 size={13} />{article.minutes} min read</span><span className="meta-dot" /><span>{article.type}</span><span className="meta-dot" /><span>Engineering note</span></div>
      {article.sections.map((section, index) => <section className="document-section" id={`section-${index}`} key={section.heading}><h2>{section.heading}</h2>{section.body.map((text, i) => <p key={i}>{text}</p>)}{section.points && <ul>{section.points.map(point => <li key={point}>{point}</li>)}</ul>}{section.formula && <div className="formula"><Braces size={18} /><code>{section.formula}</code></div>}{section.snippet && (() => {
        const snippet = snippets.find(s => s.id === section.snippet)!;
        return <div className="article-code"><CodeBlock code={snippet.code} filename={snippet.filename} language={snippet.language} compact onOpen={() => onSnippet(snippet.id)} /><span className={`snippet-status ${snippet.status === 'CPU-getestet' ? 'verified' : ''}`}><i />{snippet.status}</span><p className="article-snippet-note">{snippet.note}</p></div>;
      })()}</section>)}
      <section className="document-section sources-section"><div className="list-eyebrow">GO TO THE SOURCE</div><h2>Weiterlesen, direkt upstream.</h2>{article.sources.map(source => <a key={source.url} href={source.url} target="_blank" rel="noreferrer"><BookOpen size={15} />{source.label}<ArrowUpRight size={15} /></a>)}</section>
      <div className="next-note"><span className="list-eyebrow">KEEP EXPLORING</span><button onClick={() => onArticle(next.id)}><h3>{next.title}</h3><ArrowRight size={20} /></button></div>
    </article><aside className="article-toc"><div className="list-eyebrow">ON THIS PAGE</div>{article.sections.map((section, i) => <a key={section.heading} href={`#section-${i}`} onClick={event => { event.preventDefault(); document.getElementById(`section-${i}`)?.scrollIntoView({ behavior: 'smooth', block: 'start' }); }}>{section.heading}</a>)}<div className="toc-note"><ShieldCheck size={17} /><p>Ideas are hypotheses.<br />Verification makes them useful.</p></div></aside></div>
  </>;
}