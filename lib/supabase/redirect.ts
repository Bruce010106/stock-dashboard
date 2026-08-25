/**
 * Only allow local application paths as auth return targets. In particular,
 * rejecting `//host` prevents an auth callback from becoming an open redirect.
 */
export function safeAuthNextPath(value: string | null | undefined, fallback = '/portfolio'): string {
  if (!value || !value.startsWith('/') || value.startsWith('//')) return fallback;
  return value;
}

export function authNextQuery(value: string | null | undefined, fallback = '/portfolio'): string {
  return encodeURIComponent(safeAuthNextPath(value, fallback));
}
