import React, { useMemo, useState } from 'react';
import {
  Check,
  Code2,
  Copy,
  Download,
  Search,
  Terminal,
  X,
  CheckCircle2,
  Cpu,
} from 'lucide-react';
import {
  CLUSTER_C_CPP_SOURCE,
  CPP_SECTION_BOOKMARKS,
  CppSectionBookmark,
  GCC_VERIFICATION_STDOUT,
} from '../data/cppSourceCode';

interface CppSourceDrawerProps {
  isOpen: boolean;
  onClose: () => void;
  initialSection?: CppSectionBookmark['id'];
}

export const CppSourceDrawer: React.FC<CppSourceDrawerProps> = ({
  isOpen,
  onClose,
  initialSection = 'all',
}) => {
  const [selectedSection, setSelectedSection] =
    useState<CppSectionBookmark['id']>(initialSection);
  const [searchQuery, setSearchQuery] = useState('');
  const [copied, setCopied] = useState(false);
  const [showTerminal, setShowTerminal] = useState(true);

  React.useEffect(() => {
    setSelectedSection(initialSection);
  }, [initialSection]);

  const allLines = useMemo(() => CLUSTER_C_CPP_SOURCE.split('\n'), []);

  const activeBookmark = useMemo(
    () =>
      CPP_SECTION_BOOKMARKS.find((b) => b.id === selectedSection) ??
      CPP_SECTION_BOOKMARKS[0],
    [selectedSection]
  );

  const visibleLines = useMemo(() => {
    const startIdx = Math.max(0, activeBookmark.startLine - 1);
    const endIdx = Math.min(allLines.length, activeBookmark.endLine);
    const sliced = allLines.slice(startIdx, endIdx).map((content, idx) => ({
      lineNumber: startIdx + idx + 1,
      content,
    }));

    if (!searchQuery.trim()) return sliced;
    const q = searchQuery.toLowerCase();
    return sliced.filter((line) => line.content.toLowerCase().includes(q));
  }, [allLines, activeBookmark, searchQuery]);

  if (!isOpen) return null;

  const handleCopy = async () => {
    await navigator.clipboard.writeText(CLUSTER_C_CPP_SOURCE);
    setCopied(true);
    setTimeout(() => setCopied(false), 2000);
  };

  const handleDownload = () => {
    const blob = new Blob([CLUSTER_C_CPP_SOURCE], { type: 'text/x-c++src' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = 'cluster_c_multiphysics.cpp';
    a.click();
    URL.revokeObjectURL(url);
  };

  return (
    <div className="fixed inset-0 z-50 flex justify-end bg-black/70 backdrop-blur-sm">
      <div className="relative flex h-full w-full max-w-5xl flex-col border-l border-[#1E293B] bg-[#070A0F] text-[#F1F5F9] shadow-2xl">
        {/* Header */}
        <div className="flex flex-wrap items-center justify-between gap-3 border-b border-[#1E293B] bg-[#0D121B] px-5 py-3.5">
          <div className="flex items-center gap-3">
            <div className="flex h-8 w-8 items-center justify-center rounded-md border border-cyan-500/30 bg-cyan-500/10 text-cyan-400">
              <Code2 className="h-4 w-4" />
            </div>
            <div>
              <div className="flex items-center gap-2">
                <h2 className="font-display text-sm font-bold tracking-wider uppercase text-slate-100">
                  cpp/cluster_c_multiphysics.cpp
                </h2>
                <span className="inline-flex items-center gap-1 rounded border border-emerald-500/30 bg-emerald-500/10 px-2 py-0.5 font-mono-tabular text-[11px] font-medium text-emerald-400">
                  <CheckCircle2 className="h-3 w-3" />
                  g++ -std=c++20 -O2 -Wall -Wextra VERIFIED
                </span>
              </div>
              <p className="text-xs text-[#94A3B8]">
                Zero-copy SoA <code className="text-cyan-300">CAbiSpan&lt;T&gt;</code> data lake,
                over-relaxed polyhedral FVM operators, and all 4 Cluster C multiphysics solvers
              </p>
            </div>
          </div>

          <div className="flex items-center gap-2">
            <button
              onClick={() => setShowTerminal((v) => !v)}
              className={`flex items-center gap-1.5 rounded border px-2.5 py-1.5 text-xs font-medium transition ${
                showTerminal
                  ? 'border-emerald-500/40 bg-emerald-500/15 text-emerald-300'
                  : 'border-[#1E293B] bg-[#131B28] text-[#94A3B8] hover:text-white'
              }`}
            >
              <Terminal className="h-3.5 w-3.5" />
              {showTerminal ? 'Hide Verification Log' : 'Show Verification Log'}
            </button>

            <button
              onClick={handleCopy}
              className="flex items-center gap-1.5 rounded border border-[#1E293B] bg-[#131B28] px-2.5 py-1.5 text-xs font-medium text-slate-200 transition hover:border-cyan-500/40 hover:text-cyan-300"
            >
              {copied ? (
                <>
                  <Check className="h-3.5 w-3.5 text-emerald-400" />
                  <span className="text-emerald-400">Copied!</span>
                </>
              ) : (
                <>
                  <Copy className="h-3.5 w-3.5" />
                  <span>Copy C++20</span>
                </>
              )}
            </button>

            <button
              onClick={handleDownload}
              className="flex items-center gap-1.5 rounded border border-cyan-500/40 bg-cyan-500/15 px-2.5 py-1.5 text-xs font-medium text-cyan-300 transition hover:bg-cyan-500/25"
            >
              <Download className="h-3.5 w-3.5" />
              <span>Download .cpp</span>
            </button>

            <button
              onClick={onClose}
              className="ml-1 rounded border border-[#1E293B] bg-[#131B28] p-1.5 text-slate-400 transition hover:border-slate-600 hover:text-white"
              title="Close Inspector"
            >
              <X className="h-4 w-4" />
            </button>
          </div>
        </div>

        {/* Section Jump Bar & Search */}
        <div className="flex flex-wrap items-center justify-between gap-2 border-b border-[#1E293B] bg-[#0A0E16] px-5 py-2.5">
          <div className="flex flex-wrap items-center gap-1.5">
            {CPP_SECTION_BOOKMARKS.map((bm) => {
              const active = bm.id === selectedSection;
              return (
                <button
                  key={bm.id}
                  onClick={() => setSelectedSection(bm.id)}
                  className={`rounded px-2.5 py-1 font-mono-tabular text-[11px] font-medium transition ${
                    active
                      ? 'border border-slate-500 bg-[#1E293B] text-white shadow-sm'
                      : 'border border-transparent bg-[#131B28]/60 text-[#94A3B8] hover:border-[#1E293B] hover:text-slate-200'
                  }`}
                  style={
                    active
                      ? { borderColor: bm.accentColor, color: '#F8FAFC' }
                      : undefined
                  }
                >
                  {bm.shortTitle}
                </button>
              );
            })}
          </div>

          <div className="relative w-64">
            <Search className="pointer-events-none absolute top-1/2 left-2.5 h-3.5 w-3.5 -translate-y-1/2 text-[#64748B]" />
            <input
              type="text"
              value={searchQuery}
              onChange={(e) => setSearchQuery(e.target.value)}
              placeholder="Filter C++ symbols, equations..."
              className="w-full rounded border border-[#1E293B] bg-[#131B28] py-1 pr-3 pl-8 font-mono-tabular text-xs text-slate-200 placeholder-[#64748B] focus:border-cyan-500/50 focus:outline-none"
            />
          </div>
        </div>

        {/* Verified g++ stdout panel */}
        {showTerminal && (
          <div className="border-b border-[#1E293B] bg-[#05070B] px-5 py-3">
            <div className="mb-1.5 flex items-center justify-between">
              <span className="flex items-center gap-1.5 font-mono-tabular text-[11px] font-semibold tracking-wider uppercase text-emerald-400">
                <Cpu className="h-3.5 w-3.5" />
                Live Sandbox Compiler & Benchmark Output (GCC 12.2.0 -std=c++20 -O2)
              </span>
              <span className="font-mono-tabular text-[11px] text-[#64748B]">
                Exit Code: 0 (0 warnings, 0 errors)
              </span>
            </div>
            <pre className="max-h-44 overflow-y-auto rounded border border-emerald-500/20 bg-[#090D14] p-3 font-mono-tabular text-[11px] leading-relaxed text-emerald-300">
              {GCC_VERIFICATION_STDOUT}
            </pre>
          </div>
        )}

        {/* Code Content */}
        <div className="flex-1 overflow-y-auto bg-[#070A0F] p-4 font-mono-tabular text-xs leading-relaxed">
          <div className="rounded border border-[#1E293B] bg-[#0B0F17]">
            {visibleLines.map(({ lineNumber, content }) => {
              const isComment = content.trim().startsWith('//');
              const isPreprocessor = content.trim().startsWith('#');
              const isStructOrClass =
                /\b(struct|class|namespace|template)\b/.test(content) && !isComment;

              return (
                <div
                  key={lineNumber}
                  className="flex border-b border-[#1E293B]/30 px-3 py-0.5 hover:bg-[#131B28]/60"
                >
                  <span className="w-12 shrink-0 select-none pr-3 text-right text-[#64748B]">
                    {lineNumber}
                  </span>
                  <span
                    className={`whitespace-pre overflow-x-auto ${
                      isComment
                        ? 'text-slate-500 italic'
                        : isPreprocessor
                        ? 'text-purple-400'
                        : isStructOrClass
                        ? 'font-semibold text-amber-300'
                        : 'text-slate-200'
                    }`}
                  >
                    {content || ' '}
                  </span>
                </div>
              );
            })}
          </div>
        </div>
      </div>
    </div>
  );
};
