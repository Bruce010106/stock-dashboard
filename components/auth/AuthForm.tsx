'use client';

import Link from 'next/link';
import { useActionState } from 'react';

import {
  forgotPasswordAction,
  signInAction,
  signUpAction,
} from '../../app/auth/actions';
import type { AuthActionState } from '../../app/auth/actions';
import { getSupabaseConfigIssue } from '../../lib/supabase/config';
import { authNextQuery } from '../../lib/supabase/redirect';
import AuthShell from './AuthShell';
import styles from './auth.module.css';

export type AuthFormMode = 'sign-in' | 'sign-up' | 'forgot-password';

type AuthFormProps = {
  mode: AuthFormMode;
  nextPath?: string;
  initialError?: string;
};

const copy: Record<AuthFormMode, {
  eyebrow: string;
  title: string;
  subtitle: string;
  submit: string;
}> = {
  'sign-in': {
    eyebrow: 'ACCOUNT / SIGN IN',
    title: '登录知衡 Quant',
    subtitle: '登录后，你的自选股和持仓会在不同设备之间同步。',
    submit: '登录',
  },
  'sign-up': {
    eyebrow: 'ACCOUNT / SIGN UP',
    title: '创建账户',
    subtitle: '注册后即可把当前浏览器里的自选股和持仓同步到云端。',
    submit: '注册账户',
  },
  'forgot-password': {
    eyebrow: 'ACCOUNT / RECOVERY',
    title: '找回密码',
    subtitle: '输入注册邮箱，我们会发送一封密码重置邮件。',
    submit: '发送重置邮件',
  },
};

const initialAuthActionState: AuthActionState = {};

export default function AuthForm({ mode, nextPath = '/portfolio', initialError }: AuthFormProps) {
  const action = mode === 'sign-in'
    ? signInAction
    : mode === 'sign-up'
      ? signUpAction
      : forgotPasswordAction;
  const [state, formAction, pending] = useActionState(action, initialAuthActionState);
  const configIssue = getSupabaseConfigIssue();
  const strings = copy[mode];
  const nextQuery = authNextQuery(nextPath);

  return (
    <AuthShell eyebrow={strings.eyebrow} title={strings.title} subtitle={strings.subtitle}>
      {configIssue ? (
        <div className={styles.configNotice} role="status">
          <strong>暂时无法启用登录</strong>
          <span>{configIssue}</span>
        </div>
      ) : null}
      {initialError ? <p className={styles.error} role="alert">{initialError}</p> : null}
      {state.error ? <p className={styles.error} role="alert">{state.error}</p> : null}
      {state.message ? <p className={styles.message} role="status">{state.message}</p> : null}

      <form className={styles.form} action={formAction}>
        <input type="hidden" name="next" value={nextPath} />
        <div className={styles.field}>
          <label htmlFor="auth-email">邮箱</label>
          <input
            id="auth-email"
            name="email"
            type="email"
            autoComplete="email"
            required
            placeholder="name@example.com"
          />
        </div>
        {mode !== 'forgot-password' ? (
          <div className={styles.field}>
            <label htmlFor="auth-password">密码</label>
            <input
              id="auth-password"
              name="password"
              type="password"
              autoComplete={mode === 'sign-up' ? 'new-password' : 'current-password'}
              minLength={6}
              maxLength={128}
              required
              placeholder="至少 6 位"
            />
          </div>
        ) : null}
        {mode === 'sign-up' ? (
          <div className={styles.field}>
            <label htmlFor="auth-password-confirmation">确认密码</label>
            <input
              id="auth-password-confirmation"
              name="passwordConfirmation"
              type="password"
              autoComplete="new-password"
              minLength={6}
              maxLength={128}
              required
              placeholder="再次输入密码"
            />
          </div>
        ) : null}
        <button type="submit" disabled={pending || Boolean(configIssue)}>
          {pending ? '处理中…' : strings.submit}
        </button>
      </form>

      <nav className={styles.links} aria-label="账户操作">
        {mode === 'sign-in' ? (
          <>
            <Link href={`/auth/register?next=${nextQuery}`}>创建账户</Link>
            <Link href={`/auth/forgot-password?next=${nextQuery}`}>忘记密码？</Link>
          </>
        ) : null}
        {mode === 'sign-up' ? <Link href={`/auth/login?next=${nextQuery}`}>已有账户，去登录</Link> : null}
        {mode === 'forgot-password' ? <Link href={`/auth/login?next=${nextQuery}`}>返回登录</Link> : null}
      </nav>
    </AuthShell>
  );
}
