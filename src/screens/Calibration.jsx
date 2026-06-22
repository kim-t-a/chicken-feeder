import React, { useState } from 'react';
import { useFeeder } from '../context/FeederContext';
import toast from 'react-hot-toast';
import { RotateCcw, SlidersHorizontal } from 'lucide-react';

export default function Calibration() {
  const { config, updateConfig } = useFeeder();
  const [knownWeight, setKnownWeight] = useState('');
  const [taring, setTaring] = useState(false);
  const [calibrating, setCalibrating] = useState(false);

  async function handleTare() {
    setTaring(true);
    try {
      await updateConfig({ tare_trigger: true });
      toast.success('Tare command sent! Waiting for device to zero the scale…');
    } catch (e) {
      toast.error(e.message);
    } finally {
      setTaring(false);
    }
  }

  async function handleCalibrate() {
    const w = parseFloat(knownWeight);
    if (!w || w <= 0) { toast.error('Enter a valid known weight in grams'); return; }
    setCalibrating(true);
    try {
      await updateConfig({ known_weight_grams: w, calibration_trigger: true });
      toast.success('Calibration command sent! Device will update the scale factor…');
    } catch (e) {
      toast.error(e.message);
    } finally {
      setCalibrating(false);
    }
  }

  const isTarePending = config?.tare_trigger;
  const isCalibPending = config?.calibration_trigger;

  return (
    <div>
      <div className="page-header">
        <h1 className="page-title">Calibration</h1>
        <p className="page-subtitle">Remote scale zeroing and calibration</p>
      </div>

      {/* Tare */}
      <div className="card" style={{ marginBottom: 12 }}>
        <div style={{ display: 'flex', alignItems: 'flex-start', gap: 12 }}>
          <span style={{ fontSize: '2rem' }}>⚖️</span>
          <div style={{ flex: 1 }}>
            <div style={{ fontWeight: 700, fontSize: '1rem', marginBottom: 4 }}>Remote Tare (Zero Scale)</div>
            <div className="info-box" style={{ marginBottom: 12 }}>
              <span className="info-box-icon">💡</span>
              <span>Ensure the bowl is <strong>completely empty</strong> before taring. This sets the empty weight as zero.</span>
            </div>
            <button className="btn btn-secondary btn-full" onClick={handleTare} disabled={taring || isTarePending}>
              {taring ? <><span className="spinner" /> Sending…</> :
               isTarePending ? '⏳ Waiting for device…' :
               <><RotateCcw size={15} /> Tare Scale</>}
            </button>
            {isTarePending && (
              <p className="form-hint" style={{ marginTop: 6, textAlign: 'center' }}>
                Command received by app — device will execute within ~5 seconds
              </p>
            )}
          </div>
        </div>
      </div>

      {/* Calibration */}
      <div className="card" style={{ marginBottom: 12 }}>
        <div style={{ display: 'flex', alignItems: 'flex-start', gap: 12 }}>
          <span style={{ fontSize: '2rem' }}>🔧</span>
          <div style={{ flex: 1 }}>
            <div style={{ fontWeight: 700, fontSize: '1rem', marginBottom: 4 }}>Remote Calibration</div>
            <div className="info-box" style={{ marginBottom: 12 }}>
              <span className="info-box-icon">💡</span>
              <span>Place your known-weight object on the scale platform, enter its exact weight below, then tap <strong>Calibrate</strong>.</span>
            </div>
            <div className="form-group">
              <label className="form-label">Known weight (grams)</label>
              <input
                className="form-input"
                type="number"
                min={1}
                step={0.1}
                placeholder="e.g. 200"
                value={knownWeight}
                onChange={e => setKnownWeight(e.target.value)}
              />
            </div>
            <button className="btn btn-primary btn-full" onClick={handleCalibrate} disabled={calibrating || isCalibPending || !knownWeight}>
              {calibrating ? <><span className="spinner" /> Sending…</> :
               isCalibPending ? '⏳ Waiting for device…' :
               <><SlidersHorizontal size={15} /> Calibrate Scale</>}
            </button>
            {isCalibPending && (
              <p className="form-hint" style={{ marginTop: 6, textAlign: 'center' }}>
                Device is calibrating. New factor will appear below when done.
              </p>
            )}
          </div>
        </div>
      </div>

      {/* Current calibration factor */}
      <div className="card" style={{ marginBottom: 12 }}>
        <div className="card-label">Current Calibration Factor</div>
        <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', marginTop: 8 }}>
          <span style={{ fontSize: '1.6rem', fontWeight: 800, letterSpacing: '-0.02em', color: 'var(--c-primary)' }}>
            {config?.calibration_factor != null ? config.calibration_factor.toFixed(4) : '—'}
          </span>
          <span className="badge badge-muted">Read-only · Set by device</span>
        </div>
        <p className="form-hint" style={{ marginTop: 8 }}>
          This value is automatically written by the ESP32 after each calibration. A higher factor means the scale reads heavier per raw unit.
        </p>
      </div>

      {/* Last sync */}
      {config?.updated_at && (
        <div className="info-box">
          <span className="info-box-icon">🕐</span>
          <span>Last device sync: <strong>{new Date(config.updated_at).toLocaleString()}</strong></span>
        </div>
      )}
    </div>
  );
}
