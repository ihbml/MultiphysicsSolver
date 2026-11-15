import { useEffect, useState } from 'react';
import { AnimatePresence, motion } from 'framer-motion';
import { ArrowDownToLine, ArrowRight, ArrowUpRight, Bookmark, BookOpen, Boxes, Check, ChevronDown, CircleHelp, Code2, Command, Download, FileCheck2, Layers3, LayoutDashboard, Menu, Moon, Search, ShieldCheck, Sun, Workflow, X, type LucideIcon } from 'lucide-react';
import { articles, categories, corrections, roadmap, snippets, type ViewId } from './data/notebook';
import { ArchitecturePage, ArticleList, ArticlePage, LibrariesPage, ReviewPage, SnippetsPage } from './components/NotebookPages';
import { BrandMark, categoryIcons, EmptyState, GithubIcon, PageHeader, viewNames } from './components/NotebookUI';
import { Overview } from './components/Overview';
import { SearchDialog } from './components/SearchDialog';
import { Modal } from './components/Modal';
import { downloadFile, notebookMarkdown, noteMarkdown, parseRoute, useLocalState, type Route } from './lib/notebook';

export default function App() {
  const [route, setRoute] = useState<Route>(parseRoute);
  const [saved, setSaved] = useLocalState<string[]>('continuum-saved', []);
  const [completed, setCompleted] = useLocalState<string[]>('continuum-progress', []);
  const [theme, setTheme] = useLocalState<'dark' | 'light'>('continuum-theme', 'dark');
  const [searchOpen, setSearchOpen] = useState(false);
  const [helpOpen, setHelpOpen] = useState(false);
  const [mobileOpen, setMobileOpen] = useState(false);
  const [workspaceOpen, setWorkspaceOpen] = useState(false);
  const [libraryQuery, setLibraryQuery] = useState('');
  const [toast, setToast] = useState('');
  const currentArticle = articles.find(a => a.id === route.article);

  useEffect(() => { document.documentElement.dataset.theme = theme; }, [theme]);
  useEffect(() => {
    function update() { setRoute(parseRoute()); window.scrollTo(0, 0); }
    window.addEventListener('popstate', update);
    window.addEventListener('hashchange', update);
    return () => { window.removeEventListener('popstate', update); window.removeEventListener('hashchange', update); };
  }, []);
  useEffect(() => {
    function keydown(event: KeyboardEvent) {
      if ((event.metaKey || event.ctrlKey) && event.key.toLowerCase() === 'k') { event.preventDefault(); setSearchOpen(value => !value); }
      if (event.key === 'Escape') { setMobileOpen(false); setWorkspaceOpen(false); }
    }
    window.addEventListener('keydown', keydown);
    return () => window.removeEventListener('keydown', keydown);
  }, []);
  useEffect(() => {
    if (!toast) return;
    const timer = window.setTimeout(() => setToast(''), 3500);
    return () => window.clearTimeout(timer);
  }, [toast]);
  useEffect(() => { document.title = `${currentArticle?.title || viewNames[route.view]} | continuum`; }, [route, currentArticle]);

  function navigate(view: ViewId, detail?: string) {
    const next: Route = view === 'snippets' && detail ? { view, snippet: detail } : { view };
    history.pushState(null, '', `#${view}${detail ? `/${detail}` : ''}`);
    setRoute(next); setMobileOpen(false); setWorkspaceOpen(false);
    window.scrollTo({ top: 0, behavior: 'instant' });
  }
  function openArticle(id: string) {
    const article = articles.find(a => a.id === id);
    if (!article) return;
    history.pushState(null, '', `#note/${id}`);
    setRoute({ view: article.category, article: id }); setMobileOpen(false);
    window.scrollTo({ top: 0, behavior: 'instant' });
  }
  function toggleSave(id: string) {
    setSaved(value => value.includes(id) ? value.filter(item => item !== id) : [...value, id]);
    setToast(saved.includes(id) ? 'Note removed from your reading list.' : 'Note saved to your reading list.');
  }
  function exportNotes() {
    downloadFile('continuum-notebook.md', notebookMarkdown(), 'text/markdown');
    setToast('Notebook exported as Markdown.');
  }
  function exportArchitecture() {
    const content = `# Solver implementation path\n\n${roadmap.map(step => `- [${completed.includes(step.id) ? 'x' : ' '}] ${step.stage}. ${step.title}\n\n${step.description}\n`).join('\n')}\n## Numerical contracts\n\n${corrections.map(c => `### ${c.title}\n\n${c.after}\n`).join('\n')}`;
    downloadFile('ARCHITECTURE.md', content, 'text/markdown');
    setToast('Implementation path exported to ARCHITECTURE.md.');
  }

  function navItem(view: ViewId, label: string, Icon: LucideIcon, count?: number) {
    return <button key={view} className={`nav-item ${route.view === view ? 'active' : ''}`} aria-current={route.view === view ? 'page' : undefined} onClick={() => { if (view === 'libraries') setLibraryQuery(''); navigate(view); }}>
      <Icon size={17} strokeWidth={1.6} /><span>{label}</span>{count !== undefined && <em>{count.toString().padStart(2, '0')}</em>}
    </button>;
  }
  function pageContent() {
    if (currentArticle) return <ArticlePage article={currentArticle} saved={saved.includes(currentArticle.id)} onSave={() => toggleSave(currentArticle.id)} onBack={() => navigate(currentArticle.category)} onArticle={openArticle} onSnippet={id => navigate('snippets', id)} onExport={() => { downloadFile(`${currentArticle.id}.md`, noteMarkdown(currentArticle), 'text/markdown'); setToast('Engineering note exported.'); }} />;
    if (route.view === 'overview') return <Overview onArticle={openArticle} onNavigate={navigate} onSnippet={id => navigate('snippets', id)} />;
    if (route.view === 'review') return <ReviewPage onArticle={openArticle} onNavigate={navigate} />;
    if (route.view === 'snippets') return <SnippetsPage selectedId={route.snippet} onSelect={id => navigate('snippets', id)} onDownload={snippet => { downloadFile(snippet.filename, snippet.code); setToast(`${snippet.filename} downloaded.`); }} />;
    if (route.view === 'libraries') return <LibrariesPage initialQuery={libraryQuery} />;
    if (route.view === 'architecture') return <ArchitecturePage completed={completed} onToggle={id => setCompleted(value => value.includes(id) ? value.filter(item => item !== id) : [...value, id])} onReset={() => setCompleted([])} onArticle={openArticle} onExport={exportArchitecture} />;
    if (route.view === 'saved') return <>
      <PageHeader eyebrow="YOUR PERSONAL READING LIST" title="Keep the useful things close." description="Deine gespeicherten Engineering Notes. Privat, lokal und ohne Account." />
      {saved.length ? <ArticleList items={articles.filter(a => saved.includes(a.id))} saved={saved} onArticle={openArticle} onSave={toggleSave} /> : <EmptyState icon={Bookmark} title="A little room for good ideas." description="Save an engineering note to find it here later." action={<button className="primary-button" onClick={() => navigate('review')}>Explore the notes<ArrowRight size={15} /></button>} />}
    </>;
    const category = categories.find(c => c.id === route.view)!;
    return <><PageHeader eyebrow={category.short} title={category.title} description={category.description} /><div className="category-label"><BookOpen size={15} />{articles.filter(a => a.category === category.id).length} engineering notes<span>Read critically. Build carefully.</span></div><ArticleList items={articles.filter(a => a.category === category.id)} saved={saved} onArticle={openArticle} onSave={toggleSave} /></>;
  }

  return <div className="app-shell">
    <a className="skip-link" href="#main-content" onClick={event => { event.preventDefault(); document.getElementById('main-content')?.focus(); }}>Skip to content</a>
    {mobileOpen && <button className="mobile-scrim" aria-label="Close navigation" onClick={() => setMobileOpen(false)} />}
    <aside id="main-navigation" className={`sidebar ${mobileOpen ? 'mobile-open' : ''}`} aria-label="Main navigation">
      <button className="brand" onClick={() => navigate('overview')} aria-label="continuum home"><span className="brand-mark"><BrandMark /></span><span className="brand-wordmark">continuum<span>SOLVER ENGINEERING</span></span></button>
      <div className="workspace-picker-wrap" onBlur={event => { if (!event.currentTarget.contains(event.relatedTarget as Node | null)) setWorkspaceOpen(false); }}>
        <button className={`workspace-picker ${workspaceOpen ? 'is-open' : ''}`} onClick={() => setWorkspaceOpen(value => !value)} aria-expanded={workspaceOpen}><span className="workspace-icon"><Layers3 size={18} /></span><span><strong>CFD Research</strong><small>Personal notebook</small></span><ChevronDown size={14} /></button>
        {workspaceOpen && <div className="workspace-menu"><span className="list-eyebrow">YOUR LOCAL WORKSPACE</span><p>No account. No cloud sync.<br />Your reading list stays on this device.</p><button onClick={() => navigate('saved')}><Bookmark size={14} />Saved reading list</button><button onClick={exportNotes}><Download size={14} />Export notebook</button></div>}
      </div>
      <div className="sidebar-scroll">
        <div className="nav-group"><div className="nav-label">WORKSPACE</div>{navItem('overview','Overview',LayoutDashboard)}{navItem('review','Technical review',FileCheck2,corrections.length)}</div>
        <div className="nav-group"><div className="nav-label">EXPLORE TOPICS</div>{categories.map(category => navItem(category.id,category.title,categoryIcons[category.id]))}</div>
        <div className="nav-group resources-nav"><div className="nav-label">RESOURCES</div>{navItem('snippets','Code library',Code2,snippets.length)}{navItem('libraries','Library explorer',Boxes)}{navItem('architecture','Implementation path',Workflow)}{navItem('saved','Saved notes',Bookmark,saved.length)}</div>
      </div>
      <div className="sidebar-bottom">
        <button className="roadmap-prompt" onClick={() => navigate('architecture')}><span>From idea to implementation.</span><strong>Find your starting point<ArrowUpRight size={14} /></strong></button>
        <div className="sidebar-status"><span className="local-indicator" /><span>Local-first. Built for focus.</span><button className="icon-button" onClick={() => setTheme(value => value === 'dark' ? 'light' : 'dark')} title={theme === 'dark' ? 'Switch to light mode' : 'Switch to dark mode'} aria-label={theme === 'dark' ? 'Switch to light mode' : 'Switch to dark mode'}>{theme === 'dark' ? <Sun size={15} /> : <Moon size={15} />}</button><button className="icon-button" onClick={() => setHelpOpen(true)} title="About & shortcuts" aria-label="About and keyboard shortcuts"><CircleHelp size={15} /></button></div>
        <div className="workspace-user"><span className="user-avatar">CR</span><div><strong>Research workspace</strong><small>Make the assumptions explicit.</small></div><span className="version">v1.0</span></div>
      </div>
    </aside>
    <header className="topbar">
      <div className="breadcrumb"><button className="mobile-menu icon-button" onClick={() => setMobileOpen(value => !value)} aria-label="Toggle navigation" aria-controls="main-navigation" aria-expanded={mobileOpen}><Menu size={20} /></button><BookOpen size={15} /><button onClick={() => navigate('overview')}>Workspace</button><span>/</span><strong>{viewNames[route.view]}</strong>{currentArticle && <><span>/</span><span className="breadcrumb-note">Engineering note</span></>}</div>
      <div className="topbar-actions"><button className="global-search" onClick={() => setSearchOpen(true)}><Search size={15} /><span>Search the notebook...</span><kbd><Command size={10} />K</kbd></button><button className="github-button icon-button" onClick={() => { setLibraryQuery(''); navigate('libraries'); }} title="Open-source libraries" aria-label="Explore open-source repositories"><GithubIcon size={19} /></button><span className="topbar-divider" /><button className="export-button" onClick={exportNotes}><ArrowDownToLine size={15} /><span>Export notes</span></button></div>
    </header>
    <main className={`main-content ${currentArticle ? 'article-main' : ''}`} id="main-content" tabIndex={-1}>
      <AnimatePresence mode="wait"><motion.div className="page-container" key={route.article || route.view} initial={{ opacity: 0, y: 6 }} animate={{ opacity: 1, y: 0 }} exit={{ opacity: 0 }} transition={{ duration: 0.19 }}>{pageContent()}</motion.div></AnimatePresence>
      <footer className="page-footer"><span><BrandMark small />A field guide for better solvers.</span><span>Physics first.<span className="footer-dot" />Always verify.</span></footer>
    </main>
    {searchOpen && <SearchDialog onClose={() => setSearchOpen(false)} onArticle={openArticle} onSnippet={id => navigate('snippets', id)} onLibraries={query => { setLibraryQuery(query); navigate('libraries'); }} />}
    {helpOpen && <Modal onClose={() => setHelpOpen(false)} className="help-dialog" label="About continuum"><div className="brand-mark"><BrandMark /></div><div className="eyebrow">THE CONTINUUM NOTEBOOK</div><h2>A field guide, not a black box.</h2><p>Eine technische Referenz f&uuml;r CFD- und Multiphysics-Entwicklung. Die Inhalte unterscheiden verifizierte CPU-Beispiele, illustrative CUDA-Kernels und experimentelle Architekturideen.</p><div className="shortcut-row"><span>Search the notebook</span><div><kbd>Ctrl / Cmd</kbd><kbd>K</kbd></div></div><div className="shortcut-row"><span>Close a dialog</span><kbd>Esc</kbd></div><div className="help-local"><ShieldCheck size={18} /><p>Bookmarks and roadmap progress are saved in your browser. Nothing is sent to a server.</p></div><button className="secondary-button" onClick={exportNotes}><Download size={15} />Download the complete notebook</button></Modal>}
    <AnimatePresence>{toast && <motion.div className="toast" role="status" initial={{ opacity: 0, y: 16 }} animate={{ opacity: 1, y: 0 }} exit={{ opacity: 0, y: 8 }}><span><Check size={15} /></span>{toast}<button className="icon-button" onClick={() => setToast('')} aria-label="Dismiss notification"><X size={14} /></button></motion.div>}</AnimatePresence>
  </div>;
}