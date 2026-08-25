import { createServerClient } from '@supabase/ssr';
import { cookies } from 'next/headers';

import { getSupabaseConfig } from './config';

type ServerSupabaseClient = ReturnType<typeof createServerClient>;

/**
 * Creates a request-scoped server client. `null` means Supabase is not
 * configured; callers should keep public functionality available in that case.
 */
export async function getSupabaseServerClient(): Promise<ServerSupabaseClient | null> {
  const config = getSupabaseConfig();
  if (!config) return null;

  const cookieStore = await cookies();

  return createServerClient(config.url, config.anonKey, {
    cookies: {
      getAll() {
        return cookieStore.getAll();
      },
      setAll(cookiesToSet) {
        // Server Components cannot always mutate cookies. Route handlers and
        // server actions can; the try/catch keeps read-only render paths safe.
        try {
          for (const { name, value, options } of cookiesToSet) {
            cookieStore.set(name, value, options);
          }
        } catch {
          // The proxy refreshes the session cookie before rendering.
        }
      },
    },
  });
}

export const createClient = getSupabaseServerClient;

export async function getSupabaseUser() {
  const client = await getSupabaseServerClient();
  if (!client) return null;

  const { data, error } = await client.auth.getUser();
  if (error) return null;
  return data.user;
}
