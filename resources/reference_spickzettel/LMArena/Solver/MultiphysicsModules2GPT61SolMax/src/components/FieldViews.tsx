import { motion } from "framer-motion";
import { BatteryFrame, Parameters, TpsFrame } from "../physics";

export function temperatureColor(T: number, min = 298, max = 650) {
  const f = Math.max(0, Math.min(1, (T - min) / (max - min)));
  const stops = [
    [88, 115, 190],
    [124, 102, 179],
    [177, 99, 159],
    [223, 118, 106],
    [248, 164, 100],
    [246, 205, 123],
  ];
  const index = Math.min(4, Math.floor(f * 5));
  const t = f * 5 - index;
  const rgb = stops[index].map((v, i) =>
    Math.round(v + (stops[index + 1][i] - v) * t),
  );
  return `rgb(${rgb.join(",")})`;
}

const iso = (x: number, y: number, z = 0) => [
  316 + (x - y) * 33,
  91 + (x + y) * 13 - z,
];
const polygon = (points: number[][]) =>
  points.map((p) => p.join(",")).join(" ");

export function BatteryField({
  frame,
  selected,
  onSelect,
  section,
  field,
}: {
  frame: BatteryFrame;
  selected: number;
  onSelect: (i: number) => void;
  section: boolean;
  field: string;
}) {
  const colorFor = (i: number) =>
    field === "State of charge"
      ? `hsl(${151 + frame.soc * 20}, ${25 + frame.soc * 20}%, ${35 + frame.soc * 18}%)`
      : field === "Reaction progress"
        ? temperatureColor(298 + frame.conversion[i] * 352)
        : temperatureColor(
            frame.temperatures[i],
            298,
            Math.max(650, Math.ceil(frame.peak / 100) * 100),
          );
  return (
    <svg
      className="field-svg battery-svg"
      viewBox="0 0 720 300"
      role="img"
      aria-label="Interactive isometric temperature field of a 48-cell battery pack"
    >
      <defs>
        <radialGradient id="pack-light">
          <stop stopColor="#8270b9" stopOpacity=".10" />
          <stop offset="1" stopColor="#8270b9" stopOpacity="0" />
        </radialGradient>
        <filter id="pack-shadow">
          <feGaussianBlur stdDeviation="12" />
        </filter>
        <linearGradient id="cooling-plate" x1="0" y1="0" x2="1" y2="1">
          <stop stopColor="#294e62" />
          <stop offset="1" stopColor="#243440" />
        </linearGradient>
      </defs>
      <ellipse cx="354" cy="160" rx="280" ry="130" fill="url(#pack-light)" />
      <g opacity=".32">
        {Array.from({ length: 16 }, (_, i) => (
          <g key={i}>
            <line
              x1={iso(i - 3, -3)[0]}
              y1={iso(i - 3, -3)[1] + 50}
              x2={iso(i - 3, 10)[0]}
              y2={iso(i - 3, 10)[1] + 50}
              stroke="#303a4b"
              strokeWidth=".6"
            />
            <line
              x1={iso(-3, i - 4)[0]}
              y1={iso(-3, i - 4)[1] + 50}
              x2={iso(12, i - 4)[0]}
              y2={iso(12, i - 4)[1] + 50}
              stroke="#303a4b"
              strokeWidth=".6"
            />
          </g>
        ))}
      </g>
      {section ? (
        <g>
          {frame.temperatures.map((T, i) => (
            <g
              key={i}
              onClick={() => onSelect(i)}
              role="button"
              tabIndex={0}
              onKeyDown={(e) => e.key === "Enter" && onSelect(i)}
              aria-label={`Cell ${i + 1}, ${(T - 273.15).toFixed(1)} degrees Celsius`}
              className="cell-shape"
            >
              <rect
                x={125 + (i % 8) * 59}
                y={28 + Math.floor(i / 8) * 39}
                width="50"
                height="32"
                rx="3"
                fill={colorFor(i)}
                stroke={selected === i ? "#e5e9f4" : "#ffffff20"}
                strokeWidth={selected === i ? 2 : 1}
              />
              <text
                x={150 + (i % 8) * 59}
                y={49 + Math.floor(i / 8) * 39}
                textAnchor="middle"
                fill="#edf1f8"
                fontSize="10"
                fontFamily="monospace"
              >
                {(T - 273.15).toFixed(0)}
              </text>
            </g>
          ))}
        </g>
      ) : (
        <g>
          <ellipse
            cx="350"
            cy="217"
            rx="225"
            ry="41"
            fill="#000"
            opacity=".3"
            filter="url(#pack-shadow)"
          />
          <polygon
            points={polygon([
              iso(-0.35, -0.35, -2),
              iso(8.35, -0.35, -2),
              iso(8.35, 6.35, -2),
              iso(-0.35, 6.35, -2),
            ])}
            fill="url(#cooling-plate)"
            stroke="#456476"
            strokeWidth=".8"
          />
          <polygon
            points={polygon([
              iso(-0.35, 6.35, -2),
              iso(8.35, 6.35, -2),
              iso(8.35, 6.35, -13),
              iso(-0.35, 6.35, -13),
            ])}
            fill="#1e313f"
            stroke="#446071"
            strokeWidth=".7"
          />
          <polygon
            points={polygon([
              iso(8.35, -0.35, -2),
              iso(8.35, 6.35, -2),
              iso(8.35, 6.35, -13),
              iso(8.35, -0.35, -13),
            ])}
            fill="#1a2937"
            stroke="#446071"
            strokeWidth=".7"
          />
          {Array.from({ length: 48 }, (_, i) => {
            const x = i % 8;
            const y = Math.floor(i / 8);
            const z = 61;
            const color = colorFor(i);
            return (
              <g
                key={i}
                onClick={() => onSelect(i)}
                role="button"
                tabIndex={0}
                aria-label={`Cell ${i + 1}, ${(frame.temperatures[i] - 273.15).toFixed(1)} degrees Celsius`}
                onKeyDown={(e) => e.key === "Enter" && onSelect(i)}
                className={`cell-shape ${selected === i ? "selected-cell" : ""}`}
              >
                <polygon
                  points={polygon([
                    iso(x, y + 0.85, z),
                    iso(x + 0.84, y + 0.85, z),
                    iso(x + 0.84, y + 0.85, 4),
                    iso(x, y + 0.85, 4),
                  ])}
                  fill={color}
                  style={{ filter: "brightness(.67)" }}
                  stroke="#111723"
                  strokeWidth=".7"
                />
                <polygon
                  points={polygon([
                    iso(x + 0.84, y, z),
                    iso(x + 0.84, y + 0.85, z),
                    iso(x + 0.84, y + 0.85, 4),
                    iso(x + 0.84, y, 4),
                  ])}
                  fill={color}
                  style={{ filter: "brightness(.44)" }}
                  stroke="#111723"
                  strokeWidth=".7"
                />
                <polygon
                  points={polygon([
                    iso(x, y, z),
                    iso(x + 0.84, y, z),
                    iso(x + 0.84, y + 0.85, z),
                    iso(x, y + 0.85, z),
                  ])}
                  fill={color}
                  stroke={selected === i ? "#f1e9ff" : "#b7b3d333"}
                  strokeWidth={selected === i ? 1.8 : 0.7}
                />
                <polygon
                  points={polygon([
                    iso(x + 0.18, y + 0.19, z + 0.4),
                    iso(x + 0.66, y + 0.19, z + 0.4),
                    iso(x + 0.66, y + 0.62, z + 0.4),
                    iso(x + 0.18, y + 0.62, z + 0.4),
                  ])}
                  fill="#e1e5f5"
                  opacity=".12"
                />
                <line
                  x1={iso(x + 0.3, y + 0.35, z + 1)[0]}
                  y1={iso(x + 0.3, y + 0.35, z + 1)[1]}
                  x2={iso(x + 0.54, y + 0.35, z + 1)[0]}
                  y2={iso(x + 0.54, y + 0.35, z + 1)[1]}
                  stroke="#e9e8f0"
                  strokeWidth="2"
                  opacity=".6"
                />
              </g>
            );
          })}
          <path
            d="M130 188 L105 199 L105 211 L124 217"
            stroke="#6bb8c9"
            fill="none"
            strokeWidth="4"
            opacity=".7"
          />
          <path
            d="M151 196 L131 207 L131 220 L149 226"
            stroke="#6bb8c9"
            fill="none"
            strokeWidth="4"
            opacity=".7"
          />
          <path
            d="M106 199 L106 211"
            stroke="#a2e5e9"
            strokeWidth="2"
            opacity=".8"
          />
        </g>
      )}
      <g transform="translate(51 237)">
        <line x2="27" y2="12" stroke="#c77785" strokeWidth="1.4" />
        <line x2="-20" y2="13" stroke="#87b7a1" strokeWidth="1.4" />
        <line y2="-28" stroke="#8e9fd2" strokeWidth="1.4" />
        <text x="32" y="17" className="axis-label">
          x
        </text>
        <text x="-29" y="20" className="axis-label">
          y
        </text>
        <text x="-3" y="-35" className="axis-label">
          z
        </text>
        <circle r="2" fill="#c1c7d5" />
      </g>
      <text x="649" y="260" textAnchor="end" className="field-caption">
        {section ? "TOP SECTION" : "ISOMETRIC"} / 48 ZONES
      </text>
    </svg>
  );
}

