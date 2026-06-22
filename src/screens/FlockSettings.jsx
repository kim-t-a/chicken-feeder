import React, { useState, useEffect } from 'react';
import { useFeeder } from '../context/FeederContext';
import { lookupFeedRequirement, BREED_OPTIONS } from '../lib/feedRequirements';
import toast from 'react-hot-toast';

export default function FlockSettings() {
  const { config, updateConfig } = useFeeder();
  const [saving, setSaving] = useState(false);
  const [local, setLocal] = useState(null);

  useEffect(() => {
    if (config && !local) {
      setLocal({
        breed: config.breed,
        chicken_count: config.chicken_count,
        age_weeks: config.age_weeks,
      });
    }
  }, [config]);

  if (!local) return <div className="spinner-center"><span className="spinner spinner-lg" /></div>;

  // Live requirement calculation
  const gramsPerBirdDay = lookupFeedRequirement(local.breed, local.age_weeks);
  const dailyTotal = gramsPerBirdDay != null ? gramsPerBirdDay * local.chicken_count : null;
  const slotCount = config?.slot_count || 2;
  const gramsPerFeed = dailyTotal != null ? dailyTotal / slotCount : null;

  async function save() {
    setSaving(true);
    try {
      await updateConfig(local);
      toast.success('Flock settings saved!');
    } catch (e) {
      toast.error(e.message);
    } finally {
      setSaving(false);
    }
  }

  return (
    <div>
      <div className="page-header">
        <h1 className="page-title">Flock Settings</h1>
        <p className="page-subtitle">Bird count, breed and age</p>
      </div>

      <div className="card" style={{ marginBottom: 12 }}>
        <div className="form-group">
          <label className="form-label">Breed</label>
          <select
            className="form-select"
            value={local.breed}
            onChange={e => setLocal(l => ({ ...l, breed: e.target.value }))}
          >
            {BREED_OPTIONS.map(o => <option key={o.value} value={o.value}>{o.label}</option>)}
          </select>
        </div>

        <div className="form-group">
          <label className="form-label">Number of Birds</label>
          <input
            className="form-input"
            type="number"
            min={1}
            max={50000}
            value={local.chicken_count}
            onChange={e => setLocal(l => ({ ...l, chicken_count: parseInt(e.target.value) || 1 }))}
          />
        </div>

        <div className="form-group" style={{ marginBottom: 0 }}>
          <label className="form-label">Age (weeks)</label>
          <div className="stepper">
            <button type="button" className="stepper-btn"
              onClick={() => setLocal(l => ({ ...l, age_weeks: Math.max(1, l.age_weeks - 1) }))}>−</button>
            <div className="stepper-val">{local.age_weeks} wk</div>
            <button type="button" className="stepper-btn"
              onClick={() => setLocal(l => ({ ...l, age_weeks: Math.min(52, l.age_weeks + 1) }))}>+</button>
          </div>
        </div>
      </div>

      {/* Live requirement display */}
      <div className="card" style={{ marginBottom: 20, background: 'var(--c-primary-glow)', borderColor: 'rgba(232,144,26,0.25)' }}>
        <div className="card-label" style={{ color: 'var(--c-primary)' }}>📊 Daily Feed Requirement</div>
        {dailyTotal != null ? (
          <>
            <div className="stats-grid" style={{ marginTop: 10 }}>
              <div className="stat-card">
                <div className="stat-value">{gramsPerBirdDay}g</div>
                <div className="stat-label">per bird/day</div>
              </div>
              <div className="stat-card">
                <div className="stat-value">{dailyTotal.toFixed(0)}g</div>
                <div className="stat-label">total/day</div>
              </div>
              <div className="stat-card">
                <div className="stat-value">{gramsPerFeed?.toFixed(0)}g</div>
                <div className="stat-label">per feed ({slotCount} slots)</div>
              </div>
            </div>
            <p className="form-hint" style={{ marginTop: 10 }}>
              Based on Kenyan Poultry Standards for {local.breed}, week {local.age_weeks}.
              Set <em>Target Dispense</em> to 0 in Feed Mode to use this automatically.
            </p>
          </>
        ) : (
          <p style={{ color: 'var(--c-text-muted)', fontSize: '0.85rem', marginTop: 8 }}>
            No standard data available for this breed/age combination. Set target dispense manually.
          </p>
        )}
      </div>

      <button className="btn btn-primary btn-full btn-lg" onClick={save} disabled={saving}>
        {saving ? <><span className="spinner" /> Saving…</> : '💾 Save Flock Settings'}
      </button>
    </div>
  );
}
