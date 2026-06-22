import React from 'react';
import BottomNav from './BottomNav';
import { Toaster } from 'react-hot-toast';

export default function Layout({ children }) {
  return (
    <div className="app-shell">
      <main className="page-content">
        {children}
      </main>
      <BottomNav />
      <Toaster
        position="top-center"
        toastOptions={{
          style: {
            background: 'var(--c-surface-2)',
            color: 'var(--c-text)',
            border: '1px solid var(--c-border)',
            borderRadius: 10,
            fontSize: '0.88rem',
            fontWeight: 500,
          },
          success: { iconTheme: { primary: 'var(--c-ok)', secondary: 'white' } },
          error: { iconTheme: { primary: 'var(--c-danger)', secondary: 'white' } },
        }}
      />
    </div>
  );
}
