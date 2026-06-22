import React, { useState } from 'react';
import { useFeeder } from '../context/FeederContext';
import ConfirmModal from '../components/ConfirmModal';
import toast from 'react-hot-toast';

function Row({ label, value, valueColor }) {
  return (
    <div className="status-row">
      <span className="status-key">{label}</span>
      <span className="status-val" style={{ color: valueColor }}>{value}</span>
    </div>
  );
}

function formatAgo(isoStr) {
  if (!isoStr) return 'Never';
  const diff = (Date.now() - new Date(isoStr).getTime()) / 1000;
  if (diff < 60) return `${Math.round(diff)}s ago`;
  if (diff < 3600) return `${Math.round(diff / 60)}m ago`;
  if (diff < 86400) return `${Math.round(diff / 3600)}h ago`;
  return new Date(isoStr).toLocaleDateString();
}

export default function DeviceStatus() {
  const { config, hopper, deviceStatus } = useFeeder();
  const [confirmReset, setConfirmReset] = useState(false);

  function handleFactoryReset() {
    hopper.resetHopper();
    localStorage.removeItem('poultrypal_hopper');
    toast.success('App-side data cleared. Device is unaffected.');
    setConfirmReset(false);
  }

  if (!config) return <div className="spinner-center"><span className="spinner spinner-lg" /></div>;

  return (
    <div>
      <div className="page-header">
        <h1 className="page-title">Device Status</h1>
        <p className="page-subtitle">ESP32-S3 diagnostics</p>
      </div>

      {/* Online status */}
      <div className="card" style={{ marginBottom: 12 }}>
        <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between' }}>
          <div className="card-label" style={{ marginBottom: 0 }}>Connectivity</div>
          <div className={`online-pill ${deviceStatus.online ? 'online' : 'offline'}`}>
            <span className={`online-dot ${deviceStatus.online ? 'online' : 'offline'}`} />
            {deviceStatus.online ? 'Online' : 'Offline'}
          </div>
        </div>
        <div className="status-list" style={{ marginTop: 12 }}>
          <Row label="Last seen" value={formatAgo(config.updated_at)} />
          <Row
            label="Device clock (stored)"
            value={config.updated_at
              ? new Date(config.updated_at).toLocaleString([], { hour12: false })
              : '—'}
          />
          <Row
            label="Browser local time"
            value={new Date().toLocaleString([], { hour12: false })}
          />
          <Row
            label="Clock drift"
            value={deviceStatus.rawDiff != null
              ? `${Math.round(deviceStatus.rawDiff)}s (${deviceStatus.rawDiff < -60 ? '⚠️ ESP32 clock ahead — timezone issue' : deviceStatus.rawDiff > 60 ? '⚠️ ESP32 clock behind' : '✓ OK'})`
              : '—'}
            valueColor={deviceStatus.rawDiff != null && Math.abs(deviceStatus.rawDiff) > 60
              ? 'var(--c-warning)' : 'var(--c-ok)'}
          />
        </div>
      </div>

      {/* Motor & alerts */}
      <div className="card" style={{ marginBottom: 12 }}>
        <div className="card-label">Motor & Alerts</div>
        <div className="status-list">
          <Row
            label="Motor status"
            value={config.motor_enabled ? 'Enabled' : 'Disabled'}
            valueColor={config.motor_enabled ? 'var(--c-ok)' : 'var(--c-danger)'}
          />
          <Row
            label="Jam detected"
            value={config.jam_detected ? 'YES — inspect hardware' : 'No'}
            valueColor={config.jam_detected ? 'var(--c-danger)' : 'var(--c-ok)'}
          />
          <Row
            label="Alert cause"
            value={config.alert_cause || 'None'}
            valueColor={config.alert_cause ? 'var(--c-warning)' : 'var(--c-text-muted)'}
          />
        </div>
      </div>

      {/* Feed config */}
      <div className="card" style={{ marginBottom: 12 }}>
        <div className="card-label">Feed Configuration</div>
        <div className="status-list">
          <Row label="Mode" value={config.mode === 'ad-libitum' ? 'Ad-libitum' : 'Scheduled'} />
          <Row label="Slot count" value={config.slot_count} />
          <Row label="Target dispense" value={config.target_dispense_grams === 0 ? 'Auto' : `${config.target_dispense_grams}g`} />
          <Row label="Ad-lib threshold" value={`${config.adlib_threshold_grams}g`} />
        </div>
      </div>

      {/* Scale */}
      <div className="card" style={{ marginBottom: 12 }}>
        <div className="card-label">Scale / HX711</div>
        <div className="status-list">
          <Row label="Calibration factor" value={config.calibration_factor?.toFixed(4) ?? '—'} />
          <Row label="Tare pending" value={config.tare_trigger ? 'Yes' : 'No'} valueColor={config.tare_trigger ? 'var(--c-warning)' : undefined} />
          <Row label="Calibration pending" value={config.calibration_trigger ? 'Yes' : 'No'} valueColor={config.calibration_trigger ? 'var(--c-warning)' : undefined} />
        </div>
      </div>

      {/* Flock */}
      <div className="card" style={{ marginBottom: 20 }}>
        <div className="card-label">Flock</div>
        <div className="status-list">
          <Row label="Breed" value={config.breed} />
          <Row label="Bird count" value={config.chicken_count} />
          <Row label="Age" value={`Week ${config.age_weeks}`} />
        </div>
      </div>

      {/* Factory reset */}
      <div className="card" style={{ borderColor: 'rgba(239,68,68,0.25)', marginBottom: 12 }}>
        <div className="card-label" style={{ color: 'var(--c-danger)' }}>Danger Zone</div>
        <p style={{ fontSize: '0.82rem', color: 'var(--c-text-muted)', marginTop: 6, marginBottom: 12 }}>
          <strong>Factory Reset</strong> clears the app-side hopper estimate and local cache only.
          <em> It does NOT affect the physical device or Supabase data.</em>
        </p>
        <button className="btn btn-danger btn-full" onClick={() => setConfirmReset(true)}>
          🗑️ Factory Reset (App Only)
        </button>
      </div>

      {confirmReset && (
        <ConfirmModal
          title="Reset App Data?"
          body="This will clear the hopper estimate and all locally cached settings. Your Supabase data and the physical device are not affected."
          confirmLabel="Reset App Data"
          confirmClass="btn-danger"
          onConfirm={handleFactoryReset}
          onCancel={() => setConfirmReset(false)}
        />
      )}
    </div>
  );
}
