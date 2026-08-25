'use client';

import Link from 'next/link';
import { useEffect, useState } from 'react';

import { signOutAction } from '../../app/auth/actions';
import { getSupabaseConfigIssue } from '../../lib/supabase/config';
import { getSupabaseBrowserClient } from '../../lib/supabase/browser';
import styles from './auth.module.css';

type AuthStatusProps = {
  nextPath?: string;
  className?: string;
};

/** Compact, drop-in account status control for page headers and navigation. */
export default function AuthStatus({ nextPath = '/portfolio', className }: AuthStatusProps) {
  const client = getSupabaseBrowserClient();
  const [email, setEmail] = useState<string | null>(null);
  const [loaded, setLoaded] = useState(() => client === null);
  const configIssue = getSupabaseConfigIssue();

  useEffect(() => {
    if (!client) {
      return;
    }
    let active = true;
    void client.auth.getUser().then(({ data }) => {
      if (!active) return;
      setEmail(data.user?.email ?? null);
      setLoaded(true);
    });
    const { data: subscription } = client.auth.onAuthStateChange((_event, session) => {
      if (!active) return;
      setEmail(session?.user?.email ?? null);
      setLoaded(true);
    });
    return () => {
      active = false;
      subscription.subscription.unsubscribe();
    };
  }, [client]);

  if (!loaded) return <span className={`${styles.status} ${className ?? ''}`}>账户状态读取中…</span>;
  if (configIssue) {
    return (
      <span className={`${styles.status} ${className ?? ''}`} title={configIssue}>
        <i className={styles.statusDot} aria-hidden="true" />
        <span>登录待配置</span>
      </span>
    );
  }
  if (!email) {
    return (
      <span className={`${styles.status} ${className ?? ''}`}>
        <i className={styles.statusDot} aria-hidden="true" />
        <Link href={`/auth/login?next=${encodeURIComponent(nextPath)}`}>登录 / 注册</Link>
      </span>
    );
  }

  return (
    <span className={`${styles.status} ${className ?? ''}`}>
      <i className={`${styles.statusDot} ${styles.statusDotReady}`} aria-hidden="true" />
      <strong title={email}>{email}</strong>
      <form action={signOutAction}>
        <input type="hidden" name="next" value="/" />
        <button type="submit">退出</button>
      </form>
    </span>
  );
}
