import React, { useState } from 'react';
import { AlertTriangle, X, ShieldAlert } from 'lucide-react';
import { useFeeder } from '../context/FeederContext';
import ConfirmModal from './ConfirmModal';

const ALERT_META = {
  hopper_empty:             { icon: '🪣', level: 'danger', label: 'Hopper Empty' },
  hopper_low:               { icon: '⚠️', level: 'warning', label: 'Hopper Low' },
  jam:                      { icon: '🔴', level: 'danger', label: 'Motor Jam' },
  partial_jam_or_low_hopper:{ icon: '🟡', level: 'warning', label: 'Partial Jam / Low Hopper' },
};

export default function AlertBanner() {
  const { config, clearAlert } = useFeeder();
  const [confirming, setConfirming] = useState(false);
  const [loading, setLoading] = useState(false);

  if (!config?.alert_cause) return null;

  const meta = ALERT_META[config.alert_cause] || { icon: '⚠️', level: 'warning', label: 'Alert' };
  const isJam = config.alert_cause === 'jam';

  async function handleClear() {
    setLoading(true);
    try {
      await clearAlert();
    } finally {
      setLoading(false);
      setConfirming(false);
    }
  }

  return (
    <>
      <div className={`alert-banner ${meta.level}`}>
        <span className="alert-banner-icon">{meta.icon}</span>
        <div className="alert-banner-body">
          <div className="alert-banner-title">{meta.label}</div>
          <div className="alert-banner-msg">
            {config.alert_message || 'Check your feeder hardware.'}
          </div>
          <div className="alert-banner-actions">
            <button
              className="btn btn-sm btn-danger"
              onClick={() => setConfirming(true)}
              disabled={loading}
            >
              {loading ? <span className="spinner" /> : <X size={14} />}
              Clear &amp; Re-enable
            </button>
          </div>
        </div>
      </div>

      {confirming && (
        <ConfirmModal
          title={isJam ? '⚠️ Jam Alert — Confirm Inspection' : 'Clear Alert'}
          body={
            isJam
              ? 'The motor has failed to recover from a jam after 3 attempts. Please physically inspect the cone neck and flap outlet before re-enabling. Only proceed if the blockage has been cleared.'
              : 'This will clear the alert and re-enable the motor. Are you sure the issue has been resolved?'
          }
          confirmLabel={loading ? 'Clearing…' : 'I\'ve Inspected — Clear Alert'}
          confirmClass="btn-danger"
          onConfirm={handleClear}
          onCancel={() => setConfirming(false)}
        />
      )}
    </>
  );
}
