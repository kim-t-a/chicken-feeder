import React from 'react';
import { NavLink, useLocation } from 'react-router-dom';
import { LayoutDashboard, History, Settings, Bell } from 'lucide-react';
import { useFeeder } from '../../context/FeederContext';

const TABS = [
  { to: '/',        icon: LayoutDashboard, label: 'Dashboard' },
  { to: '/history', icon: History,         label: 'History'   },
  { to: '/settings',icon: Settings,        label: 'Settings'  },
  { to: '/alerts',  icon: Bell,            label: 'Alerts'    },
];

export default function BottomNav() {
  const { config } = useFeeder();
  const hasAlert = config?.alert_cause && config.alert_cause !== '';

  return (
    <nav style={{
      position: 'fixed',
      bottom: 0,
      left: 0,
      right: 0,
      height: 'var(--nav-h)',
      background: 'rgba(22,27,36,0.95)',
      backdropFilter: 'blur(20px)',
      borderTop: '1px solid var(--c-border)',
      display: 'flex',
      alignItems: 'center',
      justifyContent: 'space-around',
      zIndex: 100,
      paddingBottom: 'env(safe-area-inset-bottom)',
    }}>
      {TABS.map(({ to, icon: Icon, label }) => {
        const isAlerts = label === 'Alerts';
        return (
          <NavLink
            key={to}
            to={to}
            end={to === '/'}
            style={({ isActive }) => ({
              display: 'flex',
              flexDirection: 'column',
              alignItems: 'center',
              gap: 3,
              textDecoration: 'none',
              color: isActive ? 'var(--c-primary)' : 'var(--c-text-muted)',
              fontSize: '0.65rem',
              fontWeight: 600,
              padding: '6px 16px',
              borderRadius: 10,
              transition: 'color 0.15s',
              position: 'relative',
              minWidth: 56,
            })}
          >
            {({ isActive }) => (
              <>
                <span style={{ position: 'relative' }}>
                  <Icon size={22} strokeWidth={isActive ? 2.5 : 1.8} />
                  {isAlerts && hasAlert && (
                    <span style={{
                      position: 'absolute',
                      top: -3,
                      right: -5,
                      width: 8,
                      height: 8,
                      background: 'var(--c-danger)',
                      borderRadius: '50%',
                      border: '1.5px solid var(--c-bg)',
                    }} />
                  )}
                </span>
                <span>{label}</span>
                {isActive && (
                  <span style={{
                    position: 'absolute',
                    top: 0,
                    width: 28,
                    height: 2,
                    background: 'var(--c-primary)',
                    borderRadius: '0 0 2px 2px',
                  }} />
                )}
              </>
            )}
          </NavLink>
        );
      })}
    </nav>
  );
}
