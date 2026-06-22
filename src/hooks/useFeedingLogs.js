import { useState, useEffect, useRef } from 'react';
import { supabase } from '../lib/supabase';

const MAX_LOGS = 200;

export function useFeedingLogs({ onNewLog } = {}) {
  const [logs, setLogs] = useState([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState(null);
  const channelRef = useRef(null);
  const onNewLogRef = useRef(onNewLog);
  onNewLogRef.current = onNewLog;

  // ── Initial fetch: last 200 rows ──────────────────────────────────
  async function fetchLogs() {
    try {
      setLoading(true);
      const { data, error: err } = await supabase
        .from('feeding_logs')
        .select('*')
        .order('logged_at', { ascending: false })
        .limit(MAX_LOGS);

      if (err) {
        // Table missing → treat as empty, not an error worth crashing for
        const code = err.code || '';
        const msg = (err.message || '').toLowerCase();
        if (code === '42P01' || msg.includes('does not exist') || msg.includes('relation')) {
          console.warn('[PoultryPal] feeding_logs table missing — returning empty');
          setLogs([]);
          return;
        }
        throw err;
      }
      setLogs(data || []);
    } catch (e) {
      console.error('[PoultryPal] fetchLogs error:', e);
      setError(e.message);
      setLogs([]);
    } finally {
      setLoading(false);
    }
  }

  // ── Realtime subscription: new inserts ────────────────────────────
  function subscribe() {
    if (channelRef.current) channelRef.current.unsubscribe();

    channelRef.current = supabase
      .channel('feeding_logs_inserts')
      .on(
        'postgres_changes',
        { event: 'INSERT', schema: 'public', table: 'feeding_logs' },
        (payload) => {
          const newLog = payload.new;
          setLogs((prev) => [newLog, ...prev].slice(0, MAX_LOGS));
          if (onNewLogRef.current) onNewLogRef.current(newLog);
        }
      )
      .subscribe();
  }

  useEffect(() => {
    fetchLogs();
    subscribe();
    return () => {
      channelRef.current?.unsubscribe();
    };
  }, []);

  // ── Derived helpers ───────────────────────────────────────────────
  const latestLog = logs[0] || null;

  const todayLogs = logs.filter((l) => {
    const d = new Date(l.logged_at);
    const now = new Date();
    return (
      d.getFullYear() === now.getFullYear() &&
      d.getMonth() === now.getMonth() &&
      d.getDate() === now.getDate()
    );
  });

  const totalToday = todayLogs.reduce((sum, l) => sum + (l.amount_dispensed || 0), 0);
  const avgPerFeed = todayLogs.length > 0 ? totalToday / todayLogs.length : 0;

  return {
    logs,
    loading,
    error,
    latestLog,
    todayLogs,
    totalToday,
    avgPerFeed,
    feedsToday: todayLogs.length,
    refetch: fetchLogs,
  };
}
