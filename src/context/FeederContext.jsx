import React, { createContext, useContext, useEffect, useRef, useState } from 'react';
import { useFeederConfig } from '../hooks/useFeederConfig';
import { useFeedingLogs } from '../hooks/useFeedingLogs';
import { useHopper } from '../hooks/useHopper';
import { notifyAlert } from '../lib/notifications';
import toast from 'react-hot-toast';

const FeederContext = createContext(null);

export function FeederProvider({ children }) {
  const hopper = useHopper();

  // Pass hopper.deduct as callback to feeding logs hook
  const feedingLogs = useFeedingLogs({
    onNewLog: (log) => {
      if (log.amount_dispensed > 0) {
        hopper.deduct(log.amount_dispensed);
      }
    },
  });

  const feederConfig = useFeederConfig();

  // ── Sync missed hopper dispensing on mount ────────────────────────
  const syncedRef = useRef(false);
  useEffect(() => {
    if (!syncedRef.current) {
      syncedRef.current = true;
      hopper.syncMissedLogs();
    }
  }, []);

  // ── Watch for new alerts via realtime config changes ──────────────
  const prevAlertCauseRef = useRef('');
  useEffect(() => {
    if (!feederConfig.config) return;
    const cause = feederConfig.config.alert_cause || '';
    if (cause && cause !== prevAlertCauseRef.current) {
      // New alert
      notifyAlert(cause, feederConfig.config.alert_message);
    }
    prevAlertCauseRef.current = cause;
  }, [feederConfig.config?.alert_cause]);

  // ── Device online status — refreshes every 5 s via ticker ──────────
  // Without the ticker, deviceStatus only updates on Supabase Realtime events.
  // If the browser sits idle between events, online can appear false even though
  // the ESP32 is actively patching updated_at every 5 s.
  const [, setTick] = useState(0);
  useEffect(() => {
    const id = setInterval(() => setTick(t => t + 1), 2000); // 2s: online status refreshes faster
    return () => clearInterval(id);
  }, []);

  function getDeviceStatus() {
    if (!feederConfig.config?.updated_at) return { online: false, lastSeenAgo: null, recentEnough: false };
    const lastSeen = new Date(feederConfig.config.updated_at);
    const diff = (Date.now() - lastSeen.getTime()) / 1000; // seconds
    // Negative diff = ESP32 clock is ahead of browser (EAT vs UTC) → treat as online
    const effectiveDiff = diff < 0 ? 0 : diff;
    return {
      online:       effectiveDiff < 45,
      recentEnough: effectiveDiff < 90, // Feed Now button threshold
      lastSeenAgo:  effectiveDiff,
      rawDiff:      diff,
      lastSeen,
    };
  }

  // ── Convenience: clear alert ──────────────────────────────────────
  async function clearAlert() {
    try {
      await feederConfig.updateConfig({
        jam_detected: false,
        motor_enabled: true,
        alert_cause: '',
        alert_message: '',
      });
      toast.success('Alert cleared. Motor re-enabled.');
    } catch (e) {
      toast.error(`Failed to clear alert: ${e.message}`);
    }
  }

  // ── Convenience: trigger feed now ────────────────────────────────
  async function feedNow() {
    try {
      await feederConfig.updateConfig({ scheduled_feed_trigger: true });
      toast.success('Feed command sent! Waiting for device…');
    } catch (e) {
      toast.error(`Feed Now failed: ${e.message}`);
    }
  }

  // ── Convenience: tare scale ───────────────────────────────────────
  async function tareScale() {
    try {
      await feederConfig.updateConfig({ tare_trigger: true });
      toast.success('Tare command sent! Waiting for device…');
    } catch (e) {
      toast.error(`Tare failed: ${e.message}`);
    }
  }

  // ── Convenience: toggle motor ─────────────────────────────────────
  async function toggleMotor() {
    try {
      const next = !feederConfig.config.motor_enabled;
      await feederConfig.updateConfig({ motor_enabled: next });
      toast.success(next ? 'Motor enabled.' : 'Motor disabled.');
    } catch (e) {
      toast.error(`Motor toggle failed: ${e.message}`);
    }
  }

  const deviceStatus = getDeviceStatus();

  const value = {
    // Config
    config: feederConfig.config,
    configLoading: feederConfig.loading,
    configError: feederConfig.error,
    hasRow: feederConfig.hasRow,
    updateConfig: feederConfig.updateConfig,
    createConfig: feederConfig.createConfig,
    // Logs
    logs: feedingLogs.logs,
    logsLoading: feedingLogs.loading,
    latestLog: feedingLogs.latestLog,
    todayLogs: feedingLogs.todayLogs,
    totalToday: feedingLogs.totalToday,
    avgPerFeed: feedingLogs.avgPerFeed,
    feedsToday: feedingLogs.feedsToday,
    refetchLogs: feedingLogs.refetch,
    // Hopper
    hopper,
    // Device
    deviceStatus,
    // Actions
    clearAlert,
    feedNow,
    tareScale,
    toggleMotor,
  };

  return <FeederContext.Provider value={value}>{children}</FeederContext.Provider>;
}

export function useFeeder() {
  const ctx = useContext(FeederContext);
  if (!ctx) throw new Error('useFeeder must be used within FeederProvider');
  return ctx;
}
