import { useState } from 'react';
import { AnimatePresence, motion } from 'framer-motion';
import { ArrowRight, ArrowUpRight, Braces, ShieldCheck, Terminal } from 'lucide-react';
import { articles, categories, snippets, type ViewId } from '../data/notebook';
import { CodeBlock } from './CodeBlock';
import { MeshPreview } from './MeshPreview';
import { categoryIcons, SectionHeading, viewNames, WorkspaceTabs } from './NotebookUI';

export function Overview({ onArticle, onNavigate, onSnippet }: { onArticle: (id: string) => void; onNavigate: (view: ViewId) => void; onSnippet: (id: string) => void }) {
  const [codeTab, setCodeTab] = useState(0);
  const featured = ['two-class-dispatch', 'mesh-quality', 'halo-contract'].map(id => articles.find(a => a.id === id)!);
  const preview = ['layout', 'base', 'halo'].map(id => snippets.find(s => s.id === id)!)[codeTab];
  const previewCode = codeTab === 0
    ? preview.code.slice(preview.code.indexOf('// These arrays'), preview.code.indexOf('struct GradientSoA')).trimEnd()
    : preview.code.split('\n').slice(codeTab === 1 ? 14 : 3, codeTab === 1 ? 27 : 15).join('\n');
  return <>
    <section className="overview-hero">
      <motion.div className="hero-copy" initial={{ opacity: 0, y: 12 }} animate={{ opacity: 1, y: 0 }} transition={{ duration: 0.5 }}>
        <div className="eyebrow"><span />THE CONTINUUM NOTEBOOK</div>
        <h1>Good physics.<br /><span>Better engineering.</span></h1>
        <p>Fundierte Ideen, korrigierte Konzepte und nutzbarer Code<br className="desktop-break" /> f&uuml;r deinen CFD- und Multiphysics-Solver.</p>
        <div className="hero-actions">
          <button className="primary-button" onClick={() => onNavigate('review')}>Explore the review<ArrowRight size={16} /></button>
          <button className="text-button" onClick={() => onNavigate('snippets')}><Braces size={16} />Open the code library<ArrowUpRight size={14} /></button>
        </div>
      </motion.div>
      <MeshPreview />
    </section>
    <WorkspaceTabs active="overview" onNavigate={onNavigate} />
    <motion.section className="home-foundations" initial={{ opacity: 0, y: 14 }} animate={{ opacity: 1, y: 0 }} transition={{ delay: 0.15, duration: 0.5 }}>
      <SectionHeading title="Start with what matters." subtitle="Drei Grundlagen, die deinen Solver wirklich weiterbringen." action={<button className="subtle-link" onClick={() => onNavigate('review')}>All engineering notes<ArrowUpRight size={14} /></button>} />
      <div className="foundation-grid">
        <div className="featured-notes">{featured.map((article, index) => <button key={article.id} className="featured-note" onClick={() => onArticle(article.id)}>
          <span className="note-index">0{index+1}</span>
          <div className="featured-content"><div className="note-kicker">{['NUMERICS FIRST', 'KNOW YOUR MESH', 'THINK DISTRIBUTED'][index]}{index === 0 && <span className="start-here">START HERE</span>}</div><h3>{article.title}</h3><p>{article.summary}</p><div className="note-meta"><span>{viewNames[article.category]}</span><span className="meta-dot" /><span>{article.minutes} min read</span></div></div>
          <ArrowUpRight className="note-arrow" size={19} />
        </button>)}</div>
        <div className="quick-code"><div className="quick-code-heading"><span><Terminal size={14} />A SMALL BUILDING BLOCK</span><div className="quick-code-tabs">{['C++20', 'CUDA', 'MPI'].map((tab, i) => <button key={tab} className={codeTab === i ? 'active' : ''} onClick={() => setCodeTab(i)} aria-pressed={codeTab === i}>{tab}</button>)}</div></div>
          <AnimatePresence mode="wait"><motion.div key={preview.id} initial={{ opacity: 0 }} animate={{ opacity: 1 }} exit={{ opacity: 0 }} transition={{ duration: 0.14 }}><CodeBlock code={previewCode} filename={preview.filename} compact onOpen={() => onSnippet(preview.id)} /></motion.div></AnimatePresence>
          <p className="quick-code-caption"><span className="tiny-square" />Small patterns. Explicit assumptions. No black boxes.</p>
        </div>
      </div>
    </motion.section>
    <section className="explore-section"><SectionHeading title="Find your next layer." subtitle="Ein guter Solver entsteht an den Schnittstellen." /><div className="discipline-grid">{categories.slice(0,4).map(category => {
      const Icon = categoryIcons[category.id];
      return <button key={category.id} className="discipline-link" onClick={() => onNavigate(category.id)}><Icon size={21} /><div><h3>{category.title}</h3><p>{category.description}</p></div><span>{String(articles.filter(a => a.category === category.id).length).padStart(2, '0')}</span><ArrowUpRight size={15} /></button>;
    })}</div></section>
    <div className="certainty-note"><ShieldCheck size={19} /><div><h3>Strong ideas. Honest boundaries.</h3><p>Keine garantierten Speedups. Keine magischen Netze. Jede Optimierung beginnt mit einer pr&uuml;fbaren Annahme.</p></div><button className="subtle-link" onClick={() => onArticle('verification-first')}>The verification mindset<ArrowRight size={14} /></button></div>
  </>;
}