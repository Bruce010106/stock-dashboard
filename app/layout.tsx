import type { Metadata } from 'next';
import { Geist, Geist_Mono } from 'next/font/google';
import './globals.css';

const siteUrl = process.env.NEXT_PUBLIC_SITE_URL ??
  (process.env.VERCEL_PROJECT_PRODUCTION_URL
    ? `https://${process.env.VERCEL_PROJECT_PRODUCTION_URL}`
    : 'http://localhost:3000');

const geistSans = Geist({
  variable: '--font-geist-sans',
  subsets: ['latin'],
});

const geistMono = Geist_Mono({
  variable: '--font-geist-mono',
  subsets: ['latin'],
});

export const metadata: Metadata = {
  metadataBase: new URL(siteUrl),
  title: '知衡 Quant｜A 股量化选股与回测',
  description: '基于真实 A 股市场数据的策略选股与回测平台。',
  openGraph: {
    title: '知衡 Quant｜杨永兴尾盘战法',
    description: '六项严格条件筛选尾盘强势 A 股，并以点时数据验证历史信号表现。',
  },
  twitter: {
    card: 'summary_large_image',
    title: '知衡 Quant｜杨永兴尾盘战法',
    description: '六项严格条件筛选尾盘强势 A 股，并以点时数据验证历史信号表现。',
  },
};

export default function RootLayout({
  children,
}: Readonly<{
  children: React.ReactNode;
}>) {
  return (
    <html lang="zh-CN">
      <body
        className={`${geistSans.variable} ${geistMono.variable} antialiased`}
      >
        {children}
      </body>
    </html>
  );
}