export function AcousticField({
  running,
  mic,
  onMic,
}: {
  running: boolean;
  mic: number;
  onMic: (n: number) => void;
}) {
  return (
    <svg
      className="field-svg"
      viewBox="0 0 720 300"
      role="img"
      aria-label="Acoustic source and selectable microphone receivers"
    >
      <defs>
        <radialGradient id="sound-glow">
          <stop stopColor="#ac8de6" stopOpacity=".13" />
          <stop offset="1" stopColor="#ac8de6" stopOpacity="0" />
        </radialGradient>
      </defs>
      <ellipse cx="343" cy="145" rx="200" ry="135" fill="url(#sound-glow)" />
      {[1, 2, 3, 4, 5].map((v) => (
        <motion.ellipse
          key={v}
          cx="343"
          cy="145"
          rx={v * 43}
          ry={v * 24}
          stroke="#a98cdd"
          fill="none"
          strokeWidth=".8"
          opacity={0.45 - v * 0.065}
          animate={
            running
              ? { opacity: [0.1, 0.35, 0.1], scale: [0.95, 1.05, 0.95] }
              : { opacity: 0.45 - v * 0.065 }
          }
          transition={{ duration: 3, repeat: Infinity, delay: v * 0.3 }}
        />
      ))}
      <path
        d="M92 145H615 M343 25V266"
        stroke="#2b3545"
        strokeDasharray="4 6"
      />
      <g transform="translate(343 145)">
        <ellipse rx="71" ry="41" fill="#121d2b" stroke="#5a5976" />
        <g className={running ? "rotor-spinning" : ""}>
          {[0, 90, 180, 270].map((angle) => (
            <path
              key={angle}
              transform={`rotate(${angle})`}
              d="M-5 -7 C-10 -40 3 -68 17 -68 L25 -56 C9 -42 5 -18 7 -6Z"
              fill="#9a88c7"
              stroke="#c6b5ef"
              strokeWidth=".6"
            />
          ))}
        </g>
        <circle r="13" fill="#303447" stroke="#cab5f4" />
        <circle r="4" fill="#c3b0e5" />
      </g>
      {[0, 1, 2, 3].map((n) => {
        const angle = -0.8 + (n * Math.PI) / 2;
        const x = 343 + Math.cos(angle) * 230;
        const y = 145 + Math.sin(angle) * 104;
        return (
          <g key={n}>
            <line
              x1="343"
              y1="145"
              x2={x}
              y2={y}
              stroke={mic === n ? "#bda2f2" : "#333a4b"}
              strokeDasharray="3 7"
              pointerEvents="none"
            />
            <g
              className="microphone"
              role="button"
              tabIndex={0}
              aria-label={`Select microphone ${n + 1}`}
              onClick={() => onMic(n)}
              onKeyDown={(e) => e.key === "Enter" && onMic(n)}
            >
              <circle cx={x} cy={y} r="20" fill="transparent" />
              <circle
                cx={x}
                cy={y}
                r="14"
                fill={mic === n ? "#342b48" : "#19222e"}
                stroke={mic === n ? "#ba9aef" : "#697184"}
              />
              <path
                d={`M${x - 3} ${y - 5} a3 3 0 0 1 6 0 v5 a3 3 0 0 1 -6 0Z M${x - 6} ${y - 1} a6 6 0 0 0 12 0 M${x} ${y + 5}v4`}
                stroke="#ddd3ee"
                fill="none"
                strokeWidth="1.3"
              />
              <text x={x} y={y + 30} textAnchor="middle" className="axis-label">
                MIC 0{n + 1}
              </text>
            </g>
          </g>
        );
      })}
      <text x="343" y="234" textAnchor="middle" className="field-caption">
        FOUR-BLADE ROTOR / ANALYTIC PATCH SOURCE
      </text>
    </svg>
  );
}

