const BASE = '';

async function request(method, path, body) {
  const opts = {
    method,
    headers: { 'Content-Type': 'application/json' },
  };
  if (body !== undefined) opts.body = JSON.stringify(body);

  const res = await fetch(`${BASE}${path}`, opts);
  if (!res.ok) throw new Error(`${method} ${path}: ${res.status} ${res.statusText}`);
  return res.json();
}

export function getStatus() {
  return request('GET', '/api/status');
}

export function getConfig() {
  return request('GET', '/api/config');
}

export function putConfig(config) {
  return request('PUT', '/api/config', config);
}

export function getMappings() {
  return request('GET', '/api/mappings');
}

export function putMappings(mappings) {
  return request('PUT', '/api/mappings', mappings);
}

export function resetMappings() {
  return request('POST', '/api/mappings/reset');
}
