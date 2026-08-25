'use client';

import Link from 'next/link';
import { useEffect, useState } from 'react';
import type { DailyMarketBar, MarketSnapshot } from '../../lib/data/market-data-provider.ts';
import type { RuleCheck, YangYongxingResult } from '../../lib/strategies/yang-yongxing.ts';
import { StockKlineChart } from './StockKlineChart.tsx';
import AuthStatus from '../auth/AuthStatus.tsx';
import styles from './stock-detail.module.css';

type StockDetailResponse = {
  code: string;
  name: string;
  exchange?: 'SH' | 'SZ' | 'BJ';
  generatedAt: string;
  source: string;
  historyMode: 'tushare' | 'tencent-fallback';
  isFallback: boolean;
  warnings: string[];
  snapshot?: MarketSnapshot;
  bars: DailyMarketBar[];
  evaluation?: YangYongxingResult;
};

function signed(value: number): string {
  return `${value >= 0 ? '+' : ''}${value.toFixed(2)}%`;
}

function RuleList({ checks }: { checks: RuleCheck[] }) {
  return <div className={styles.rules}>{checks.map((check) => <div key={check.key} className={styles.rule}><span className={check.passed ? styles.pass : styles.fail}>{check.passed ? '通过' : '未过'}</span><div><strong>{check.label}</strong><p>{check.actual}</p><small>要求：{check.expected}</small></div></div>)}</div>;
}

export function StockDetailClient({ code }: { code: string }) {
  const [data, setData] = useState<StockDetailResponse | null>(null);
  const [error, setError] = useState('');

  useEffect(() => {
    const controller = new AbortController();
    fetch(`/api/stocks/${encodeURIComponent(code)}`, { signal: controller.signal })
      .then(async (response) => {
        const payload = await response.json();
        if (!response.ok) throw new Error(payload.error ?? '股票详情读取失败');
        return payload as StockDetailResponse;
      })
      .then(setData)
      .catch((cause) => {
        if (cause instanceof DOMException && cause.name === 'AbortError') return;
        setError(cause instanceof Error ? cause.message : '股票详情读取失败');
      });
    return () => controller.abort();
  }, [code]);

  const latest = data?.snapshot;
  const changePct = latest ? (latest.lastPrice / latest.previousClose - 1) * 100 : undefined;
  const signalDate = data?.evaluation?.passed ? latest?.timestamp.slice(0, 10) : undefined;

  return (
    <main className={styles.shell}>
      <aside className={styles.sidebar}>
        <Link className={styles.brand} href="/">知衡 <span>Quant</span></Link>
        <nav><Link href="/">策略选股</Link><Link href="/backtest">策略回测</Link><Link href="/data">数据中心</Link><Link href="/portfolio">自选与持仓</Link></nav>
        <div className={styles.sideFoot}>真实行情 · 服务端密钥</div>
      </aside>
      <div className={styles.content}>
        <header className={styles.header}>
          <div><p>股票详情 / DAILY MARKET</p><h1>{data ? `${data.name} ${data.code}` : code}</h1></div>
          <div className={styles.headerActions}><AuthStatus nextPath={`/stocks/${code}`} /><Link href="/">返回选股 →</Link></div>
        </header>

        {error ? <section className={styles.state}><strong>暂时无法读取详情</strong><p>{error}</p></section> : null}
        {!data && !error ? <section className={styles.state}>正在读取真实行情…</section> : null}

        {data ? <>
          <section className={styles.metrics}>
            <div><span>最新价</span><strong>{latest ? latest.lastPrice.toFixed(2) : data.bars.at(-1)?.close.toFixed(2) ?? '—'}</strong><small className={changePct == null ? '' : changePct >= 0 ? styles.positive : styles.negative}>{changePct == null ? '日线收盘口径' : signed(changePct)}</small></div>
            <div><span>量比</span><strong>{latest?.volumeRatio.toFixed(2) ?? '—'}</strong><small>实时快照</small></div>
            <div><span>换手率</span><strong>{latest ? `${latest.turnoverRatePct.toFixed(2)}%` : '—'}</strong><small>实时快照</small></div>
            <div><span>总市值</span><strong>{latest ? `${(latest.totalMarketCapYuan / 100_000_000).toFixed(1)}亿` : '—'}</strong><small>{data.exchange ?? 'A股'}</small></div>
          </section>

          {data.warnings.length ? <div className={styles.warning}>{data.warnings.join('；')}</div> : null}

          <section className={styles.panel}>
            <div className={styles.panelHead}><div><p>PRICE ACTION</p><h2>日K · 均线 · 策略标记</h2></div><span>{data.historyMode === 'tushare' ? 'TUSHARE 历史口径' : '腾讯降级口径'}</span></div>
            <StockKlineChart bars={data.bars} signalDate={signalDate} />
          </section>

          <section className={styles.audit}>
            <div className={styles.panelHead}><div><p>STRATEGY AUDIT</p><h2>杨永兴尾盘战法 · 当前核验</h2></div><span className={data.evaluation?.passed ? styles.signalPass : styles.signalWait}>{data.evaluation ? data.evaluation.passed ? '严格命中' : `${data.evaluation.score} 分` : '无实时快照'}</span></div>
            {data.evaluation ? <RuleList checks={data.evaluation.checks} /> : <div className={styles.state}>当前没有可用于策略核验的实时快照。</div>}
          </section>
          <p className={styles.disclaimer}>涨停圆点来自历史日线识别；三角标记仅在当前六项规则全部通过时显示。数据与策略结果不构成投资建议。</p>
        </> : null}
      </div>
    </main>
  );
}
