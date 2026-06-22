/**
 * Browser Push Notification helpers.
 * Uses the Web Notifications API + service worker.
 * 
 * NOTE: True "push while app is closed" requires a push server (VAPID).
 * This implementation covers foreground + background tab notifications.
 * The service worker keeps notifications working when the tab is minimised.
 */

export async function requestNotificationPermission() {
  if (!('Notification' in window)) return 'unsupported';
  if (Notification.permission === 'granted') return 'granted';
  if (Notification.permission === 'denied') return 'denied';
  const result = await Notification.requestPermission();
  return result;
}

export function showNotification(title, options = {}) {
  if (!('Notification' in window)) return;
  if (Notification.permission !== 'granted') return;

  // Prefer service worker notification (works in background)
  if ('serviceWorker' in navigator) {
    navigator.serviceWorker.ready.then((reg) => {
      reg.showNotification(title, {
        icon: '/icons/icon-192.png',
        badge: '/icons/icon-72.png',
        vibrate: [200, 100, 200],
        ...options,
      });
    });
  } else {
    new Notification(title, {
      icon: '/icons/icon-192.png',
      ...options,
    });
  }
}

export function notifyAlert(alertCause, alertMessage) {
  const titles = {
    hopper_empty: '🚨 Hopper Empty!',
    hopper_low: '⚠️ Hopper Low',
    jam: '🔴 Motor Jam Detected!',
    partial_jam_or_low_hopper: '⚠️ Check Feeder',
  };
  const title = titles[alertCause] || '⚠️ Feeder Alert';
  showNotification(title, {
    body: alertMessage || 'Your feeder needs attention.',
    tag: 'feeder-alert', // prevents duplicate notifications
    requireInteraction: alertCause === 'jam' || alertCause === 'hopper_empty',
  });
}

export function notifyFeedComplete(grams) {
  showNotification('✅ Feed Dispensed', {
    body: `${grams.toFixed(0)} g dispensed successfully.`,
    tag: 'feed-complete',
  });
}
