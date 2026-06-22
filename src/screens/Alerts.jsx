import React, { useState, useEffect } from 'react';
import { useFeeder } from '../context/FeederContext';
import { supabase } from '../lib/supabase';
import toast from 'react-hot-toast';
import { Bell, BellOff, CheckCircle } from 'lucide-react';
import { requestNotificationPermission } from '../lib/notifications';

const ALERT_META = {
  hopper_empty:              { icon: '🪣', color: 'var(--c-danger)',  label: 'Hopper Empty',
    guidance: 'Refill the hopper immediately to prevent missed feeding events.' },
  hopper_low:                { icon: '⚠️', color: 'var(--c-warning)', label: 'Hopper Low',
    guidance: 'Hopper is running low. Schedule a refill soon.' },
  jam:                       { icon: '🔴', color: 'var(--c-danger)',  label: 'Motor Jam',
    guidance: 'Physically inspect the cone neck and flap outlet. Remove any obstructions before re-enabling.' },
  partial_jam_or_low_hopper: { icon: '🟡', color: 'var(--c-warning)', label: 'Partial Jam / Low Hopper',
    guidance: 'Check the dispensing outlet for partial blockage AND verify hopper level. Clear any obstruction and refill if needed.' },
};

function AlertCard({ alert, onClear }) {
  const meta = ALERT_META[alert.alert_cause] || { icon: '⚠️', color: 'var(--c-warning)', label: 'Alert', guidance: '' };
  const [clearing, setClearing] = useState(false);

  async function handleClear() {
    setClearing(true);
    try {
      await supabase.from('alert_logs').update({ is_cleared: true, cleared_at: new Date().toISOString() }).eq('id', alert.id);
      if (onClear) onClear(alert.id);
      toast.success('Alert cleared');
    } catch (e) {
      toast.error(e.message);
    } finally {
      setClearing(false);
    }
  }

  return (
    <div className="card" style={{ borderLeft: `4px solid ${meta.color}`, marginBottom: 10 }}>
      <div style={{ display: 'flex', gap: 12, alignItems: 'flex-start' }}>
        <span style={{ fontSize: '1.4rem' }}>{meta.icon}</span>
        <div style={{ flex: 1 }}>
          <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', gap: 8 }}>
            <span style={{ fontWeight: 700, fontSize: '0.9rem', color: meta.color }}>{meta.label}</span>
            <span style={{ fontSize: '0.72rem', color: 'var(--c-text-muted)' }}>
              {new Date(alert.logged_at).toLocaleString([], { month: 'short', day: 'numeric', hour: '2-digit', minute: '2-digit' })}
            </span>
          </div>
          {alert.alert_message && (
            <div style={{ fontSize: '0.82rem', color: 'var(--c-text-muted)', marginTop: 3, marginBottom: 6 }}>
              {alert.alert_message}
            </div>
          )}
          <div className="info-box" style={{ marginBottom: 10, padding: '8px 10px' }}>
            <span className="info-box-icon" style={{ fontSize: '0.9rem' }}>💡</span>
            <span style={{ fontSize: '0.78rem' }}>{meta.guidance}</span>
          </div>
          {!alert.is_cleared && (
            <button className="btn btn-sm btn-secondary" onClick={handleClear} disabled={clearing}>
              {clearing ? <span className="spinner" /> : <CheckCircle size={13} />}
              Mark Resolved
            </button>
          )}
          {alert.is_cleared && (
            <span className="badge badge-ok" style={{ fontSize: '0.7rem' }}>✓ Resolved</span>
          )}
        </div>
      </div>
    </div>
  );
}

