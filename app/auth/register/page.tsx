import type { Metadata } from 'next';

import AuthForm from '../../../components/auth/AuthForm';
import { safeAuthNextPath } from '../../../lib/supabase/redirect';

export const metadata: Metadata = {
  title: '注册｜知衡 Quant',
  description: '创建知衡 Quant 账户，同步你的自选股和持仓。',
};

type RegisterPageProps = {
  searchParams: Promise<Record<string, string | string[] | undefined>>;
};

export default async function RegisterPage({ searchParams }: RegisterPageProps) {
  const params = await searchParams;
  const nextPath = safeAuthNextPath(typeof params.next === 'string' ? params.next : undefined);
  return <AuthForm mode="sign-up" nextPath={nextPath} />;
}
