'use client';

import { useState } from 'react';
import Link from 'next/link';
import BacktestPerformance from '../../components/backtest/backtest-performance';

type BacktestSignal = {
  signalDate: string;
  code: string;
  name: string;
  signalPrice: number;
  exitPrice: number;
  returnPct: number;
  holdingTradingDays: number;
  evaluation: { intraday: { breakoutTime?: string } };
};

type BacktestResponse = {
  source: string;
  generatedAt: string;
  startDate: string;
  endDate: string;
  holdingTradingDays: number;
  totalSignals: number;
  completedSignals: number;
  averageReturnPct: number;
  medianReturnPct: number;
  winRatePct: number;
  bestReturnPct: number;
  worstReturnPct: number;
  evaluatedCodes: string[];
  scannedTradingDays: number;
  prefilteredDays: number;
  minuteDaysLoaded: number;
  warnings: string[];
  signals: BacktestSignal[];
  error?: string;
  code?: string;
};

function hongKongDate(date: Date): string {
  return new Intl.DateTimeFormat('en-CA', {
    timeZone: 'Asia/Hong_Kong',
    year: 'numeric',
    month: '2-digit',
    day: '2-digit',
  }).format(date);
}

function signedPercent(value: number): string {
  return `${value >= 0 ? '+' : ''}${value.toFixed(2)}%`;
}

const initialNow = new Date();
const initialEndDate = hongKongDate(initialNow);
const initialStartDate = hongKongDate(new Date(initialNow.getTime() - 59 * 86_400_000));

