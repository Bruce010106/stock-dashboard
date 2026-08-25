import { createServerClient } from '@supabase/ssr';
import { NextRequest, NextResponse } from 'next/server';

import { getSupabaseConfig } from './config';

/**
 * Refreshes the Supabase auth token in the Next.js 16 proxy. It is deliberately
 * a no-op when Supabase is not configured, so public pages remain deployable.
 */
export async function updateSupabaseSession(request: NextRequest): Promise<NextResponse> {
  const config = getSupabaseConfig();
  if (!config) return NextResponse.next();

  let response = NextResponse.next({ request });
  const client = createServerClient(config.url, config.anonKey, {
    cookies: {
      getAll() {
        return request.cookies.getAll();
      },
      setAll(cookiesToSet) {
        for (const { name, value } of cookiesToSet) {
          request.cookies.set(name, value);
        }

        response = NextResponse.next({ request });
        for (const { name, value, options } of cookiesToSet) {
          response.cookies.set(name, value, options);
        }
      },
    },
  });

  // `getUser` validates the token with Supabase and causes @supabase/ssr to
  // refresh an expired session through the cookie adapter above.
  await client.auth.getUser();
  return response;
}