export default function Alerts() {
  const { config } = useFeeder();
  const [alerts, setAlerts] = useState([]);
  const [loading, setLoading] = useState(true);
  const [notifPerm, setNotifPerm] = useState(Notification?.permission || 'default');
  const [filter, setFilter] = useState('all'); // 'all' | 'active' | 'cleared'

  // Load alert_logs
  useEffect(() => {
    (async () => {
      try {
        const { data, error } = await supabase
          .from('alert_logs')
          .select('*')
          .order('logged_at', { ascending: false })
          .limit(100);
        // Silently swallow table-not-found — show empty state
        if (error) {
          const code = error.code || '';
          const msg = (error.message || '').toLowerCase();
          const missing = code === '42P01' || msg.includes('does not exist') || msg.includes('relation');
          if (!missing) console.error('[PoultryPal] alert_logs fetch error:', error);
        } else {
          setAlerts(data || []);
        }
      } finally {
        setLoading(false);
      }
    })();

    // Realtime subscription for new alert_logs
    const ch = supabase
      .channel('alert_logs_inserts')
      .on('postgres_changes', { event: 'INSERT', schema: 'public', table: 'alert_logs' }, (p) => {
        if (p.new) setAlerts(prev => [p.new, ...prev]);
      })
      .subscribe();

    return () => ch.unsubscribe();
  }, []);

  // Log current alert when it changes (only if table likely exists)
  useEffect(() => {
    if (!config?.alert_cause || config.alert_cause === '') return;
    const recent = alerts.find(a =>
      a.alert_cause === config.alert_cause &&
      !a.is_cleared &&
      (Date.now() - new Date(a.logged_at).getTime()) < 60000
    );
    if (!recent) {
      supabase.from('alert_logs').insert([{
        alert_cause: config.alert_cause,
        alert_message: config.alert_message || '',
        logged_at: new Date().toISOString(),
      }]).select().single().then(({ data, error }) => {
        if (!error && data) setAlerts(prev => [data, ...prev]);
      });
    }
  }, [config?.alert_cause]);

  async function enableNotifications() {
    const perm = await requestNotificationPermission();
    setNotifPerm(perm);
    if (perm === 'granted') toast.success('Notifications enabled!');
    else if (perm === 'denied') toast.error('Notifications blocked. Enable in browser settings.');
  }

  function handleCleared(id) {
    setAlerts(prev => prev.map(a => a.id === id ? { ...a, is_cleared: true, cleared_at: new Date().toISOString() } : a));
  }

  const filtered = alerts.filter(a => {
    if (filter === 'active') return !a.is_cleared;
    if (filter === 'cleared') return a.is_cleared;
    return true;
  });

  return (
    <div>
      <div style={{ display: 'flex', alignItems: 'flex-start', justifyContent: 'space-between', marginBottom: 16 }}>
        <div>
          <h1 className="page-title">Alerts</h1>
          <p className="page-subtitle">Event history &amp; notifications</p>
        </div>
        <button
          className={`btn btn-sm ${notifPerm === 'granted' ? 'btn-secondary' : 'btn-primary'}`}
          onClick={enableNotifications}
        >
          {notifPerm === 'granted' ? <><Bell size={13} /> On</> : <><BellOff size={13} /> Enable Push</>}
        </button>
      </div>

      {/* Current active alert from config */}
      {config?.alert_cause && (
        <div className="info-box" style={{ marginBottom: 12, borderLeft: '3px solid var(--c-danger)' }}>
          <span className="info-box-icon">🔴</span>
          <span style={{ fontWeight: 600, color: 'var(--c-danger)' }}>
            Active alert: {ALERT_META[config.alert_cause]?.label || config.alert_cause}
            {' — '}
            <span style={{ fontWeight: 400, color: 'var(--c-text-muted)' }}>go to Dashboard to clear</span>
          </span>
        </div>
      )}

      {/* Filter tabs */}
      <div className="sub-tabs">
        {['all', 'active', 'cleared'].map(f => (
          <button key={f} className={`sub-tab ${filter === f ? 'active' : ''}`} onClick={() => setFilter(f)}>
            {f.charAt(0).toUpperCase() + f.slice(1)}
          </button>
        ))}
      </div>

      {loading ? (
        <div className="spinner-center"><span className="spinner spinner-lg" /></div>
      ) : filtered.length === 0 ? (
        <div style={{ textAlign: 'center', color: 'var(--c-text-muted)', padding: '48px 0' }}>
          <div style={{ fontSize: '2rem', marginBottom: 8 }}>✅</div>
          <div style={{ fontWeight: 600, marginBottom: 4 }}>No alerts</div>
          <div style={{ fontSize: '0.82rem' }}>{filter === 'all' ? 'Your feeder is running smoothly.' : `No ${filter} alerts.`}</div>
        </div>
      ) : (
        filtered.map(alert => <AlertCard key={alert.id} alert={alert} onClear={handleCleared} />)
      )}
    </div>
  );
}
