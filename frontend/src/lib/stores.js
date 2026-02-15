import { writable } from 'svelte/store';
import { subscribe as wsSub, connect as wsConnect } from './ws.js';

// Connection state
export const wsConnected = writable(false);

// System status (from GET /api/status and WS "status" messages)
export const status = writable({
  usb_connected: false,
  cat_state: 'disconnected',
  free_heap: 0,
});

// Live control events (ring buffer of last 100)
export const controlLog = writable([]);

// Live CAT dispatch events (ring buffer of last 100)
export const catLog = writable([]);

// CAT ticker: merged TX+RX stream (ring buffer of last 20 for ticker display)
export const catTicker = writable([]);

// LED states: Map of note -> "on"|"off"|"blink"
export const ledStates = writable({});

const MAX_LOG = 100;
const MAX_TICKER = 20;

function pushTicker(entry) {
  catTicker.update(log => {
    const next = [entry, ...log];
    return next.length > MAX_TICKER ? next.slice(0, MAX_TICKER) : next;
  });
}

function handleWsMessage(msg) {
  switch (msg.type) {
    case 'ws':
      wsConnected.set(msg.connected);
      break;
    case 'status':
      status.update(s => ({ ...s, ...msg }));
      break;
    case 'control':
      controlLog.update(log => {
        const entry = { ...msg, ts: Date.now() };
        const next = [entry, ...log];
        return next.length > MAX_LOG ? next.slice(0, MAX_LOG) : next;
      });
      break;
    case 'cat':
      catLog.update(log => {
        const entry = { ...msg, ts: Date.now() };
        const next = [entry, ...log];
        return next.length > MAX_LOG ? next.slice(0, MAX_LOG) : next;
      });
      // Also push to ticker as TX
      pushTicker({ dir: 'TX', cat: msg.cat, ts: Date.now() });
      break;
    case 'cat_rx':
      // CAT response from Thetis
      pushTicker({ dir: 'RX', cat: `${msg.cmd}${msg.value}`, ts: Date.now() });
      break;
    case 'led':
      ledStates.update(s => ({ ...s, [msg.note]: msg.state }));
      break;
    case 'led_all_off':
      ledStates.set({});
      break;
  }
}

export function initStores() {
  wsSub(handleWsMessage);
  wsConnect();
}
