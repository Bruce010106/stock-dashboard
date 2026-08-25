'use client';

import { useEffect, useState } from 'react';
import { useRouter } from 'next/navigation';

import { getSupabaseConfigIssue } from '../../lib/supabase/config';
import { getSupabaseBrowserClient } from '../../lib/supabase/browser';
import AuthShell from './AuthShell';
import styles from './auth.module.css';

export default function ResetPasswordForm() {
  const router = useRouter();
  const client = getSupabaseBrowserClient();
  const configIssue = getSupabaseConfigIssue();
  const [ready, setReady] = useState(false);
  const [password, setPassword] = useState('');
  const [confirmation, setConfirmation] = useState('');
  const [pending, setPending] = useState(false);
  const [error, setError] = useState('');
  const [message, setMessage] = useState('');

  useEffect(() => {
    if (!client) return;
    let active = true;
    void client.auth.getUser().then(({ data }) => {
      if (active) setReady(Boolean(data.user));
    });
    const { data: subscription } = client.auth.onAuthStateChange((_event, session) => {
      if (active) setReady(Boolean(session?.user));
    });
    return () => {
      active = false;
      subscription.subscription.unsubscribe();
    };
  }, [client]);

  async function submit(event: React.FormEvent<HTMLFormElement>) {
    event.preventDefault();
    setError('');
    setMessage('');
    if (!client) {
      setError(configIssue ?? '登录功能暂时不可用。');
      return;
    }
    if (!ready) {
      setError('重置链接已失效或尚未完成验证，请重新申请重置邮件。');
      return;
    }
    if (password.length < 6) {
      setError('密码至少需要 6 位。');
      return;
    }
    if (password !== confirmation) {
      setError('两次输入的密码不一致。');
      return;
    }

    setPending(true);
    const { error: updateError } = await client.auth.updateUser({ password });
    setPending(false);
    if (updateError) {
      setError(updateError.message || '密码更新失败，请重新申请重置邮件。');
      return;
    }
    setMessage('密码已更新，正在返回组合管理…');
    window.setTimeout(() => router.push('/portfolio'), 700);
  }

  return (
    <AuthShell
      eyebrow="ACCOUNT / RESET PASSWORD"
      title="设置新密码"
      subtitle="为账户设置一个新的登录密码。"
    >
      {configIssue ? (
        <div className={styles.configNotice} role="status">
          <strong>暂时无法启用登录</strong>
          <span>{configIssue}</span>
        </div>
      ) : null}
      {error ? <p className={styles.error} role="alert">{error}</p> : null}
      {message ? <p className={styles.message} role="status">{message}</p> : null}
      <form className={styles.form} onSubmit={submit}>
        <div className={styles.field}>
          <label htmlFor="reset-password">新密码</label>
          <input
            id="reset-password"
            type="password"
            value={password}
            onChange={(event) => setPassword(event.target.value)}
            autoComplete="new-password"
            minLength={6}
            maxLength={128}
            required
            placeholder="至少 6 位"
          />
        </div>
        <div className={styles.field}>
          <label htmlFor="reset-password-confirmation">确认新密码</label>
          <input
            id="reset-password-confirmation"
            type="password"
            value={confirmation}
            onChange={(event) => setConfirmation(event.target.value)}
            autoComplete="new-password"
            minLength={6}
            maxLength={128}
            required
            placeholder="再次输入密码"
          />
        </div>
        <button type="submit" disabled={pending || Boolean(configIssue)}>
          {pending ? '更新中…' : '更新密码'}
        </button>
      </form>
      <nav className={styles.links} aria-label="账户操作">
        <a href="/auth/login">返回登录</a>
      </nav>
    </AuthShell>
  );
}
