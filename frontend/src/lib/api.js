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

export function getCommands() {
  return request('GET', '/api/commands');
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

export function clearMapping(controlName) {
  return request('POST', `/api/mappings/clear?c=${encodeURIComponent(controlName)}`);
}

export function downloadMappings() {
  // Trigger browser file download
  const a = document.createElement('a');
  a.href = '/api/mappings/download';
  a.download = 'mappings.json';
  a.click();
}

export async function uploadMappings(jsonText) {
  const res = await fetch('/api/mappings/upload', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: jsonText,
  });
  if (!res.ok) throw new Error(`Upload: ${res.status} ${res.statusText}`);
  return res.json();
}
