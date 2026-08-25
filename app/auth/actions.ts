'use server';

import { headers } from 'next/headers';
import { redirect } from 'next/navigation';

import {
  getSupabaseConfigIssue,
  SUPABASE_CONFIG_MESSAGE,
} from '../../lib/supabase/config';
import { safeAuthNextPath } from '../../lib/supabase/redirect';
import { getSupabaseServerClient } from '../../lib/supabase/server';

export type AuthActionState = {
  error?: string;
  message?: string;
};

function formValue(formData: FormData, key: string): string {
  const value = formData.get(key);
  return typeof value === 'string' ? value.trim() : '';
}

function emailError(email: string): string | null {
  if (!email) return '请输入邮箱地址。';
  if (email.length > 320 || !/^\S+@\S+\.\S+$/.test(email)) {
    return '请输入有效的邮箱地址。';
  }
  return null;
}

function passwordError(password: string, confirmation?: string): string | null {
  if (!password) return '请输入密码。';
  if (password.length < 6) return '密码至少需要 6 位。';
  if (password.length > 128) return '密码不能超过 128 位。';
  if (confirmation !== undefined && password !== confirmation) {
    return '两次输入的密码不一致。';
  }
  return null;
}

function translateAuthError(message: string): string {
  const normalized = message.toLowerCase();
  if (normalized.includes('invalid login credentials')) return '邮箱或密码不正确。';
  if (normalized.includes('email not confirmed')) return '邮箱尚未验证，请先查收验证邮件。';
  if (normalized.includes('user already registered')) return '这个邮箱已经注册，请直接登录。';
  if (normalized.includes('password should be at least')) return '密码至少需要 6 位。';
  if (normalized.includes('rate limit')) return '操作过于频繁，请稍后再试。';
  if (normalized.includes('email address') && normalized.includes('invalid')) {
    return '请输入有效的邮箱地址。';
  }
  return message || '认证服务暂时不可用，请稍后再试。';
}

function configurationState(): AuthActionState | null {
  const issue = getSupabaseConfigIssue();
  return issue ? { error: issue } : null;
}

function originFromHeaders(headerStore: Headers): string {
  const configured = process.env.NEXT_PUBLIC_SITE_URL?.trim();
  if (configured) {
    try {
      const parsed = new URL(configured);
      if (parsed.protocol === 'http:' || parsed.protocol === 'https:') {
        return parsed.origin;
      }
    } catch {
      // Fall through to the current request origin.
    }
  }

  const host = headerStore.get('x-forwarded-host') ?? headerStore.get('host');
  if (!host) return 'http://localhost:3000';
  const protocol = headerStore.get('x-forwarded-proto') ?? 'http';
  return `${protocol}://${host}`;
}

function callbackUrl(origin: string, nextPath: string): string {
  return `${origin}/auth/callback?next=${encodeURIComponent(nextPath)}`;
}

export async function signInAction(
  _previousState: AuthActionState,
  formData: FormData,
): Promise<AuthActionState> {
  const configured = configurationState();
  if (configured) return configured;

  const email = formValue(formData, 'email');
  const password = formData.get('password');
  const passwordValue = typeof password === 'string' ? password : '';
  const nextPath = safeAuthNextPath(formValue(formData, 'next'));

  const emailIssue = emailError(email);
  if (emailIssue) return { error: emailIssue };
  const passwordIssue = passwordError(passwordValue);
  if (passwordIssue) return { error: passwordIssue };

  const client = await getSupabaseServerClient();
  if (!client) return { error: SUPABASE_CONFIG_MESSAGE };

  const { error } = await client.auth.signInWithPassword({
    email,
    password: passwordValue,
  });
  if (error) return { error: translateAuthError(error.message) };

  redirect(nextPath);
}

export async function signUpAction(
  _previousState: AuthActionState,
  formData: FormData,
): Promise<AuthActionState> {
  const configured = configurationState();
  if (configured) return configured;

  const email = formValue(formData, 'email');
  const password = formData.get('password');
  const confirmation = formData.get('passwordConfirmation');
  const passwordValue = typeof password === 'string' ? password : '';
  const confirmationValue = typeof confirmation === 'string' ? confirmation : '';
  const nextPath = safeAuthNextPath(formValue(formData, 'next'));

  const emailIssue = emailError(email);
  if (emailIssue) return { error: emailIssue };
  const passwordIssue = passwordError(passwordValue, confirmationValue);
  if (passwordIssue) return { error: passwordIssue };

  const client = await getSupabaseServerClient();
  if (!client) return { error: SUPABASE_CONFIG_MESSAGE };

  const headerStore = await headers();
  const { data, error } = await client.auth.signUp({
    email,
    password: passwordValue,
    options: {
      emailRedirectTo: callbackUrl(originFromHeaders(headerStore), nextPath),
    },
  });
  if (error) return { error: translateAuthError(error.message) };

  if (data.session) redirect(nextPath);

  return {
    message: '注册成功。请查收验证邮件，完成邮箱验证后再登录。',
  };
}

export async function forgotPasswordAction(
  _previousState: AuthActionState,
  formData: FormData,
): Promise<AuthActionState> {
  const configured = configurationState();
  if (configured) return configured;

  const email = formValue(formData, 'email');
  const emailIssue = emailError(email);
  if (emailIssue) return { error: emailIssue };

  const client = await getSupabaseServerClient();
  if (!client) return { error: SUPABASE_CONFIG_MESSAGE };

  const headerStore = await headers();
  const { error } = await client.auth.resetPasswordForEmail(email, {
    redirectTo: callbackUrl(originFromHeaders(headerStore), '/auth/reset-password'),
  });
  if (error) return { error: translateAuthError(error.message) };

  // Keep the response deliberately generic so the form does not reveal
  // whether an address is registered.
  return { message: '如果该邮箱已注册，密码重置邮件会很快送达，请查收。' };
}

export async function signOutAction(formData?: FormData): Promise<void> {
  const configured = configurationState();
  if (configured) redirect('/');

  const client = await getSupabaseServerClient();
  if (client) await client.auth.signOut();

  const nextPath = formData ? safeAuthNextPath(formValue(formData, 'next'), '/') : '/';
  redirect(nextPath);
}