export function MhdField({
  params,
  running,
}: {
  params: Parameters;
  running: boolean;
}) {
  const p = (x: number, y: number) =>
    `${120 + x * 455},${86 + y * 108 + x * 28}`;
  return (
    <svg
      className="field-svg"
      viewBox="0 0 720 300"
      role="img"
      aria-label="Hartmann channel velocity field and magnetic field direction"
    >
      <defs>
        <linearGradient id="channel-flow" x1="0" y1="0" x2="0" y2="1">
          {Array.from({ length: 81 }, (_, i) => {
            const y = -1 + i / 40;
            const u =
              (Math.cosh(params.hartmann) - Math.cosh(params.hartmann * y)) /
              (Math.cosh(params.hartmann) - 1);
            return (
              <stop
                key={i}
                offset={`${(i / 80) * 100}%`}
                stopColor={`rgb(${71 + Math.round(113 * u)},${93 + Math.round(118 * u)},${154 + Math.round(9 * u)})`}
              />
            );
          })}
        </linearGradient>
        <marker
          id="arrowhead"
          markerWidth="6"
          markerHeight="6"
          refX="5"
          refY="3"
          orient="auto"
        >
          <path d="M0 0L6 3L0 6" fill="none" stroke="#9bd4c1" />
        </marker>
      </defs>
      <polygon
        points={`${p(0, 0)} ${p(1, 0)} ${p(1, 1)} ${p(0, 1)}`}
        fill="url(#channel-flow)"
        opacity=".65"
      />
      {Array.from({ length: 17 }, (_, i) => (
        <line
          key={i}
          x1={120 + (i * 455) / 16}
          x2={120 + (i * 455) / 16}
          y1={86 + (i * 28) / 16}
          y2={194 + (i * 28) / 16}
          stroke="#131f32"
          opacity=".24"
        />
      ))}
      {Array.from({ length: 9 }, (_, i) => (
        <line
          key={i}
          x1="120"
          x2="575"
          y1={86 + i * 13.5}
          y2={114 + i * 13.5}
          stroke="#131f32"
          opacity=".24"
        />
      ))}
      {[0, 1].map((i) => (
        <polygon
          key={i}
          points={`${p(0, i)} ${p(1, i)} ${p(1.04, i - 0.09)} ${p(0.04, i - 0.09)}`}
          fill="#576577"
          stroke="#8590a0"
          strokeWidth=".5"
        />
      ))}
      {[0.2, 0.35, 0.5, 0.65, 0.8].map((v, i) => (
        <motion.path
          key={v}
          d={`M137 ${88 + v * 108} L550 ${114 + v * 108}`}
          stroke="#d4e8cf"
          strokeWidth="1.2"
          strokeDasharray="17 13"
          opacity=".6"
          animate={running ? { strokeDashoffset: [0, -60] } : {}}
          transition={{
            duration: 2 + Math.abs(i - 2),
            repeat: Infinity,
            ease: "linear",
          }}
          markerEnd="url(#arrowhead)"
        />
      ))}
      {[0, 1, 2, 3, 4].map((i) => (
        <g key={i}>
          <line
            x1={190 + i * 79}
            x2={190 + i * 79}
            y1="42"
            y2="75"
            stroke="#b19cdb"
            strokeWidth="1.3"
          />
          <path
            d={`M${186 + i * 79} 69 l4 7 4 -7`}
            fill="none"
            stroke="#b19cdb"
          />
          <line
            x1={190 + i * 79}
            x2={190 + i * 79}
            y1="230"
            y2="258"
            stroke="#b19cdb"
            strokeWidth="1.3"
          />
          <path
            d={`M${186 + i * 79} 252 l4 7 4 -7`}
            fill="none"
            stroke="#b19cdb"
          />
        </g>
      ))}
      <text x="603" y="63" className="axis-label">
        B0
      </text>
      <text x="78" y="149" className="axis-label">
        u
      </text>
      <text x="344" y="286" textAnchor="middle" className="field-caption">
        Ha = {params.hartmann} / LOW MAGNETIC REYNOLDS NUMBER
      </text>
    </svg>
  );
}

