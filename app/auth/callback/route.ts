import { NextRequest, NextResponse } from 'next/server';

import { getSupabaseConfig, getSupabaseConfigIssue } from '../../../lib/supabase/config';
import { safeAuthNextPath } from '../../../lib/supabase/redirect';
import { getSupabaseServerClient } from '../../../lib/supabase/server';

function loginError(request: NextRequest, message: string): NextResponse {
  const url = new URL('/auth/login', request.url);
  url.searchParams.set('error', message);
  return NextResponse.redirect(url);
}

export async function GET(request: NextRequest) {
  const config = getSupabaseConfig();
  if (!config) {
    return loginError(request, getSupabaseConfigIssue() ?? '登录功能暂时不可用。');
  }

  const code = request.nextUrl.searchParams.get('code');
  const errorDescription = request.nextUrl.searchParams.get('error_description');
  const nextPath = safeAuthNextPath(request.nextUrl.searchParams.get('next'));

  if (errorDescription) return loginError(request, errorDescription);
  if (!code) return loginError(request, '验证链接无效或已过期，请重新申请。');

  const client = await getSupabaseServerClient();
  if (!client) return loginError(request, '登录功能暂时不可用，请稍后再试。');

  const { error } = await client.auth.exchangeCodeForSession(code);
  if (error) return loginError(request, error.message || '验证链接无效或已过期，请重新申请。');

  return NextResponse.redirect(new URL(nextPath, request.url));
}
