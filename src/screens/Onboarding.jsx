import React, { useState } from 'react';
import { useFeeder } from '../context/FeederContext';
import toast from 'react-hot-toast';
import { BREED_OPTIONS } from '../lib/feedRequirements';

const INITIAL = {
  breed: 'broiler',
  chicken_count: 50,
  age_weeks: 4,
};

export default function Onboarding() {
  const { createConfig } = useFeeder();
  const [form, setForm] = useState(INITIAL);
  const [loading, setLoading] = useState(false);

  async function handleSubmit(e) {
    e.preventDefault();
    setLoading(true);
    try {
      await createConfig(form);
      toast.success('Feeder set up successfully!');
    } catch (err) {
      toast.error(`Setup failed: ${err.message}`);
    } finally {
      setLoading(false);
    }
  }

  return (
    <div className="onboarding-wrap">
      <div className="onboarding-icon">🐔</div>
      <h1 className="onboarding-title">Welcome to PoultryPal</h1>
      <p className="onboarding-desc">
        Let's get your feeder configured. You can change all of these settings later.
      </p>

      <form onSubmit={handleSubmit} style={{ width: '100%', maxWidth: 360 }}>
        <div className="form-group">
          <label className="form-label">Breed</label>
          <select
            className="form-select"
            value={form.breed}
            onChange={e => setForm(f => ({ ...f, breed: e.target.value }))}
          >
            {BREED_OPTIONS.map(o => (
              <option key={o.value} value={o.value}>{o.label}</option>
            ))}
          </select>
        </div>

        <div className="form-group">
          <label className="form-label">Number of Birds</label>
          <input
            className="form-input"
            type="number"
            min={1}
            max={10000}
            value={form.chicken_count}
            onChange={e => setForm(f => ({ ...f, chicken_count: parseInt(e.target.value) || 1 }))}
          />
        </div>

        <div className="form-group">
          <label className="form-label">Age (weeks)</label>
          <div className="stepper">
            <button type="button" className="stepper-btn"
              onClick={() => setForm(f => ({ ...f, age_weeks: Math.max(1, f.age_weeks - 1) }))}>−</button>
            <div className="stepper-val">{form.age_weeks}</div>
            <button type="button" className="stepper-btn"
              onClick={() => setForm(f => ({ ...f, age_weeks: Math.min(52, f.age_weeks + 1) }))}>+</button>
          </div>
        </div>

        <button className="btn btn-primary btn-full btn-lg" type="submit" disabled={loading}>
          {loading ? <><span className="spinner" /> Setting up…</> : '🚀 Set Up Feeder'}
        </button>
      </form>
    </div>
  );
}
