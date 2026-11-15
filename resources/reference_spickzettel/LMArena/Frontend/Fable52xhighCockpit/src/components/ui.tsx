import { useEffect, useId, useLayoutEffect, useRef, useState, type CSSProperties, type KeyboardEvent, type ReactNode, type RefObject } from 'react';
import { Check, Copy } from 'lucide-react';
import { cn } from '../utils/cn';
import { rampCss, type RampOpts } from '../lib/colormaps';
import { fmtSi, inputText, parseQuantity, UNIT_HINT, type Dim } from '../lib/units';

// ----------------------------------------------------------- useSize ----

export function useSize<T extends HTMLElement>(): [RefObject<T | null>, { w: number; h: number }] {
  const ref = useRef<T | null>(null);
  const [size, setSize] = useState({ w: 0, h: 0 });
  useLayoutEffect(() => {
    const el = ref.current;
    if (!el) return;
    const ro = new ResizeObserver(() => {
      const w = Math.round(el.clientWidth);
      const h = Math.round(el.clientHeight);
      setSize((p) => (p.w === w && p.h === h ? p : { w, h }));
    });
    ro.observe(el);
    return () => ro.disconnect();
  }, []);
  return [ref, size];
}

// --------------------------------------------------------- Segmented ----

export interface SegOption<T extends string | number> {
  value: T;
  label: ReactNode;
  title?: string;
}

export function Segmented<T extends string | number>({
  options,
  value,
  onChange,
  label,
  className,
  stretch,
}: {
  options: SegOption<T>[];
  value: T;
  onChange: (v: T) => void;
  label: string;
  className?: string;
  stretch?: boolean;
}) {
  const refs = useRef<(HTMLButtonElement | null)[]>([]);
  const onKey = (e: KeyboardEvent, i: number) => {
    const d = e.key === 'ArrowRight' || e.key === 'ArrowDown' ? 1 : e.key === 'ArrowLeft' || e.key === 'ArrowUp' ? -1 : 0;
    if (!d) return;
    e.preventDefault();
    const n = (i + d + options.length) % options.length;
    onChange(options[n].value);
    refs.current[n]?.focus();
  };
  return (
    <div
      role="radiogroup"
      aria-label={label}
      className={cn('inline-flex max-w-full overflow-x-auto rounded-[4px] border border-line bg-bg0 p-[2px]', stretch && 'flex w-full', className)}
    >
      {options.map((o, i) => {
        const on = o.value === value;
        return (
          <button
            key={String(o.value)}
            ref={(el) => {
              refs.current[i] = el;
            }}
            type="button"
            role="radio"
            aria-checked={on}
            tabIndex={on ? 0 : -1}
            title={o.title}
            onClick={() => onChange(o.value)}
            onKeyDown={(e) => onKey(e, i)}
            className={cn(
              'min-h-[28px] rounded-[3px] px-2.5 text-[13px] leading-none whitespace-nowrap transition-colors max-sm:min-h-[36px]',
              stretch && 'flex-1',
              on ? 'bg-bg4 text-ink shadow-[inset_0_0_0_1px_var(--c-line2)]' : 'text-ink2 hover:bg-bg3 hover:text-ink',
            )}
          >
            {o.label}
          </button>
        );
      })}
    </div>
  );
}

// ------------------------------------------------------------ Switch ----

export function Switch({
  checked,
  onChange,
  label,
  hint,
  disabled,
}: {
  checked: boolean;
  onChange: (v: boolean) => void;
  label: ReactNode;
  hint?: ReactNode;
  disabled?: boolean;
}) {
  return (
    <button
      type="button"
      role="switch"
      aria-checked={checked}
      disabled={disabled}
      onClick={() => onChange(!checked)}
      className={cn(
        'group flex w-full items-start gap-2.5 rounded-[3px] py-1 text-left max-sm:py-2',
        disabled && 'opacity-50',
      )}
    >
      <span
        aria-hidden
        className={cn(
          'relative mt-[2px] h-[16px] w-[28px] shrink-0 rounded-full border transition-colors',
          checked ? 'border-accent-ink bg-accent' : 'border-line2 bg-bg0 group-hover:border-ink3',
        )}
      >
        <span
          className={cn(
            'absolute top-[2px] h-[10px] w-[10px] rounded-full transition-[left,background-color] duration-150',
            checked ? 'left-[14px] bg-white' : 'left-[2px] bg-ink3',
          )}
        />
      </span>
      <span className="min-w-0">
        <span className="block text-[13px] leading-[1.35] text-ink">{label}</span>
        {hint && <span className="mt-0.5 block text-[12px] leading-[1.35] text-ink3">{hint}</span>}
      </span>
    </button>
  );
}

// --------------------------------------------------------- UnitInput ----

/**
 * Eingabefeld, das Einheiten versteht („850 C“, „3 bar“, „1 µm“) und den Wert
 * SI-normiert weitergibt. Die Eingabe bleibt stehen, darunter steht der SI-Wert.
 */
