import React, { useMemo, useState } from 'react';
import { useFeeder } from '../context/FeederContext';
import { supabase } from '../lib/supabase';
import { BarChart, Bar, XAxis, YAxis, Tooltip, ResponsiveContainer, Cell } from 'recharts';
import { RefreshCw, Trash2, AlertTriangle } from 'lucide-react';
import toast from 'react-hot-toast';
import ConfirmModal from '../components/ConfirmModal';

// Always display in user's LOCAL timezone (browser handles this automatically)
function fmtDate(iso) {
  return new Date(iso).toLocaleDateString([], { month: 'short', day: 'numeric' });
}
function fmtTime(iso) {
  return new Date(iso).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
}
function fmtDateTime(iso) {
  return new Date(iso).toLocaleString([], {
    month: 'short', day: 'numeric',
    hour: '2-digit', minute: '2-digit',
  });
}

function CustomTooltip({ active, payload }) {
  if (!active || !payload?.length) return null;
  return (
    <div style={{ background: 'var(--c-surface-2)', border: '1px solid var(--c-border)', borderRadius: 8, padding: '8px 12px', fontSize: '0.8rem' }}>
      <div style={{ fontWeight: 700 }}>{payload[0].payload.label}</div>
      <div style={{ color: 'var(--c-primary)' }}>{payload[0].value?.toFixed(0)}g dispensed</div>
    </div>
  );
}

