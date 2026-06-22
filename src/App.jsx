import React from 'react';
import { BrowserRouter, Routes, Route } from 'react-router-dom';
import { FeederProvider, useFeeder } from './context/FeederContext';
import Layout from './components/Layout/Layout';
import Dashboard from './screens/Dashboard';
import History from './screens/History';
import Settings from './screens/Settings';
import Alerts from './screens/Alerts';
import Onboarding from './screens/Onboarding';

// ── Shown when Supabase tables haven't been created yet ───────────────
function SqlSetupScreen() {
  return (
    <div style={{
      display: 'flex', alignItems: 'center', justifyContent: 'center',
      minHeight: '100dvh', flexDirection: 'column', gap: 16, padding: 24, textAlign: 'center',
    }}>
      <span style={{ fontSize: '3rem' }}>🗄️</span>
      <h1 style={{ fontSize: '1.4rem', fontWeight: 800, letterSpacing: '-0.02em' }}>
        Database Setup Required
      </h1>
      <p style={{ color: 'var(--c-text-muted)', fontSize: '0.9rem', maxWidth: 360, lineHeight: 1.6 }}>
        The required Supabase tables don't exist yet. Run the SQL setup script to create them.
      </p>

      <div style={{
        background: 'var(--c-surface)', border: '1px solid var(--c-border)',
        borderRadius: 16, padding: '20px 24px', maxWidth: 420, width: '100%', textAlign: 'left',
      }}>
        <div style={{ fontWeight: 700, marginBottom: 12, fontSize: '0.9rem' }}>Steps to fix:</div>
        {[
          'Open your Supabase project → SQL Editor',
          'Open supabase_setup.sql from your project root',
          'Paste its contents into the SQL Editor and click Run',
          'Go to Database → Replication → enable Realtime for: feeder_config, feeding_logs, alert_logs',
          'Refresh this page',
        ].map((step, i) => (
          <div key={i} style={{ display: 'flex', gap: 12, marginBottom: 10, alignItems: 'flex-start' }}>
            <span style={{
              minWidth: 24, height: 24, background: 'var(--c-primary)', borderRadius: '50%',
              display: 'flex', alignItems: 'center', justifyContent: 'center',
              fontSize: '0.72rem', fontWeight: 700, color: '#fff', flexShrink: 0,
            }}>{i + 1}</span>
            <span style={{ fontSize: '0.83rem', color: 'var(--c-text-muted)', lineHeight: 1.5 }}>{step}</span>
          </div>
        ))}
      </div>

      <button className="btn btn-primary" onClick={() => window.location.reload()}>
        🔄 Retry Connection
      </button>
    </div>
  );
}

function AppRoutes() {
  const { hasRow, configLoading, configError } = useFeeder();

  if (configLoading) {
    return (
      <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'center', minHeight: '100dvh', flexDirection: 'column', gap: 16 }}>
        <span style={{ fontSize: '2.5rem' }}>🐔</span>
        <div className="spinner spinner-lg" style={{ color: 'var(--c-primary)' }} />
        <span style={{ color: 'var(--c-text-muted)', fontSize: '0.9rem' }}>Connecting to feeder…</span>
      </div>
    );
  }

  // Tables don't exist yet
  const tablesNotSetup = configError && (
    configError.toLowerCase().includes('tables not set up') ||
    configError.toLowerCase().includes('does not exist') ||
    configError.toLowerCase().includes('relation')
  );
  if (tablesNotSetup) return <SqlSetupScreen />;

  // Tables exist but no row → first-run onboarding
  if (hasRow === false) {
    return (
      <Layout>
        <Onboarding />
      </Layout>
    );
  }

  return (
    <Layout>
      <Routes>
        <Route path="/"         element={<Dashboard />} />
        <Route path="/history"  element={<History />} />
        <Route path="/settings" element={<Settings />} />
        <Route path="/alerts"   element={<Alerts />} />
      </Routes>
    </Layout>
  );
}

export default function App() {
  return (
    <BrowserRouter>
      <FeederProvider>
        <AppRoutes />
      </FeederProvider>
    </BrowserRouter>
  );
}
