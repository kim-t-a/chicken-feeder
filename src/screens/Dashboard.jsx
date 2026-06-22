import React, { useState } from 'react';
import { useFeeder } from '../context/FeederContext';
import AlertBanner from '../components/AlertBanner';
import BowlWeightRing from '../components/BowlWeightRing';
import HopperGauge from '../components/HopperGauge';
import toast from 'react-hot-toast';

function formatAgo(seconds) {
  if (seconds < 60) return `${Math.round(seconds)}s ago`;
  if (seconds < 3600) return `${Math.round(seconds / 60)}m ago`;
  return `${Math.round(seconds / 3600)}h ago`;
}

export default function Dashboard() {
  const { config, latestLog, hopper, deviceStatus, feedNow, tareScale, toggleMotor } = useFeeder();
  const [feedLoading, setFeedLoading] = useState(false);
  const [tareLoading, setTareLoading]   = useState(false);
  const [motorLoading, setMotorLoading] = useState(false);

  // ── Bowl weight: 3-tier priority ──────────────────────────────────────
  // Tier 1: Live value from ESP32 via feeder_config.current_bowl_weight
  //         (requires "Run SQL setup" to add the column)
  // Tier 2: Weight from the most recent feeding log current_weight
  // Tier 3: null → ring shows '--'
  const liveBowlWeight = config?.current_bowl_weight;       // undefined if no column
  const logBowlWeight  = latestLog?.current_weight ?? null;  // null when no feeding logs
  const hasLiveColumn  = liveBowlWeight !== undefined && liveBowlWeight !== null;
  const deviceOnline   = deviceStatus?.online ?? false;

  // Use current_bowl_weight whenever the column exists (online OR offline).
  // This is always the last value pushed by the sensor — more accurate than
  // the feeding log which only updates after a dispense event (stale 5g etc).
  // Fall back to feeding log only when the column doesn't exist at all.
  const useLive       = hasLiveColumn;
  const bowlWeight    = useLive ? liveBowlWeight : logBowlWeight;
  const weightIsLive  = hasLiveColumn && deviceOnline; // green LIVE badge only when online
  const hasWeightData = bowlWeight !== null;
  const needsSqlSetup = !hasLiveColumn && deviceOnline;


  // ── System status badge ───────────────────────────────────────────────
  const systemStatus =
    config?.jam_detected   ? { label: 'JAMMED',   cls: 'badge-danger' } :
    !config?.motor_enabled ? { label: 'Disabled', cls: 'badge-warn'   } :
                             { label: 'Active',   cls: 'badge-ok'     };

  async function handleFeedNow() {
    setFeedLoading(true);
    try { await feedNow(); } finally { setFeedLoading(false); }
  }
  async function handleTare() {
    setTareLoading(true);
    try { await tareScale(); } finally { setTareLoading(false); }
  }
  async function handleMotorToggle() {
    setMotorLoading(true);
    try { await toggleMotor(); } finally { setMotorLoading(false); }
  }

  return (
    <div>
      {/* ── Header ── */}
      <div style={{ display: 'flex', alignItems: 'flex-start', justifyContent: 'space-between', marginBottom: 16 }}>
        <div>
          <h1 className="page-title">Dashboard</h1>
          <p className="page-subtitle">Live feeder overview</p>
        </div>
        <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'flex-end', gap: 6 }}>
          <div className={`online-pill ${deviceStatus.online ? 'online' : 'offline'}`}>
            <span className={`online-dot ${deviceStatus.online ? 'online' : 'offline'}`} />
            {deviceStatus.online
              ? 'Device Online'
              : `Offline · ${deviceStatus.lastSeenAgo != null ? formatAgo(deviceStatus.lastSeenAgo) : 'never'}`}
          </div>
          <span className={`badge ${systemStatus.cls}`}>{systemStatus.label}</span>
        </div>
      </div>

      <AlertBanner />

      {/* ── Bowl weight + Hopper ── */}
      <div className="card" style={{ marginBottom: 12 }}>
        <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-around', flexWrap: 'wrap', gap: 16, padding: '8px 0' }}>

          {/* Bowl weight column */}
          <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', gap: 4 }}>
            {/* Status badge: LIVE / LAST LOG / NO DATA */}
            <span style={{
              fontSize: '0.65rem', fontWeight: 700, letterSpacing: '0.08em',
              padding: '2px 8px', borderRadius: 99,
              background: weightIsLive  ? 'rgba(34,197,94,0.15)'
                        : hasWeightData ? 'rgba(255,255,255,0.06)'
                        : 'rgba(239,68,68,0.12)',
              color: weightIsLive  ? 'var(--c-ok)'
                   : hasWeightData ? 'var(--c-text-dim)'
                   : 'var(--c-danger)',
              border: `1px solid ${weightIsLive ? 'var(--c-ok)' : 'transparent'}`,
              display: 'flex', alignItems: 'center', gap: 4,
            }}>
              {weightIsLive && (
                <span style={{
                  width: 6, height: 6, borderRadius: '50%',
                  background: 'var(--c-ok)',
                  animation: 'pulse 1.5s infinite',
                  display: 'inline-block',
                }} />
              )}
              {weightIsLive ? 'LIVE' : hasWeightData ? 'LAST LOG' : 'NO DATA'}
            </span>

            {/* Ring — pass null when no data so it shows '--' */}
            <BowlWeightRing weight={hasWeightData ? bowlWeight : null} max={2000} size={172} />

            {/* Hint shown only when device is online but SQL column is missing */}
            {needsSqlSetup && (
              <span style={{ fontSize: '0.6rem', color: 'var(--c-warning)', textAlign: 'center', maxWidth: 150 }}>
                ⚠ Run SQL setup to enable live weight
              </span>
            )}
          </div>

          {/* Hopper column */}
          <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', gap: 8 }}>
            <HopperGauge
              percent={hopper.percent}
              grams={hopper.currentGrams}
              capacity={hopper.capacity}
            />
          </div>
        </div>

        {/* Mode + last feed row */}
        <div style={{ borderTop: '1px solid var(--c-border)', marginTop: 16, paddingTop: 14, display: 'flex', gap: 8, flexWrap: 'wrap', justifyContent: 'space-between', alignItems: 'center' }}>
          <div style={{ display: 'flex', gap: 8 }}>
            <span className="badge badge-primary">
              {config?.mode === 'ad-libitum' ? '♻️ Ad-libitum' : '🕐 Scheduled'}
            </span>
          </div>
          {latestLog && (
            <span style={{ fontSize: '0.75rem', color: 'var(--c-text-muted)' }}>
              Last feed: {new Date(latestLog.logged_at).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' })}
              {' · '}{latestLog.amount_dispensed?.toFixed(0)}g
            </span>
          )}
        </div>
      </div>

      {/* ── Quick Actions ── */}
      <div className="section">
        <div className="section-title">Quick Actions</div>
        <div className="action-grid">
          <button
            className="action-btn primary"
            onClick={handleFeedNow}
            disabled={feedLoading || !deviceStatus.recentEnough || !config?.motor_enabled || config?.jam_detected}
          >
            {feedLoading ? <span className="spinner" style={{ fontSize: '1.4rem' }} /> : <span className="action-btn-icon">⚡</span>}
            <span>Feed Now</span>
          </button>

          <button className="action-btn" onClick={handleTare} disabled={tareLoading}>
            {tareLoading ? <span className="spinner" style={{ fontSize: '1.4rem' }} /> : <span className="action-btn-icon">⚖️</span>}
            <span>Tare Scale</span>
          </button>

          <button
            className={`action-btn ${!config?.motor_enabled ? '' : 'danger'}`}
            onClick={handleMotorToggle}
            disabled={motorLoading || config?.jam_detected}
          >
            {motorLoading
              ? <span className="spinner" style={{ fontSize: '1.4rem' }} />
              : <span className="action-btn-icon">{config?.motor_enabled ? '🔴' : '🟢'}</span>}
            <span>{config?.motor_enabled ? 'Disable Motor' : 'Enable Motor'}</span>
          </button>
        </div>
      </div>

      {/* Offline warning for Feed Now */}
      {!deviceStatus.recentEnough && (
        <div className="info-box" style={{ marginTop: -8, marginBottom: 12 }}>
          <span className="info-box-icon">📡</span>
          <span>Device is offline — "Feed Now" is disabled until the device reconnects (last seen {deviceStatus.lastSeenAgo != null ? formatAgo(deviceStatus.lastSeenAgo) : 'never'}).</span>
        </div>
      )}

      {/* ── Flock summary ── */}
      <div className="card">
        <div className="card-label">Flock</div>
        <div style={{ display: 'flex', gap: 16, flexWrap: 'wrap', marginTop: 4 }}>
          <div><span style={{ fontSize: '1.1rem', fontWeight: 700 }}>{config?.chicken_count ?? '—'}</span> <span style={{ fontSize: '0.8rem', color: 'var(--c-text-muted)' }}>birds</span></div>
          <div><span style={{ fontSize: '1.1rem', fontWeight: 700, textTransform: 'capitalize' }}>{config?.breed ?? '—'}</span></div>
          <div><span style={{ fontSize: '1.1rem', fontWeight: 700 }}>Week {config?.age_weeks ?? '—'}</span></div>
        </div>
      </div>
    </div>
  );
}