export default function History() {
  const { logs, logsLoading, totalToday, avgPerFeed, feedsToday, refetchLogs } = useFeeder();
  const [confirmClear, setConfirmClear] = useState(false);
  const [clearingAll, setClearingAll] = useState(false);
  const [deletingId, setDeletingId] = useState(null);

  // ── Clear all history ─────────────────────────────────────────────
  async function handleClearAll() {
    setClearingAll(true);
    try {
      const { error } = await supabase
        .from('feeding_logs')
        .delete()
        .neq('id', '00000000-0000-0000-0000-000000000000'); // delete all rows trick
      if (error) throw error;
      await refetchLogs();
      toast.success('All feeding history cleared.');
    } catch (e) {
      toast.error(`Failed to clear: ${e.message}`);
    } finally {
      setClearingAll(false);
      setConfirmClear(false);
    }
  }

  // ── Delete single log entry ───────────────────────────────────────
  async function handleDeleteOne(id) {
    setDeletingId(id);
    try {
      const { error } = await supabase.from('feeding_logs').delete().eq('id', id);
      if (error) throw error;
      await refetchLogs();
      toast.success('Entry deleted.');
    } catch (e) {
      toast.error(`Delete failed: ${e.message}`);
    } finally {
      setDeletingId(null);
    }
  }

  // ── 7-day chart data ──────────────────────────────────────────────
  const chartData = useMemo(() => {
    const days = {};
    const now = new Date();
    for (let i = 6; i >= 0; i--) {
      const d = new Date(now);
      d.setDate(d.getDate() - i);
      const key = d.toDateString();
      days[key] = { label: fmtDate(d.toISOString()), total: 0, count: 0 };
    }
    logs.forEach(l => {
      const key = new Date(l.logged_at).toDateString();
      if (days[key]) {
        days[key].total += l.amount_dispensed || 0;
        days[key].count++;
      }
    });
    return Object.values(days);
  }, [logs]);

  const todayLabel = fmtDate(new Date().toISOString());

  return (
    <div>
      {/* Header */}
      <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', marginBottom: 16 }}>
        <div>
          <h1 className="page-title">Feeding History</h1>
          <p className="page-subtitle">All dispensing events</p>
        </div>
        <div style={{ display: 'flex', gap: 6 }}>
          <button className="btn btn-secondary btn-sm" onClick={refetchLogs} disabled={logsLoading}>
            <RefreshCw size={13} /> Refresh
          </button>
          {logs.length > 0 && (
            <button className="btn btn-danger btn-sm" onClick={() => setConfirmClear(true)} disabled={clearingAll}>
              <Trash2 size={13} /> Clear All
            </button>
          )}
        </div>
      </div>

      {/* Today stats */}
      <div className="stats-grid" style={{ marginBottom: 12 }}>
        <div className="stat-card">
          <div className="stat-value">{totalToday.toFixed(0)}g</div>
          <div className="stat-label">Today total</div>
        </div>
        <div className="stat-card">
          <div className="stat-value">{feedsToday}</div>
          <div className="stat-label">Feeds today</div>
        </div>
        <div className="stat-card">
          <div className="stat-value">{feedsToday > 0 ? avgPerFeed.toFixed(0) : '—'}g</div>
          <div className="stat-label">Avg/feed</div>
        </div>
      </div>

      {/* 7-day bar chart */}
      <div className="card" style={{ marginBottom: 12 }}>
        <div className="card-label">Last 7 Days — Grams Dispensed</div>
        <div className="chart-wrap" style={{ marginTop: 12, height: 160 }}>
          <ResponsiveContainer width="100%" height="100%">
            <BarChart data={chartData} barCategoryGap="30%">
              <XAxis
                dataKey="label"
                tick={{ fill: 'var(--c-text-muted)', fontSize: 10 }}
                axisLine={false}
                tickLine={false}
              />
              <YAxis hide />
              <Tooltip content={<CustomTooltip />} cursor={{ fill: 'rgba(255,255,255,0.04)' }} />
              <Bar dataKey="total" radius={[4, 4, 0, 0]}>
                {chartData.map((entry, idx) => (
                  <Cell
                    key={idx}
                    fill={entry.label === todayLabel ? 'var(--c-primary)' : 'var(--c-surface-3)'}
                  />
                ))}
              </Bar>
            </BarChart>
          </ResponsiveContainer>
        </div>
      </div>

      {/* Log list */}
      {logsLoading ? (
        <div className="spinner-center"><span className="spinner spinner-lg" /></div>
      ) : logs.length === 0 ? (
        <div style={{ textAlign: 'center', color: 'var(--c-text-muted)', padding: '40px 0' }}>
          <div style={{ fontSize: '2rem', marginBottom: 8 }}>🥣</div>
          <div style={{ fontWeight: 600, marginBottom: 4 }}>No feeding events yet</div>
          <div style={{ fontSize: '0.82rem' }}>Events appear here when the device dispenses feed.</div>
        </div>
      ) : (
        <div className="log-list">
          {logs.map(log => (
            <div key={log.id} className="log-item">
              <span className="log-icon">🥣</span>
              <div className="log-info">
                <div className="log-amount">{(log.amount_dispensed || 0).toFixed(1)}g dispensed</div>
                <div className="log-weight">Feed in bowl after: {(log.current_weight || 0).toFixed(0)}g</div>
              </div>
              <div style={{ textAlign: 'right', flexShrink: 0 }}>
                <div style={{ fontSize: '0.82rem', fontWeight: 600, color: 'var(--c-text)' }}>
                  {fmtTime(log.logged_at)}
                </div>
                <div className="log-time">{fmtDate(log.logged_at)}</div>
              </div>
              {/* Per-item delete */}
              <button
                onClick={() => handleDeleteOne(log.id)}
                disabled={deletingId === log.id}
                style={{
                  background: 'none', border: 'none', cursor: 'pointer',
                  color: 'var(--c-text-dim)', padding: '4px', marginLeft: 2,
                  display: 'flex', alignItems: 'center', borderRadius: 4,
                  transition: 'color 0.15s',
                }}
                onMouseEnter={e => e.currentTarget.style.color = 'var(--c-danger)'}
                onMouseLeave={e => e.currentTarget.style.color = 'var(--c-text-dim)'}
                title="Delete entry"
              >
                {deletingId === log.id
                  ? <span className="spinner" style={{ width: 14, height: 14 }} />
                  : <Trash2 size={14} />}
              </button>
            </div>
          ))}
        </div>
      )}

      {/* Confirm clear all modal */}
      {confirmClear && (
        <ConfirmModal
          title="⚠️ Clear All History?"
          body={`This will permanently delete all ${logs.length} feeding records. This cannot be undone.`}
          confirmLabel={clearingAll ? 'Clearing…' : 'Yes, Delete All'}
          confirmClass="btn-danger"
          onConfirm={handleClearAll}
          onCancel={() => setConfirmClear(false)}
        />
      )}
    </div>
  );
}
