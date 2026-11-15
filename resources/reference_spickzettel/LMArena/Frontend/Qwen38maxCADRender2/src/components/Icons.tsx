import type { SVGProps } from "react";

type P = SVGProps<SVGSVGElement>;

const base = (p: P) => ({
  viewBox: "0 0 24 24",
  fill: "none",
  stroke: "currentColor",
  strokeWidth: 1.6,
  strokeLinecap: "round" as const,
  strokeLinejoin: "round" as const,
  width: 16,
  height: 16,
  ...p,
});

export const IconCube = (p: P) => (
  <svg {...base(p)}>
    <path d="M12 2.6 21 7.3v9.4L12 21.4 3 16.7V7.3z" />
    <path d="M3 7.3 12 12l9-4.7M12 12v9.4" />
  </svg>
);

export const IconWire = (p: P) => (
  <svg {...base(p)}>
    <path d="M4 8.5 12 4l8 4.5v7L12 20l-8-4.5z" />
    <path d="M4 8.5 12 13l8-4.5M12 13v7M8 6.2 16 17.8M16 6.2 8 17.8" />
  </svg>
);

export const IconXray = (p: P) => (
  <svg {...base(p)}>
    <path d="M2.5 12S6 5.8 12 5.8 21.5 12 21.5 12 18 18.2 12 18.2 2.5 12 2.5 12z" />
    <circle cx="12" cy="12" r="2.6" />
  </svg>
);

export const IconEdge = (p: P) => (
  <svg {...base(p)}>
    <path d="M3 17.5 9.5 4l5 8.5L21 8" />
    <circle cx="9.5" cy="4" r="1.5" />
    <circle cx="14.5" cy="12.5" r="1.5" />
  </svg>
);

export const IconGrid = (p: P) => (
  <svg {...base(p)}>
    <path d="M3 9h18M3 15h18M9 3v18M15 3v18" />
    <rect x="3" y="3" width="18" height="18" rx="1.5" />
  </svg>
);

export const IconAxes = (p: P) => (
  <svg {...base(p)}>
    <path d="M4 20V4M4 20h16" />
    <path d="m4 20 8-8" />
    <path d="M4 7.5 2.5 6M4 7.5 5.5 6M20 20l-1.5-1.5M20 20l-1.5 1.5M11 11.5 9 12M11 11.5l.5-2" />
  </svg>
);

export const IconSection = (p: P) => (
  <svg {...base(p)}>
    <path d="M3 15.5 12 20l9-4.5" />
    <path d="M12 3v17" strokeDasharray="2 2.5" />
    <path d="M6.5 9.5 12 12l5.5-2.5L12 4z" />
  </svg>
);

export const IconSpin = (p: P) => (
  <svg {...base(p)}>
    <path d="M20.5 12a8.5 8.5 0 1 1-2.9-6.4" />
    <path d="M20.8 4v4.6h-4.6" />
  </svg>
);

export const IconFit = (p: P) => (
  <svg {...base(p)}>
    <path d="M4 9V4h5M20 9V4h-5M4 15v5h5M20 15v5h-5" />
    <circle cx="12" cy="12" r="2.4" />
  </svg>
);

export const IconPlug = (p: P) => (
  <svg {...base(p)}>
    <path d="M9 3v5M15 3v5" />
    <path d="M6.5 8h11v3.2a5.5 5.5 0 0 1-5.5 5.5 5.5 5.5 0 0 1-5.5-5.5z" />
    <path d="M12 16.7V21" />
  </svg>
);

export const IconTree = (p: P) => (
  <svg {...base(p)}>
    <circle cx="6" cy="5.5" r="2.2" />
    <circle cx="18" cy="12" r="2.2" />
    <circle cx="6" cy="18.5" r="2.2" />
    <path d="M8.2 5.5h4.3a2 2 0 0 1 2 2v3.4M8.2 18.5h4.3a2 2 0 0 0 2-2v-3.4" />
  </svg>
);

export const IconTerminal = (p: P) => (
  <svg {...base(p)}>
    <rect x="3" y="4.5" width="18" height="15" rx="2" />
    <path d="m7.5 10 2.6 2.4-2.6 2.4M13 15h4" />
  </svg>
);

export const IconChip = (p: P) => (
  <svg {...base(p)}>
    <rect x="7" y="7" width="10" height="10" rx="1.5" />
    <path d="M10 3v4M14 3v4M10 17v4M14 17v4M3 10h4M3 14h4M17 10h4M17 14h4" />
  </svg>
);

export const IconBolt = (p: P) => (
  <svg {...base(p)}>
    <path d="M13.5 2.5 5 13.4h5.4L9.8 21.5 19 10.4h-5.6z" />
  </svg>
);

export const IconCheck = (p: P) => (
  <svg {...base(p)}>
    <path d="m4.5 12.5 5 5 10-11" />
  </svg>
);

export const IconArrow = (p: P) => (
  <svg {...base(p)}>
    <path d="M4 12h15M13.5 6.5 20 12l-6.5 5.5" />
  </svg>
);

export const IconLayers = (p: P) => (
  <svg {...base(p)}>
    <path d="m12 3 9 4.8-9 4.8-9-4.8z" />
    <path d="m3.5 12.4 8.5 4.5 8.5-4.5M3.5 16.9 12 21.4l8.5-4.5" />
  </svg>
);

export const IconRuler = (p: P) => (
  <svg {...base(p)}>
    <rect x="2.5" y="8" width="19" height="8" rx="1.4" />
    <path d="M6.5 8v3M10 8v4.5M13.5 8v3M17 8v4.5" />
  </svg>
);

export const IconFlask = (p: P) => (
  <svg {...base(p)}>
    <path d="M9.5 3v6.2L4.6 17.6A2 2 0 0 0 6.3 20.6h11.4a2 2 0 0 0 1.7-3l-4.9-8.4V3" />
    <path d="M8.4 3h7.2M7.3 14.6h9.4" />
  </svg>
);
