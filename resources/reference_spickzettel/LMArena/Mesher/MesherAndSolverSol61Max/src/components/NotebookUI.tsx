import type { ReactNode } from 'react';
import { motion } from 'framer-motion';
import { Cpu, Grid2X2, Network, Orbit, ShieldCheck, type LucideIcon } from 'lucide-react';
import { corrections, snippets, type CategoryId, type ViewId } from '../data/notebook';

export const categoryIcons: Record<CategoryId, LucideIcon> = {
  mesh: Grid2X2, gpu: Cpu, multiphysics: Orbit, scaling: Network, verification: ShieldCheck,
};
export const viewNames: Record<ViewId, string> = {
  overview: 'Overview', review: 'Technical review', snippets: 'Code library', libraries: 'Library explorer',
  architecture: 'Implementation path', saved: 'Saved notes', mesh: 'Mesh & Geometrie', gpu: 'GPU & Performance',
  multiphysics: 'Multiphysics', scaling: 'Distributed Computing', verification: 'Validierung & Tests',
};
export function GithubIcon({ size = 20 }: { size?: number }) {
  return <svg width={size} height={size} viewBox="0 0 24 24" fill="currentColor" aria-hidden="true"><path d="M12 .8a11.4 11.4 0 0 0-3.6 22.22c.57.11.78-.25.78-.55v-2.13c-3.18.7-3.85-1.35-3.85-1.35-.52-1.32-1.27-1.67-1.27-1.67-1.04-.71.08-.7.08-.7 1.15.08 1.75 1.18 1.75 1.18 1.02 1.75 2.67 1.24 3.32.95.1-.74.4-1.24.73-1.53-2.54-.29-5.21-1.27-5.21-5.65 0-1.25.45-2.27 1.18-3.07-.12-.29-.51-1.45.11-3.02 0 0 .96-.31 3.14 1.17a10.9 10.9 0 0 1 5.71 0c2.18-1.48 3.14-1.17 3.14-1.17.62 1.57.23 2.73.11 3.02.74.8 1.18 1.82 1.18 3.07 0 4.39-2.68 5.36-5.23 5.64.41.36.78 1.05.78 2.12v3.14c0 .3.2.66.79.55A11.4 11.4 0 0 0 12 .8Z" /></svg>;
}
export function BrandMark({ small = false }: { small?: boolean }) {
  return <svg width={small ? 21 : 31} height={small ? 24 : 35} viewBox="0 0 31 35" fill="none" aria-hidden="true"><path d="M15.5 2L28 9.2V23.8L15.5 31L3 23.8V9.2L15.5 2Z" stroke="currentColor" strokeWidth="1.5" /><path d="M15.5 8.2L22.6 12.3V20.6L15.5 24.7L8.4 20.6V12.3L15.5 8.2Z" stroke="currentColor" strokeWidth="1.3" /><path d="M3 9.2L15.5 16.5M15.5 16.5L28 9.2M15.5 16.5V31M8.4 12.3L22.6 20.6M22.6 12.3L8.4 20.6" stroke="currentColor" strokeWidth="1.1" /></svg>;
}
export function SectionHeading({ title, subtitle, action }: { title: string; subtitle?: string; action?: ReactNode }) {
  return <div className="section-heading"><div><h2>{title}</h2>{subtitle && <p>{subtitle}</p>}</div>{action}</div>;
}
export function PageHeader({ eyebrow, title, description, children }: { eyebrow: string; title: string; description: string; children?: ReactNode }) {
  return <header className="page-heading"><div className="eyebrow"><span />{eyebrow}</div><h1>{title}</h1><p>{description}</p>{children}</header>;
}
export function EmptyState({ icon: Icon, title, description, action }: { icon: LucideIcon; title: string; description: string; action?: ReactNode }) {
  return <div className="empty-state"><Icon size={31} strokeWidth={1.3} /><h2>{title}</h2><p>{description}</p>{action}</div>;
}
export function WorkspaceTabs({ active, onNavigate }: { active: ViewId; onNavigate: (view: ViewId) => void }) {
  const tabs: { id: ViewId; label: string; count?: number }[] = [
    { id: 'overview', label: 'Overview' }, { id: 'review', label: 'Technical review', count: corrections.length },
    { id: 'snippets', label: 'Code library', count: snippets.length }, { id: 'architecture', label: 'Implementation path' },
  ];
  return <nav className="workspace-tabs" aria-label="Notebook sections">{tabs.map(tab => <button key={tab.id} className={active === tab.id ? 'active' : ''} onClick={() => onNavigate(tab.id)}>{tab.label}{tab.count && <span>{tab.count}</span>}{active === tab.id && <motion.i layoutId="workspace-tab" transition={{ type: 'spring', stiffness: 400, damping: 35 }} />}</button>)}<span className="tabs-caption">BUILT TO BE QUESTIONED</span></nav>;
}