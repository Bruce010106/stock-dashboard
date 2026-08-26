const BACKSLASH_PATTERN = /\\/;

function hasControlChar(input: string): boolean {
  for (let index = 0; index < input.length; index += 1) {
    const code = input.charCodeAt(index);
    if (code <= 0x1f || code === 0x7f) return true;
  }
  return false;
}

function decodeOnce(value: string): string | null {
  try {
    return decodeURIComponent(value);
  } catch {
    return null;
  }
}

/** A fixed, non-user-controlled base used only to ask the WHATWG URL parser
 * whether `value` could resolve to a different origin. */
const SAFE_BASE = 'http://same-site.invalid';

/**
 * Only allow local application paths as auth return targets.
 *
 * Rejects anything that could turn into a cross-origin redirect once a
 * browser or proxy parses it: protocol-relative values (`//host`), raw or
 * percent-encoded backslashes (browsers treat `\` like `/` in special-scheme
 * URLs, so `/\evil.example` and its decoded `%5C` form behave like
 * `//evil.example`), control characters (the URL spec strips tabs/newlines
 * wherever they occur, which can smuggle a `//` past a naive check), and any
 * other value that WHATWG URL parsing against a fixed same-site base would
 * resolve to a different origin.
 */
export function safeAuthNextPath(value: string | null | undefined, fallback = '/portfolio'): string {
  if (!value) return fallback;
  if (!value.startsWith('/') || value.startsWith('//')) return fallback;
  if (BACKSLASH_PATTERN.test(value) || hasControlChar(value)) return fallback;

  const decoded = decodeOnce(value);
  if (decoded === null) return fallback;
  if (BACKSLASH_PATTERN.test(decoded) || hasControlChar(decoded)) return fallback;
  if (decoded.startsWith('//')) return fallback;

  let parsed: URL;
  try {
    parsed = new URL(value, SAFE_BASE);
  } catch {
    return fallback;
  }
  if (parsed.origin !== SAFE_BASE) return fallback;

  return value;
}

export function authNextQuery(value: string | null | undefined, fallback = '/portfolio'): string {
  return encodeURIComponent(safeAuthNextPath(value, fallback));
}
