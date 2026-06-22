import React, { useState } from 'react';
import FeedMode from './FeedMode';
import FlockSettings from './FlockSettings';
import HopperManagement from './HopperManagement';
import Calibration from './Calibration';
import DeviceStatus from './DeviceStatus';

const TABS = [
  { id: 'feed',    label: '🕐 Feed Mode'  },
  { id: 'flock',   label: '🐔 Flock'      },
  { id: 'hopper',  label: '🪣 Hopper'     },
  { id: 'calib',   label: '⚖️ Calibration'},
  { id: 'device',  label: '📡 Device'     },
];

export default function Settings() {
  const [tab, setTab] = useState('feed');

  return (
    <div>
      {/* Horizontal scrollable sub-tabs */}
      <div style={{ overflowX: 'auto', marginBottom: 16, paddingBottom: 2 }}>
        <div style={{ display: 'flex', gap: 6, width: 'max-content' }}>
          {TABS.map(t => (
            <button
              key={t.id}
              onClick={() => setTab(t.id)}
              style={{
                padding: '8px 14px',
                borderRadius: 8,
                border: '1px solid',
                borderColor: tab === t.id ? 'var(--c-primary)' : 'var(--c-border)',
                background: tab === t.id ? 'var(--c-primary-glow)' : 'var(--c-surface-2)',
                color: tab === t.id ? 'var(--c-primary)' : 'var(--c-text-muted)',
                fontWeight: 600,
                fontSize: '0.8rem',
                cursor: 'pointer',
                whiteSpace: 'nowrap',
                transition: 'all 0.15s',
              }}
            >
              {t.label}
            </button>
          ))}
        </div>
      </div>

      {tab === 'feed'   && <FeedMode />}
      {tab === 'flock'  && <FlockSettings />}
      {tab === 'hopper' && <HopperManagement />}
      {tab === 'calib'  && <Calibration />}
      {tab === 'device' && <DeviceStatus />}
    </div>
  );
}
