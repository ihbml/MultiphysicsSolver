import { useState } from 'react';
import { Highlight, themes } from 'prism-react-renderer';
import { ArrowUpRight, Check, Copy, FileCode2 } from 'lucide-react';

export async function copyText(text: string) {
  try {
    await navigator.clipboard.writeText(text);
  } catch {
    const area = document.createElement('textarea');
    area.value = text;
    area.style.position = 'fixed';
    area.style.opacity = '0';
    document.body.appendChild(area);
    area.select();
    const copied = document.execCommand('copy');
    area.remove();
    if (!copied) throw new Error('Copy unavailable');
  }
}

interface CodeBlockProps {
  code: string;
  filename: string;
  language?: 'cpp' | 'bash';
  compact?: boolean;
  onOpen?: () => void;
}

export function CodeBlock({ code, filename, language = 'cpp', compact = false, onOpen }: CodeBlockProps) {
  const [copied, setCopied] = useState(false);
  const [failed, setFailed] = useState(false);
  async function copy() {
    try {
      await copyText(code);
      setCopied(true);
      window.setTimeout(() => setCopied(false), 2200);
    } catch {
      setFailed(true);
      window.setTimeout(() => setFailed(false), 2500);
    }
  }
  return (
    <div className={`code-block ${compact ? 'code-compact' : ''}`}>
      <div className="code-toolbar">
        <span className="code-filename"><FileCode2 size={14} />{filename}</span>
        <button className={`copy-button ${copied ? 'is-copied' : ''}`} onClick={copy} aria-label={`Copy ${filename}`} title="Code kopieren">
          {copied ? <Check size={14} /> : <Copy size={14} />}
          <span>{copied ? 'Copied' : failed ? 'Select to copy' : 'Copy'}</span>
        </button>
      </div>
      <div className="code-scroll" tabIndex={0} aria-label={`Code in ${filename}`}>
        <Highlight theme={themes.vsDark} code={code.trimEnd()} language={language}>
          {({ tokens, getLineProps, getTokenProps }) => (
            <pre>
              {tokens.map((line, i) => (
                <div {...getLineProps({ line })} key={i} className="code-line">
                  <span className="line-number" aria-hidden="true">{String(i + 1).padStart(2, '0')}</span>
                  <span className="line-content">{line.map((token, key) => <span {...getTokenProps({ token })} key={key} />)}</span>
                </div>
              ))}
            </pre>
          )}
        </Highlight>
      </div>
      {onOpen && <button className="code-open" onClick={onOpen}><span>Explore this snippet</span><ArrowUpRight size={15} /></button>}
    </div>
  );
}