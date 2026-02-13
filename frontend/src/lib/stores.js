import { writable } from 'svelte/store';
import { subscribe as wsSub, connect as wsConnect } from './ws.js';

// Connection state
export const wsConnected = writable(false);

// System status (from GET /api/status and WS "status" messages)
export const status = writable({
  usb_connected: false,
  tci_state: 'disconnected',
  cat_state: 'disconnected',
  free_heap: 0,
  radio: null,
});

// Live control events (ring buffer of last 100)
export const controlLog = writable([]);

// Radio state (from WS "radio" messages)
export const radio = writable({
  vfo_a: 0, vfo_b: 0, mode: '', drive: 0,
  tx: false, mute: false, filter_low: 0, filter_high: 0,
});

const MAX_LOG = 100;

function handleWsMessage(msg) {
  switch (msg.type) {
    case 'ws':
      wsConnected.set(msg.connected);
      break;
    case 'status':
      status.update(s => ({ ...s, ...msg }));
      break;
    case 'radio':
      radio.set(msg);
      break;
    case 'control':
      controlLog.update(log => {
        const entry = { ...msg, ts: Date.now() };
        const next = [entry, ...log];
        return next.length > MAX_LOG ? next.slice(0, MAX_LOG) : next;
      });
      break;
  }
}

export function initStores() {
  wsSub(handleWsMessage);
  wsConnect();
}
