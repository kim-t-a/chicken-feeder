import { useState, useEffect, useRef } from 'react';
import { supabase } from '../lib/supabase';

const DEFAULT_CONFIG = {
  mode: 'scheduled',
  breed: 'broiler',
  chicken_count: 50,
  age_weeks: 4,
  adlib_threshold_grams: 300,
  target_dispense_grams: 0,
  calibration_factor: 1.0,
  calibration_trigger: false,
  known_weight_grams: 100,
  tare_trigger: false,
  motor_enabled: true,
  jam_detected: false,
  alert_cause: '',
  alert_message: '',
  scheduled_feed_trigger: false,
  slot_count: 2,
  feed_time_0: '07:00',
  feed_time_1: '17:00',
  feed_time_2: '',
  feed_time_3: '',
  feed_time_4: '',
  feed_time_5: '',
  feed_time_6: '',
  feed_time_7: '',
  updated_at: null,
};

export function useFeederConfig() {
  const [config, setConfig] = useState(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState(null);
  const [hasRow, setHasRow] = useState(null); // null = unknown, true/false
  const configIdRef = useRef(null);
  const channelRef = useRef(null);

  // ── Initial fetch ─────────────────────────────────────────────────
  async function fetchConfig() {
    try {
      setLoading(true);
      const { data, error: err } = await supabase
        .from('feeder_config')
        .select('*')
        .limit(1)
        .maybeSingle();

      if (err) {
        // Table does not exist yet (42P01) or any 4xx — treat as no row
        // so the onboarding/setup screen is shown with a helpful message
        const code = err.code || '';
        const msg  = (err.message || '').toLowerCase();
        const isTableMissing =
          code === '42P01' ||
          msg.includes('does not exist') ||
          msg.includes('relation') ||
          err.details?.includes('404') ||
          String(err.status) === '404';

        if (isTableMissing) {
          console.warn('[PoultryPal] feeder_config table missing — showing setup screen');
          setHasRow(false);
          setConfig(DEFAULT_CONFIG);
          setError('Supabase tables not set up yet. Run supabase_setup.sql first.');
        } else {
          throw err;
        }
      } else if (!data) {
        setHasRow(false);
        setConfig(DEFAULT_CONFIG);
      } else {
        setHasRow(true);
        configIdRef.current = data.id;
        setConfig({ ...DEFAULT_CONFIG, ...data });
      }
    } catch (e) {
      // Any other error: still show UI with defaults rather than spinning
      console.error('[PoultryPal] fetchConfig error:', e);
      setError(e.message);
      setHasRow(false);
      setConfig(DEFAULT_CONFIG);
    } finally {
      setLoading(false);
    }
  }

  // ── Realtime subscription ─────────────────────────────────────────
  function subscribe() {
    if (channelRef.current) channelRef.current.unsubscribe();

    channelRef.current = supabase
      .channel('feeder_config_changes')
      .on(
        'postgres_changes',
        { event: '*', schema: 'public', table: 'feeder_config' },
        (payload) => {
          if (payload.new) {
            configIdRef.current = payload.new.id;
            setHasRow(true);
            setConfig((prev) => ({ ...prev, ...payload.new }));
          }
        }
      )
      .subscribe();
  }

  useEffect(() => {
    fetchConfig();
    subscribe();
    return () => {
      channelRef.current?.unsubscribe();
    };
  }, []);

  // ── Lightweight polling fallback for live bowl weight ─────────────
  // Realtime is the primary path but can occasionally lag or miss events.
  // This 2s poll fetches ONLY the 2 fields the dashboard ring needs, so
  // the display is never more than 2s stale — without re-fetching everything.
  useEffect(() => {
    const id = setInterval(async () => {
      if (!configIdRef.current) return; // skip until first sync sets the ID
      try {
        const { data } = await supabase
          .from('feeder_config')
          .select('current_bowl_weight, updated_at')
          .eq('id', configIdRef.current)
          .maybeSingle();
        if (data) {
          setConfig((prev) => prev ? { ...prev, ...data } : prev);
        }
      } catch (_) {
        // polling errors are silent — Realtime is still the primary path
      }
    }, 2000);
    return () => clearInterval(id);
  }, []);

  // ── Update helper ─────────────────────────────────────────────────
  async function updateConfig(patch) {
    if (!configIdRef.current) {
      throw new Error('No feeder_config row to update');
    }
    const { error: err } = await supabase
      .from('feeder_config')
      .update({ ...patch, updated_at: new Date().toISOString() })
      .eq('id', configIdRef.current);
    if (err) throw err;
    setConfig((prev) => ({ ...prev, ...patch }));
  }

  // ── Create initial row (onboarding) ───────────────────────────────
  async function createConfig(initial = {}) {
    const { data, error: err } = await supabase
      .from('feeder_config')
      .insert([{ ...DEFAULT_CONFIG, ...initial, updated_at: new Date().toISOString() }])
      .select()
      .single();
    if (err) throw err;
    configIdRef.current = data.id;
    setHasRow(true);
    setConfig({ ...DEFAULT_CONFIG, ...data });
    return data;
  }

  return {
    config,
    loading,
    error,
    hasRow,
    updateConfig,
    createConfig,
    refetch: fetchConfig,
    configId: configIdRef.current,
  };
}
