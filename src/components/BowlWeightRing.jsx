import React, { useState } from 'react';

/**
 * Animated ring showing the weight of feed currently in the bowl.
 *
 * WHAT THIS SHOWS:
 *   The number = grams of feed sitting in the physical bowl right now,
 *   as measured by the HX711 load cell under the bowl after the last
 *   dispense event. It is NOT a live sensor stream — it updates each
 *   time the ESP32 logs a feeding event to Supabase.
 *
 *   Ring colour:
 *     Green  = bowl has plenty of feed (>50% of max)
 *     Yellow = bowl is getting low  (20–50%)
 *     Red    = bowl is nearly empty (<20%)
 *
 * @param {number} weight - grams of feed in bowl (from last feeding_log)
 * @param {number} max    - max display scale in grams (default 2000)
 * @param {number} size   - SVG size in px
 */
export default function BowlWeightRing({ weight = 0, max = 2000, size = 180 }) {
  const [showTip, setShowTip] = useState(false);
  const pct    = Math.min(1, Math.max(0, weight / max));
  const r      = (size - 20) / 2;
  const circ   = 2 * Math.PI * r;
  const offset = circ * (1 - pct);
  const cx     = size / 2;

  const color =
    pct > 0.5 ? 'var(--c-ok)' :
    pct > 0.2 ? 'var(--c-warning)' :
    'var(--c-danger)';

  const statusLabel =
    pct > 0.5 ? 'Plenty' :
    pct > 0.2 ? 'Getting low' :
    weight === 0 ? 'Empty / no data' : 'Very low';

  return (
    <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', gap: 6, position: 'relative' }}>
      {/* Clickable ring */}
      <div
        role="button"
        tabIndex={0}
        aria-label="Feed in bowl gauge — tap for info"
        onClick={() => setShowTip(v => !v)}
        onKeyDown={e => e.key === 'Enter' && setShowTip(v => !v)}
        style={{ cursor: 'pointer', borderRadius: '50%' }}
      >
        <svg width={size} height={size} style={{ transform: 'rotate(-90deg)', display: 'block' }}>
          {/* Track */}
          <circle cx={cx} cy={cx} r={r} fill="none" stroke="var(--c-surface-3)" strokeWidth={10} />
          {/* Fill */}
          <circle
            cx={cx} cy={cx} r={r}
            fill="none"
            stroke={color}
            strokeWidth={10}
            strokeLinecap="round"
            strokeDasharray={circ}
            strokeDashoffset={offset}
            style={{ transition: 'stroke-dashoffset 0.8s cubic-bezier(0.4,0,0.2,1), stroke 0.4s' }}
          />
          {/* Inner text (un-rotate) */}
          <g transform={`rotate(90, ${cx}, ${cx})`}>
            <text
              x={cx} y={cx - 12}
              textAnchor="middle"
              fill="var(--c-text)"
              fontSize={size * 0.18}
              fontWeight="800"
              fontFamily="Inter, sans-serif"
              style={{ letterSpacing: '-0.03em' }}
            >
              {weight != null ? Math.round(weight) : '--'}
            </text>
            <text
              x={cx} y={cx + 10}
              textAnchor="middle"
              fill="var(--c-text-muted)"
              fontSize={size * 0.085}
              fontWeight="500"
              fontFamily="Inter, sans-serif"
            >
              g in bowl
            </text>
            <text
              x={cx} y={cx + 26}
              textAnchor="middle"
              fill={color}
              fontSize={size * 0.072}
              fontWeight="600"
              fontFamily="Inter, sans-serif"
            >
              {statusLabel}
            </text>
          </g>
        </svg>
      </div>

      {/* Label */}
      <span style={{ fontSize: '0.75rem', color: 'var(--c-text-muted)', fontWeight: 600 }}>
        Feed in Bowl
      </span>

      {/* Info tooltip — shown on tap */}
      {showTip && (
        <div
          onClick={() => setShowTip(false)}
          style={{
            position: 'absolute',
            bottom: 'calc(100% + 8px)',
            left: '50%',
            transform: 'translateX(-50%)',
            background: 'var(--c-surface-2)',
            border: '1px solid var(--c-border)',
            borderRadius: 10,
            padding: '10px 14px',
            width: 230,
            fontSize: '0.78rem',
            lineHeight: 1.55,
            color: 'var(--c-text-muted)',
            boxShadow: '0 4px 20px rgba(0,0,0,0.4)',
            zIndex: 10,
          }}
        >
          <div style={{ fontWeight: 700, color: 'var(--c-text)', marginBottom: 4 }}>
            🥣 What is "Feed in Bowl"?
          </div>
          This shows how many grams of feed are currently in the physical bowl, measured by the load cell (weight sensor) under the bowl.
          <br /><br />
          It updates each time the device dispenses feed. Tap anywhere to close.
        </div>
      )}
    </div>
  );
}
