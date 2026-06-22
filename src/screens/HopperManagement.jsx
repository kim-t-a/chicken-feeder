import React, { useState } from 'react';
import { useFeeder } from '../context/FeederContext';
import HopperGauge from '../components/HopperGauge';
import toast from 'react-hot-toast';
import { Droplets } from 'lucide-react';

export default function HopperManagement() {
  const { hopper } = useFeeder();
  const [showRefill, setShowRefill] = useState(false);
  const [refillGrams, setRefillGrams] = useState('');
  const [refillNotes, setRefillNotes] = useState('');
  const [refillLoading, setRefillLoading] = useState(false);
  const [editCapacity, setEditCapacity] = useState(false);
  const [newCapacity, setNewCapacity] = useState('');

  async function handleRefill() {
    const g = parseFloat(refillGrams);
    if (!g || g <= 0) { toast.error('Enter a valid amount'); return; }
    setRefillLoading(true);
    try {
      await hopper.refill(g, refillNotes);
      toast.success(`Hopper refilled with ${g}g!`);
      setShowRefill(false);
      setRefillGrams('');
      setRefillNotes('');
    } catch (e) {
      toast.error(e.message);
    } finally {
      setRefillLoading(false);
    }
  }

  function handleCapacitySave() {
    const c = parseFloat(newCapacity);
    if (!c || c < 100) { toast.error('Capacity must be at least 100g'); return; }
    hopper.setCapacity(c);
    toast.success('Capacity updated!');
    setEditCapacity(false);
  }

  return (
    <div>
      <div className="page-header">
        <h1 className="page-title">Hopper</h1>
        <p className="page-subtitle">Estimated feed remaining</p>
      </div>

      {/* Gauge card */}
      <div className="card" style={{ marginBottom: 12 }}>
        <div style={{ display: 'flex', gap: 24, alignItems: 'center' }}>
          <HopperGauge
            percent={hopper.percent}
            grams={hopper.currentGrams}
            capacity={hopper.capacity}
          />
          <div style={{ flex: 1 }}>
            <div style={{ marginBottom: 12 }}>
              <div className="card-label">Estimated Level</div>
              <div style={{ fontSize: '2rem', fontWeight: 800, letterSpacing: '-0.03em', color: hopper.percent <= 20 ? 'var(--c-danger)' : hopper.percent <= 40 ? 'var(--c-warning)' : 'var(--c-ok)' }}>
                {Math.round(hopper.currentGrams).toLocaleString()}g
              </div>
              <div style={{ fontSize: '0.82rem', color: 'var(--c-text-muted)' }}>
                of {hopper.capacity.toLocaleString()}g capacity
              </div>
            </div>
            <div className="progress-bar" style={{ height: 8, marginBottom: 8 }}>
              <div
                className="progress-fill"
                style={{
                  width: `${hopper.percent}%`,
                  background: hopper.percent <= 20 ? 'var(--c-danger)' : hopper.percent <= 40 ? 'var(--c-warning)' : 'var(--c-ok)',
                }}
              />
            </div>
            <div style={{ fontSize: '0.75rem', color: 'var(--c-text-muted)' }}>
              {Math.round(hopper.percent)}% full
            </div>
          </div>
        </div>

        {hopper.percent <= 20 && (
          <div className="info-box" style={{ marginTop: 14, borderLeft: '3px solid var(--c-danger)', background: 'rgba(239,68,68,0.08)' }}>
            <span className="info-box-icon">🪣</span>
            <span style={{ color: 'var(--c-danger)' }}>
              {hopper.percent <= 0 ? 'Hopper appears empty — please refill now!' : 'Hopper level is critically low. Refill soon to avoid interruptions.'}
            </span>
          </div>
        )}
      </div>

      {/* Note about estimation */}
      <div className="info-box" style={{ marginBottom: 12 }}>
        <span className="info-box-icon">ℹ️</span>
        <span>
          The hopper level is a <strong>software estimate</strong>. There is no hardware sensor. 
          It auto-decreases with each feeding event. Tap <strong>Record Refill</strong> whenever you top up.
        </span>
      </div>

      {/* Actions */}
      <div style={{ display: 'flex', gap: 10, marginBottom: 12 }}>
        <button className="btn btn-primary btn-full" onClick={() => setShowRefill(true)}>
          <Droplets size={16} /> Record Refill
        </button>
        <button className="btn btn-secondary" onClick={() => { setEditCapacity(true); setNewCapacity(hopper.capacity); }}>
          ⚙️ Capacity
        </button>
      </div>

      {/* Capacity edit inline */}
      {editCapacity && (
        <div className="card" style={{ marginBottom: 12 }}>
          <div className="card-label">Hopper Capacity</div>
          <div style={{ display: 'flex', gap: 8, marginTop: 8 }}>
            <input
              className="form-input"
              type="number"
              min={100}
              value={newCapacity}
              onChange={e => setNewCapacity(e.target.value)}
              placeholder="Grams"
            />
            <button className="btn btn-primary" onClick={handleCapacitySave}>Save</button>
            <button className="btn btn-secondary" onClick={() => setEditCapacity(false)}>Cancel</button>
          </div>
          <p className="form-hint">Enter the maximum capacity of your hopper in grams (default: 2000g).</p>
        </div>
      )}

      {/* Refill modal */}
      {showRefill && (
        <div className="modal-overlay" onClick={() => setShowRefill(false)}>
          <div className="modal-sheet" onClick={e => e.stopPropagation()}>
            <div className="modal-handle" />
            <div className="modal-title">🪣 Record Refill</div>
            <div className="modal-body">How many grams did you add to the hopper?</div>
            <div className="form-group">
              <label className="form-label">Amount added (grams)</label>
              <input
                className="form-input"
                type="number"
                min={1}
                max={10000}
                placeholder="e.g. 1500"
                value={refillGrams}
                onChange={e => setRefillGrams(e.target.value)}
                autoFocus
              />
            </div>
            <div className="form-group">
              <label className="form-label">Notes (optional)</label>
              <input
                className="form-input"
                type="text"
                placeholder="e.g. Full bag added"
                value={refillNotes}
                onChange={e => setRefillNotes(e.target.value)}
              />
            </div>
            <div className="modal-actions">
              <button className="btn btn-secondary" onClick={() => setShowRefill(false)}>Cancel</button>
              <button className="btn btn-primary" onClick={handleRefill} disabled={refillLoading}>
                {refillLoading ? <span className="spinner" /> : '✅ Confirm Refill'}
              </button>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}