export function TpsField({
  frame,
  params,
}: {
  frame: TpsFrame;
  params: Parameters;
}) {
  return (
    <svg
      className="field-svg"
      viewBox="0 0 720 300"
      role="img"
      aria-label="Through-thickness thermal protection system temperature and char field"
    >
      <defs>
        <linearGradient id="heat-in" x1="0" x2="1">
          <stop stopColor="#e58d6b" stopOpacity="0" />
          <stop offset="1" stopColor="#e58d6b" stopOpacity=".3" />
        </linearGradient>
      </defs>
      <rect x="50" y="60" width="130" height="173" fill="url(#heat-in)" />
      {Array.from({ length: 8 }, (_, i) => (
        <g key={i}>
          <path
            d={`M${55 + (i % 3) * 10} ${72 + i * 21}H174 l-7 -4 m7 4 l-7 4`}
            stroke="#ed9977"
            strokeWidth="1.5"
            opacity={0.4 + (i % 3) * 0.15}
          />
        </g>
      ))}
      {frame.temperatures.map((T, i) => (
        <g key={i}>
          <rect
            x={185 + i * 9.5}
            y="60"
            width="10"
            height="173"
            fill={temperatureColor(T, 300, 1800)}
          />
          <line
            x1={185 + i * 9.5}
            x2={185 + i * 9.5}
            y1="60"
            y2="233"
            stroke="#161e2d"
            opacity=".13"
          />
          {frame.density[i] < 220 &&
            Array.from({ length: 6 }, (_, j) => (
              <circle
                key={j}
                cx={190 + i * 9.5}
                cy={78 + j * 27 + (i % 2) * 8}
                r="1"
                fill="#211827"
                opacity=".6"
              />
            ))}
        </g>
      ))}
      <path d="M185 60V233" stroke="#f7b08f" strokeWidth="3" />
      <path d="M574 60V233" stroke="#7c98c0" strokeWidth="4" />
      {[0, 1, 2, 3].map((i) => (
        <path
          key={i}
          d={`M202 ${90 + i * 36} C172 ${78 + i * 36} 187 ${58 + i * 36} 158 ${52 + i * 36} m0 0 l8 0 m-8 0 l4 7`}
          stroke="#d9c5b1"
          strokeWidth="1"
          fill="none"
          opacity=".6"
        />
      ))}
      <line x1="185" x2="574" y1="257" y2="257" stroke="#687589" />
      <path
        d="M190 253l-5 4 5 4 M569 253l5 4-5 4"
        stroke="#687589"
        fill="none"
      />
      <text x="380" y="277" textAnchor="middle" className="axis-label">
        {params.thickness} mm / 40 FINITE VOLUMES
      </text>
      <text x="128" y="37" textAnchor="middle" className="field-caption">
        HEAT FLUX
      </text>
      <text x="257" y="37" textAnchor="middle" className="field-caption">
        CHAR / PYROLYSIS
      </text>
      <text x="471" y="37" textAnchor="middle" className="field-caption">
        VIRGIN MATERIAL
      </text>
      <text x="612" y="151" className="axis-label">
        T inf
      </text>
    </svg>
  );
}
