let socket = null;
let listeners = [];
let reconnectTimer = null;

function getWsUrl() {
  const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
  return `${proto}//${location.host}/ws`;
}

export function connect() {
  if (socket && socket.readyState <= 1) return;

  socket = new WebSocket(getWsUrl());

  socket.onopen = () => {
    if (reconnectTimer) { clearTimeout(reconnectTimer); reconnectTimer = null; }
    notify({ type: 'ws', connected: true });
  };

  socket.onmessage = (ev) => {
    try {
      const msg = JSON.parse(ev.data);
      notify(msg);
    } catch (_) { /* ignore non-JSON */ }
  };

  socket.onclose = () => {
    notify({ type: 'ws', connected: false });
    scheduleReconnect();
  };

  socket.onerror = () => {
    socket.close();
  };
}

function scheduleReconnect() {
  if (!reconnectTimer) {
    reconnectTimer = setTimeout(() => { reconnectTimer = null; connect(); }, 3000);
  }
}

function notify(msg) {
  for (const fn of listeners) fn(msg);
}

export function subscribe(fn) {
  listeners.push(fn);
  return () => { listeners = listeners.filter(l => l !== fn); };
}

export function disconnect() {
  if (reconnectTimer) { clearTimeout(reconnectTimer); reconnectTimer = null; }
  if (socket) { socket.close(); socket = null; }
}
