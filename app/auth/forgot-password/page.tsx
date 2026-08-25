import type { Metadata } from 'next';

import AuthForm from '../../../components/auth/AuthForm';
import { safeAuthNextPath } from '../../../lib/supabase/redirect';

export const metadata: Metadata = {
  title: '找回密码｜知衡 Quant',
  description: '通过注册邮箱重置知衡 Quant 登录密码。',
};

type ForgotPasswordPageProps = {
  searchParams: Promise<Record<string, string | string[] | undefined>>;
};

export default async function ForgotPasswordPage({ searchParams }: ForgotPasswordPageProps) {
  const params = await searchParams;
  const nextPath = safeAuthNextPath(typeof params.next === 'string' ? params.next : undefined);
  return <AuthForm mode="forgot-password" nextPath={nextPath} />;
}
