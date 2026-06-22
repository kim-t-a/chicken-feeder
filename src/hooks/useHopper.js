import { useState, useCallback } from 'react';
import { supabase } from '../lib/supabase';

const STORAGE_KEY = 'poultrypal_hopper';
const DEFAULT_CAPACITY = 2000; // grams

function loadFromStorage() {
  try {
    const raw = localStorage.getItem(STORAGE_KEY);
    if (!raw) return null;
    return JSON.parse(raw);
  } catch {
    return null;
  }
}

function saveToStorage(state) {
  localStorage.setItem(STORAGE_KEY, JSON.stringify(state));
}

function getInitialState() {
  const stored = loadFromStorage();
  if (stored) return stored;
  return {
    capacity: DEFAULT_CAPACITY,
    currentGrams: DEFAULT_CAPACITY,
    lastUpdated: new Date(0).toISOString(),
  };
}

export function useHopper() {
  const [state, setState] = useState(getInitialState);

  const percent = Math.max(0, Math.min(100, (state.currentGrams / state.capacity) * 100));

  // ── Deduct grams (called from useFeedingLogs new-log callback) ────
  const deduct = useCallback((grams) => {
    setState((prev) => {
      const next = {
        ...prev,
        currentGrams: Math.max(0, prev.currentGrams - grams),
        lastUpdated: new Date().toISOString(),
      };
      saveToStorage(next);
      return next;
    });
  }, []);

  // ── Record a refill ───────────────────────────────────────────────
  const refill = useCallback(async (addedGrams, notes = '') => {
    setState((prev) => {
      const next = {
        ...prev,
        currentGrams: Math.min(prev.capacity, prev.currentGrams + addedGrams),
        lastUpdated: new Date().toISOString(),
      };
      saveToStorage(next);
      return next;
    });

    // Persist to Supabase hopper_refills table
    try {
      await supabase.from('hopper_refills').insert([
        {
          added_grams: addedGrams,
          notes,
          refilled_at: new Date().toISOString(),
        },
      ]);
    } catch (e) {
      console.error('Failed to log refill to Supabase:', e);
    }
  }, []);

  // ── Update capacity ───────────────────────────────────────────────
  const setCapacity = useCallback((newCapacity) => {
    setState((prev) => {
      const next = {
        ...prev,
        capacity: newCapacity,
        currentGrams: Math.min(prev.currentGrams, newCapacity),
      };
      saveToStorage(next);
      return next;
    });
  }, []);

  // ── Manually override current level ──────────────────────────────
  const setCurrentGrams = useCallback((grams) => {
    setState((prev) => {
      const next = {
        ...prev,
        currentGrams: Math.max(0, Math.min(prev.capacity, grams)),
        lastUpdated: new Date().toISOString(),
      };
      saveToStorage(next);
      return next;
    });
  }, []);

  // ── Factory reset (clears localStorage) ──────────────────────────
  const resetHopper = useCallback(() => {
    const fresh = {
      capacity: DEFAULT_CAPACITY,
      currentGrams: DEFAULT_CAPACITY,
      lastUpdated: new Date().toISOString(),
    };
    saveToStorage(fresh);
    setState(fresh);
  }, []);

  // ── Sync missed dispensing since last update ───────────────────────
  const syncMissedLogs = useCallback(async () => {
    try {
      const { data } = await supabase
        .from('feeding_logs')
        .select('amount_dispensed')
        .gt('logged_at', state.lastUpdated)
        .order('logged_at', { ascending: true });

      if (data && data.length > 0) {
        const totalMissed = data.reduce((sum, l) => sum + (l.amount_dispensed || 0), 0);
        setState((prev) => {
          const next = {
            ...prev,
            currentGrams: Math.max(0, prev.currentGrams - totalMissed),
            lastUpdated: new Date().toISOString(),
          };
          saveToStorage(next);
          return next;
        });
      }
    } catch (e) {
      console.error('Failed to sync missed logs:', e);
    }
  }, [state.lastUpdated]);

  return {
    currentGrams: state.currentGrams,
    capacity: state.capacity,
    percent,
    lastUpdated: state.lastUpdated,
    deduct,
    refill,
    setCapacity,
    setCurrentGrams,
    resetHopper,
    syncMissedLogs,
  };
}
