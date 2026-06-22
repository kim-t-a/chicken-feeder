import React from 'react';

/**
 * Vertical hopper fill gauge.
 * @param {number} percent  0-100
 * @param {number} grams    current grams
 * @param {number} capacity max grams
 */
export default function HopperGauge({ percent = 0, grams = 0, capacity = 2000 }) {
  const color =
    percent <= 0  ? 'var(--c-danger)' :
    percent <= 20 ? 'var(--c-danger)' :
    percent <= 40 ? 'var(--c-warning)' :
    'var(--c-ok)';

  const label =
    percent <= 0  ? 'Empty' :
    percent <= 20 ? 'Critical' :
    percent <= 40 ? 'Low' :
    percent <= 70 ? 'Moderate' :
    'Good';

  return (
    <div className="hopper-gauge-wrap">
      <div className="hopper-gauge-bar">
        <div
          className="hopper-gauge-fill"
          style={{
            height: `${Math.max(0, percent)}%`,
            background: `linear-gradient(to top, ${color}, ${color}aa)`,
          }}
        />
        {/* Tick marks at 25%, 50%, 75% */}
        {[25, 50, 75].map(tick => (
          <div key={tick} style={{
            position: 'absolute',
            bottom: `${tick}%`,
            left: 0, right: 0,
            height: 1,
            background: 'rgba(255,255,255,0.1)',
          }} />
        ))}
      </div>
      <div className="hopper-pct" style={{ color }}>{Math.round(percent)}%</div>
      <div className="hopper-grams">{Math.round(grams).toLocaleString()} / {capacity.toLocaleString()} g</div>
      <div style={{ marginTop: 4 }}>
        <span className={`badge ${percent <= 20 ? 'badge-danger' : percent <= 40 ? 'badge-warn' : 'badge-ok'}`}>
          {label}
        </span>
      </div>
    </div>
  );
}
