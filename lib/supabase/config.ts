/**
 * Supabase configuration helpers.
 *
 * Environment variables are intentionally read inside functions rather than at
 * module scope. This keeps public pages and builds usable before Supabase has
 * been provisioned.
 */

export const SUPABASE_CONFIG_MESSAGE =
  '登录功能尚未配置，请先设置 NEXT_PUBLIC_SUPABASE_URL 和 NEXT_PUBLIC_SUPABASE_PUBLISHABLE_KEY（兼容旧变量 NEXT_PUBLIC_SUPABASE_ANON_KEY）。';

export type SupabaseConfig = {
  url: string;
  anonKey: string;
};

function readPublicUrl(): string {
  // Next.js only exposes NEXT_PUBLIC values to the browser when their names
  // are statically visible to the compiler; computed process.env access is
  // intentionally unsupported in client bundles.
  return process.env.NEXT_PUBLIC_SUPABASE_URL?.trim() ?? '';
}

function readPublicKey(): string {
  // Supabase now documents the publishable key name. Keep the anon-key
  // fallback so existing deployments can upgrade without a hard cutover.
  return process.env.NEXT_PUBLIC_SUPABASE_PUBLISHABLE_KEY?.trim()
    || process.env.NEXT_PUBLIC_SUPABASE_ANON_KEY?.trim()
    || '';
}

/** Returns the configuration only when both public values are valid. */
export function getSupabaseConfig(): SupabaseConfig | null {
  const url = readPublicUrl();
  const anonKey = readPublicKey();

  if (!url || !anonKey) return null;

  try {
    const parsed = new URL(url);
    if (parsed.protocol !== 'https:' && parsed.protocol !== 'http:') return null;
  } catch {
    return null;
  }

  return { url, anonKey };
}

/** A user-facing explanation for configuration failures. */
export function getSupabaseConfigIssue(): string | null {
  const url = readPublicUrl();
  const anonKey = readPublicKey();

  if (!url || !anonKey) return SUPABASE_CONFIG_MESSAGE;

  try {
    const parsed = new URL(url);
    if (parsed.protocol !== 'https:' && parsed.protocol !== 'http:') {
      return '登录功能配置有误：NEXT_PUBLIC_SUPABASE_URL 必须是 http(s) 地址。';
    }
  } catch {
    return '登录功能配置有误：NEXT_PUBLIC_SUPABASE_URL 不是有效地址。';
  }

  return null;
}

export function isSupabaseConfigured(): boolean {
  return getSupabaseConfig() !== null;
}
