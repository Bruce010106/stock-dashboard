import Link from 'next/link';
import type { ReactNode } from 'react';

import styles from './auth.module.css';

type AuthShellProps = {
  eyebrow: string;
  title: string;
  subtitle: string;
  children: ReactNode;
};

export default function AuthShell({ eyebrow, title, subtitle, children }: AuthShellProps) {
  return (
    <main className={styles.page}>
      <div className={styles.shell}>
        <Link className={styles.brand} href="/" aria-label="返回知衡 Quant 首页">
          <span className={styles.brandMark}>知</span>
          <span>知衡 Quant</span>
        </Link>
        <section className={styles.card}>
          <p className={styles.eyebrow}>{eyebrow}</p>
          <h1 className={styles.title}>{title}</h1>
          <p className={styles.subtitle}>{subtitle}</p>
          {children}
        </section>
        <footer className={styles.footer}>
          <span>账户数据仅用于同步你的自选和持仓</span>
          <Link href="/">返回首页</Link>
        </footer>
      </div>
    </main>
  );
}
