import React, { useState, useEffect } from 'react';
import { useFeeder } from '../context/FeederContext';
import toast from 'react-hot-toast';
import { Plus, Trash2 } from 'lucide-react';

const FEED_KEYS = ['feed_time_0','feed_time_1','feed_time_2','feed_time_3','feed_time_4','feed_time_5','feed_time_6','feed_time_7'];

export default function FeedMode() {
  const { config, updateConfig } = useFeeder();
  const [saving, setSaving] = useState(false);
  const [local, setLocal] = useState(null);

  useEffect(() => {
    if (config && !local) {
      setLocal({
        mode: config.mode,
        adlib_threshold_grams: config.adlib_threshold_grams,
        target_dispense_grams: config.target_dispense_grams,
        slot_count: config.slot_count,
        ...Object.fromEntries(FEED_KEYS.map(k => [k, config[k] || ''])),
      });
    }
  }, [config]);

  if (!local) return <div className="spinner-center"><span className="spinner spinner-lg" /></div>;

  const isAdlib = local.mode === 'ad-libitum';

  async function save() {
    setSaving(true);
    try {
      await updateConfig(local);
      toast.success('Feed mode saved!');
    } catch (e) {
      toast.error(e.message);
    } finally {
      setSaving(false);
    }
  }

  function setTime(idx, val) {
    setLocal(l => ({ ...l, [`feed_time_${idx}`]: val }));
  }

  function addSlot() {
    if (local.slot_count >= 8) return;
    setLocal(l => ({ ...l, slot_count: l.slot_count + 1 }));
  }

  function removeSlot() {
    if (local.slot_count <= 1) return;
    const idx = local.slot_count - 1;
    setLocal(l => ({ ...l, slot_count: l.slot_count - 1, [`feed_time_${idx}`]: '' }));
  }

  return (
    <div>
      <div className="page-header">
        <h1 className="page-title">Feed Mode</h1>
        <p className="page-subtitle">Configure how the feeder operates</p>
      </div>

      {/* Mode toggle */}
      <div className="card" style={{ marginBottom: 12 }}>
        <div className="card-label">Operating Mode</div>
        <div className="mode-toggle" style={{ marginTop: 8 }}>
          <button
            className={`mode-toggle-btn ${isAdlib ? 'active' : ''}`}
            onClick={() => setLocal(l => ({ ...l, mode: 'ad-libitum' }))}
          >♻️ Ad-libitum</button>
          <button
            className={`mode-toggle-btn ${!isAdlib ? 'active' : ''}`}
            onClick={() => setLocal(l => ({ ...l, mode: 'scheduled' }))}
          >🕐 Scheduled</button>
        </div>
      </div>

      {/* Ad-libitum settings */}
      {isAdlib && (
        <div className="card" style={{ marginBottom: 12 }}>
          <div className="card-label">Threshold</div>
          <div className="form-group" style={{ marginTop: 8, marginBottom: 4 }}>
            <label className="form-label">Refill trigger (grams)</label>
            <input
              className="form-input"
              type="number"
              min={0}
              value={local.adlib_threshold_grams}
              onChange={e => setLocal(l => ({ ...l, adlib_threshold_grams: parseFloat(e.target.value) || 0 }))}
            />
            <p className="form-hint">
              The motor activates whenever the bowl weight drops below this value. Set to match your birds' minimum bowl level.
            </p>
          </div>
        </div>
      )}

      {/* Scheduled settings */}
      {!isAdlib && (
        <div className="card" style={{ marginBottom: 12 }}>
          <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', marginBottom: 12 }}>
            <div className="card-label" style={{ marginBottom: 0 }}>Feed Times ({local.slot_count}/8)</div>
            <div style={{ display: 'flex', gap: 6 }}>
              <button className="btn btn-sm btn-secondary" onClick={removeSlot} disabled={local.slot_count <= 1}>
                <Trash2 size={13} /> Remove
              </button>
              <button className="btn btn-sm btn-primary" onClick={addSlot} disabled={local.slot_count >= 8}>
                <Plus size={13} /> Add
              </button>
            </div>
          </div>
          <div className="time-grid">
            {Array.from({ length: local.slot_count }).map((_, i) => (
              <div key={i} className="time-slot">
                <span className="time-slot-label">Slot {i + 1}</span>
                <input
                  type="time"
                  value={local[`feed_time_${i}`] || ''}
                  onChange={e => setTime(i, e.target.value)}
                />
              </div>
            ))}
          </div>
        </div>
      )}

      {/* Target dispense */}
      <div className="card" style={{ marginBottom: 20 }}>
        <div className="card-label">Target Dispense per Feed</div>
        <div className="form-group" style={{ marginTop: 8, marginBottom: 4 }}>
          <label className="form-label">Grams per feed event</label>
          <input
            className="form-input"
            type="number"
            min={0}
            value={local.target_dispense_grams}
            onChange={e => setLocal(l => ({ ...l, target_dispense_grams: parseFloat(e.target.value) || 0 }))}
          />
          <p className="form-hint">
            Set to <strong style={{color:'var(--c-primary)'}}>0</strong> to let the device auto-calculate from flock data (breed × bird count × age).
          </p>
        </div>
      </div>

      <button className="btn btn-primary btn-full btn-lg" onClick={save} disabled={saving}>
        {saving ? <><span className="spinner" /> Saving…</> : '💾 Save Feed Mode'}
      </button>
    </div>
  );
}
