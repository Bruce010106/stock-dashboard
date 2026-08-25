import type { Metadata } from 'next';

import AuthForm from '../../../components/auth/AuthForm';
import { safeAuthNextPath } from '../../../lib/supabase/redirect';

export const metadata: Metadata = {
  title: '登录｜知衡 Quant',
  description: '登录知衡 Quant，同步你的自选股和持仓。',
};

type LoginPageProps = {
  searchParams: Promise<Record<string, string | string[] | undefined>>;
};

function queryValue(value: string | string[] | undefined): string | undefined {
  return typeof value === 'string' ? value : undefined;
}

export default async function LoginPage({ searchParams }: LoginPageProps) {
  const params = await searchParams;
  const nextPath = safeAuthNextPath(queryValue(params.next));
  const initialError = queryValue(params.error);
  return <AuthForm mode="sign-in" nextPath={nextPath} initialError={initialError} />;
}