export default function BacktestPage() {
  const [codes, setCodes] = useState('002892,603211,001269');
  const [startDate, setStartDate] = useState(initialStartDate);
  const [endDate, setEndDate] = useState(initialEndDate);
  const [holdingDays, setHoldingDays] = useState<1 | 3 | 5 | 10>(5);
  const [isRunning, setIsRunning] = useState(false);
  const [result, setResult] = useState<BacktestResponse | null>(null);
  const [error, setError] = useState('');

  async function runBacktest() {
    setIsRunning(true);
    setError('');
    try {
      const params = new URLSearchParams({
        codes,
        startDate,
        endDate,
        holdingTradingDays: String(holdingDays),
      });
      const response = await fetch(`/api/backtest/yang-yongxing?${params}`, {
        cache: 'no-store',
      });
      const payload = await response.json() as BacktestResponse;
      if (!response.ok) throw new Error(payload.error ?? '真实历史回测失败');
      setResult(payload);
    } catch (cause) {
      setResult(null);
      setError(cause instanceof Error ? cause.message : '真实历史回测失败');
    } finally {
      setIsRunning(false);
    }
  }

  const hasResult = result !== null;

  return (
    <main className="app-shell">
      <aside className="sidebar">
        <div className="brand"><span className="brand-mark">量</span><span>知衡 Quant</span></div>
        <nav aria-label="主导航">
          <Link className="nav-item" href="/"><span>◈</span>策略选股</Link>
          <Link className="nav-item active" href="/backtest"><span>↗</span>策略回测</Link>
          <Link className="nav-item" href="/portfolio"><span>◎</span>自选与持仓</Link>
          <Link className="nav-item" href="/data"><span>⌘</span>数据中心</Link>
        </nav>
        <div className="sidebar-foot"><div className="data-status"><span className="status-dot" />Tushare 历史回测</div><p>点时指标 · 1 分钟线</p></div>
      </aside>
      <section className="workspace">
        <header className="topbar">
          <div><p className="eyebrow">策略回测 / 真实历史信号</p><h1>杨永兴尾盘战法</h1></div>
          <Link className="back-link" href="/">返回选股 →</Link>
        </header>

        <div className="backtest-grid">
          <section className="strategy-card backtest-config">
            <span className="pill">POINT-IN-TIME · LIVE DATA</span>
            <h2>真实回测参数</h2>
            <p className="section-copy">使用 Tushare 历史日线、每日点时指标和 1 分钟线重放信号；先用前五项条件粗筛，再按需读取候选日期分钟线。</p>
            <div className="field-grid">
              <label>股票代码（最多 5 只）
                <input value={codes} onChange={(event) => setCodes(event.target.value)} placeholder="例如 002892,603211" />
              </label>
              <label>持有交易日
                <select value={holdingDays} onChange={(event) => setHoldingDays(Number(event.target.value) as 1 | 3 | 5 | 10)}>
                  <option value={1}>1 个交易日</option><option value={3}>3 个交易日</option><option value={5}>5 个交易日</option><option value={10}>10 个交易日</option>
                </select>
              </label>
              <label>开始日期
                <input suppressHydrationWarning type="date" value={startDate} max={endDate || initialEndDate} onChange={(event) => setStartDate(event.target.value)} />
              </label>
              <label>结束日期
                <input suppressHydrationWarning type="date" value={endDate} min={startDate} max={initialEndDate} onChange={(event) => setEndDate(event.target.value)} />
              </label>
            </div>
            <div className="backtest-actions">
              <small>单次区间最多 90 个自然日；需要 TUSHARE_TOKEN 及 stk_mins 权限。</small>
              <button type="button" onClick={runBacktest} disabled={isRunning || !startDate || !endDate || !codes.trim()}>
                {isRunning ? '正在读取历史行情…' : '运行真实回测'} <span>→</span>
              </button>
            </div>
            {error ? <p className="inline-error" role="alert">{error}</p> : null}
            <div className="backtest-note"><strong>信号价格口径</strong><p>策略需要尾盘收盘仍站稳突破位，因此以信号日最后一分钟收盘价作为信号价，再观察第 N 个交易日收盘收益。</p></div>
          </section>
          <aside className="metric-stack">
            <div className="metric-card"><span>平均收益</span><strong className={hasResult ? result.averageReturnPct >= 0 ? 'positive' : 'negative' : ''}>{hasResult ? signedPercent(result.averageReturnPct) : '—'}</strong><small>{hasResult ? `完成 ${result.completedSignals}/${result.totalSignals} 个信号` : `持有 ${holdingDays} 日`}</small></div>
            <div className="metric-card"><span>信号胜率</span><strong>{hasResult ? `${result.winRatePct.toFixed(1)}%` : '—'}</strong><small>{hasResult ? `扫描 ${result.scannedTradingDays} 个股票交易日` : '等待运行'}</small></div>
            <div className="metric-card"><span>收益中位数</span><strong className={hasResult ? result.medianReturnPct >= 0 ? 'positive' : 'negative' : ''}>{hasResult ? signedPercent(result.medianReturnPct) : '—'}</strong><small>{hasResult ? `分钟复核 ${result.minuteDaysLoaded} 个候选日` : '点时数据口径'}</small></div>
          </aside>
        </div>

        {result ? <BacktestPerformance key={result.generatedAt} signals={result.signals} backtestEndDate={result.endDate} /> : null}

        <section className="results-card">
          <div className="results-head"><div><p className="eyebrow">信号明细</p><h2>{hasResult ? `严格信号 ${result.totalSignals} · 完整收益 ${result.completedSignals}` : '等待真实历史回测'}</h2></div><span className="sample-badge">{hasResult ? 'TUSHARE 真实数据' : '尚未运行'}</span></div>
          {result?.warnings.length ? <div className="source-note"><strong>口径说明</strong><span>{result.warnings.join('；')}</span></div> : null}
          <div className="table-wrap"><table><thead><tr><th>信号日期</th><th>股票</th><th>信号价</th><th>退出价</th><th>持有期</th><th>区间收益</th><th>突破时刻</th></tr></thead><tbody>
            {result?.signals.map((signal) => <tr key={`${signal.signalDate}-${signal.code}`}><td>{signal.signalDate}</td><td><Link href={`/stocks/${signal.code}`}><strong>{signal.name}</strong><small>{signal.code}</small></Link></td><td>¥{signal.signalPrice.toFixed(2)}</td><td>¥{signal.exitPrice.toFixed(2)}</td><td>{signal.holdingTradingDays} 日</td><td className={signal.returnPct >= 0 ? 'positive' : 'negative'}>{signedPercent(signal.returnPct)}</td><td>{signal.evaluation.intraday.breakoutTime ?? '—'}</td></tr>)}
            {result && result.signals.length === 0 ? <tr><td colSpan={7} className="empty-state">真实数据扫描完成，该区间没有形成可计算收益的严格信号</td></tr> : null}
            {!result ? <tr><td colSpan={7} className="empty-state">配置股票和日期后运行；结果不再使用演示样本</td></tr> : null}
          </tbody></table></div>
          <p className="disclaimer">数据来源：Tushare Pro 日线、daily_basic 点时指标与 stk_mins 历史分钟线。回测结果不构成投资建议。</p>
        </section>
      </section>
    </main>
  );
}
