import { NextRequest } from 'next/server';

import { updateSupabaseSession } from './lib/supabase/session';

export async function proxy(request: NextRequest) {
  return updateSupabaseSession(request);
}

export default proxy;

export const config = {
  matcher: [
    '/((?!_next/static|_next/image|favicon.ico|.*\\.(?:svg|png|jpg|jpeg|gif|webp|ico|css|js|map|txt|xml)$).*)',
  ],
};
