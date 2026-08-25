'use client';

import { createBrowserClient } from '@supabase/ssr';
import type { SupabaseClient } from '@supabase/supabase-js';

import { getSupabaseConfig } from './config';

type BrowserSupabaseClient = SupabaseClient;

let browserClient: BrowserSupabaseClient | null | undefined;

/**
 * Creates one browser client per tab. A null result is expected when the
 * optional Supabase environment variables have not been added yet.
 */
export function getSupabaseBrowserClient(): BrowserSupabaseClient | null {
  if (browserClient !== undefined) return browserClient;

  const config = getSupabaseConfig();
  if (!config) {
    browserClient = null;
    return browserClient;
  }

  browserClient = createBrowserClient(config.url, config.anonKey);
  return browserClient;
}

export function resetSupabaseBrowserClient(): void {
  browserClient = undefined;
}

// Keep the conventional name available for consumers that use a dedicated
// browser Supabase module.
export const createClient = getSupabaseBrowserClient;