export function UnitInput({
  label,
  dim,
  value,
  onCommit,
  initialText,
  validate,
  echo = true,
  compact,
}: {
  label: ReactNode;
  dim: Dim;
  value: number;
  onCommit: (si: number) => void;
  initialText?: string;
  validate?: (si: number) => string | null;
  echo?: boolean;
  compact?: boolean;
}) {
  const id = useId();
  const [text, setText] = useState(initialText ?? inputText(value, dim));
  const [error, setError] = useState<string | null>(null);
  const committed = useRef(value);

  // Wert wurde von außen geändert (Reset, Null-Kopplung …) → Text nachziehen
  useEffect(() => {
    if (Math.abs(value - committed.current) > 1e-12 * Math.max(1, Math.abs(value))) {
      committed.current = value;
      setText(inputText(value, dim));
      setError(null);
    }
  }, [value, dim]);

  const commit = () => {
    const r = parseQuantity(text, dim);
    if (!r.ok) return setError(r.error);
    const msg = validate?.(r.si) ?? null;
    if (msg) return setError(msg);
    setError(null);
    committed.current = r.si;
    onCommit(r.si);
  };

  return (
    <div className={cn('min-w-0', compact ? '' : 'py-1')}>
      <div className="flex items-baseline justify-between gap-2">
        <label htmlFor={id} className="text-[13px] text-ink2">
          {label}
        </label>
        {echo && !error && (
          <span className="num shrink-0 text-[11.5px] text-ink3" title="An das Backend geht der SI-Wert">
            = {fmtSi(value, dim)}
          </span>
        )}
      </div>
      <input
        id={id}
        className="field num mt-1 text-[13px]"
        value={text}
        inputMode="text"
        autoComplete="off"
        spellCheck={false}
        aria-invalid={!!error}
        aria-describedby={error ? id + '-err' : undefined}
        title={`Einheiten: ${UNIT_HINT[dim]}`}
        onChange={(e) => {
          setText(e.target.value);
          if (error) setError(null);
        }}
        onBlur={commit}
        onKeyDown={(e) => {
          if (e.key === 'Enter') commit();
        }}
      />
      {error && (
        <p id={id + '-err'} role="alert" className="mt-1 text-[12px] leading-snug text-bad-ink">
          {error}
        </p>
      )}
    </div>
  );
}

// -------------------------------------------------------------- Ramp ----

export function Ramp({
  lut,
  className,
  style,
  ...opts
}: { lut: Uint8Array; className?: string; style?: CSSProperties } & RampOpts) {
  return <div aria-hidden className={className} style={{ backgroundImage: rampCss(lut, opts), ...style }} />;
}

// ----------------------------------------------------------- CopyHex ----

export function CopyHex({ hex, className }: { hex: string; className?: string }) {
  const [done, setDone] = useState(false);
  useEffect(() => {
    if (!done) return;
    const t = window.setTimeout(() => setDone(false), 1400);
    return () => window.clearTimeout(t);
  }, [done]);
  return (
    <button
      type="button"
      onClick={() => {
        navigator.clipboard?.writeText(hex).then(
          () => setDone(true),
          () => setDone(false),
        );
      }}
      className={cn(
        'num inline-flex items-center gap-1.5 rounded-[3px] px-1 py-0.5 text-[12.5px] text-ink2 hover:bg-bg4 hover:text-ink',
        className,
      )}
      aria-label={`${hex} kopieren`}
    >
      {hex}
      {done ? <Check size={12} className="text-ok" aria-hidden /> : <Copy size={11} className="opacity-50" aria-hidden />}
      <span className="sr-only" aria-live="polite">
        {done ? 'kopiert' : ''}
      </span>
    </button>
  );
}

// -------------------------------------------------------------- Misc ----

export function Chip({ children, tone = 'neutral', className }: { children: ReactNode; tone?: 'neutral' | 'ok' | 'warn' | 'bad' | 'accent'; className?: string }) {
  const tones = {
    neutral: 'border-line2 text-ink2',
    ok: 'border-ok/50 text-ok',
    warn: 'border-warn/50 text-warn',
    bad: 'border-bad/60 text-bad-ink',
    accent: 'border-accent-ink/50 text-accent-ink',
  } as const;
  return (
    <span
      className={cn(
        'inline-flex items-center gap-1 rounded-[3px] border px-1.5 py-[1px] font-cond text-[12px] font-medium tracking-[0.04em] whitespace-nowrap uppercase',
        tones[tone],
        className,
      )}
    >
      {children}
    </span>
  );
}

export function PanelHead({ title, right, className }: { title: ReactNode; right?: ReactNode; className?: string }) {
  return (
    <div className={cn('flex min-h-[34px] items-center justify-between gap-2 border-b border-line bg-bg2 px-3', className)}>
      <h2 className="label">{title}</h2>
      {right}
    </div>
  );
}

export function Group({ title, right, children }: { title: ReactNode; right?: ReactNode; children: ReactNode }) {
  return (
    <section className="border-b border-line px-3 py-3">
      <div className="mb-2 flex items-center justify-between gap-2">
        <h3 className="label">{title}</h3>
        {right}
      </div>
      {children}
    </section>
  );
}

/** Seitenkopf der Dokument-Ansichten. */
export function PageHead({ kicker, title, children }: { kicker: string; title: string; children?: ReactNode }) {
  return (
    <header className="max-w-[72ch]">
      <p className="label text-accent-ink">{kicker}</p>
      <h1 className="display mt-2 text-[clamp(30px,5vw,46px)] text-ink">{title}</h1>
      {children && <div className="mt-3 space-y-2 text-[15px] leading-[1.55] text-ink2">{children}</div>}
    </header>
  );
}

export function SectionHead({ id, title, children }: { id?: string; title: string; children?: ReactNode }) {
  return (
    <div id={id} className="max-w-[72ch] scroll-mt-6">
      <h2 className="display text-[26px] text-ink">{title}</h2>
      {children && <div className="mt-1.5 text-[14.5px] leading-[1.55] text-ink2">{children}</div>}
    </div>
  );
}
