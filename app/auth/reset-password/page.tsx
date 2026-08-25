import type { Metadata } from 'next';

import ResetPasswordForm from '../../../components/auth/ResetPasswordForm';

export const metadata: Metadata = {
  title: '设置新密码｜知衡 Quant',
  description: '设置新的知衡 Quant 登录密码。',
};

export default function ResetPasswordPage() {
  return <ResetPasswordForm />;
}
