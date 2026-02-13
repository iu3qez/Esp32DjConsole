import { writable } from 'svelte/store';

export const toasts = writable([]);

let nextId = 0;

export function toast(message, type = 'info', duration = 3000) {
  const id = nextId++;
  toasts.update(t => [...t, { id, message, type, duration }]);
  return id;
}

export function dismissToast(id) {
  toasts.update(t => t.filter(x => x.id !== id));
}

export function success(message) { return toast(message, 'success'); }
export function error(message) { return toast(message, 'error', 5000); }
