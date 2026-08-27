'use client';

import { useEffect, useState } from 'react';
import Link from 'next/link';
import AuthStatus from '../../components/auth/AuthStatus';
import BacktestPerformance from '../../components/backtest/backtest-performance';
import {
  MAX_LIVE_BACKTEST_CALENDAR_DAYS_FREE,
  MAX_LIVE_BACKTEST_CALENDAR_DAYS_TUSHARE,
} from '../../lib/api-validation';
import {
  backtestSourceMode,
  resultSignalWord,
  signalPriceBasisCopy,
} from '../../lib/backtest/result-copy';

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

type BacktestAccuracyMode = 'point-in-time-1m' | 'approximate-5m';

type BacktestResponse = {
  source: string;
  accuracyMode: BacktestAccuracyMode;
  isApproximate: boolean;
  maxRangeDays: number;
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

type DataStatusProbe = {
  backtestMode: 'tushare-exact' | 'sina-free-approximate';
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
// Kept within the free (no-Tushare) 30-calendar-day cap by default, so the
// page works out of the box without any configuration.
const initialStartDate = hongKongDate(
  new Date(initialNow.getTime() - (MAX_LIVE_BACKTEST_CALENDAR_DAYS_FREE - 1) * 86_400_000),
);

export default function BacktestPage() {
  const [codes, setCodes] = useState('002892,603211,001269');
  const [startDate, setStartDate] = useState(initialStartDate);
  const [endDate, setEndDate] = useState(initialEndDate);
  const [holdingDays, setHoldingDays] = useState<1 | 3 | 5 | 10>(5);
  const [isRunning, setIsRunning] = useState(false);
  const [result, setResult] = useState<BacktestResponse | null>(null);
  const [error, setError] = useState('');
  // Despite the name (kept for minimal diff against the rest of the page),
  // this reflects the server's health-checked backtestMode, not merely
  // whether a Tushare token is configured — a configured-but-bad token
  // reports 'sina-free-approximate' here too, so the pre-run UI (cap, mode
  // label) matches what the run will actually use.
  const [tushareConfigured, setTushareConfigured] = useState<boolean | null>(null);

  useEffect(() => {
    let active = true;
    fetch('/api/data-status')
      .then(async (response) => {
        const payload = await response.json() as DataStatusProbe;
        if (active) setTushareConfigured(payload.backtestMode === 'tushare-exact');
      })
      .catch(() => { if (active) setTushareConfigured(false); });
    return () => { active = false; };
  }, []);

  const maxRangeDays = tushareConfigured
    ? MAX_LIVE_BACKTEST_CALENDAR_DAYS_TUSHARE
    : MAX_LIVE_BACKTEST_CALENDAR_DAYS_FREE;

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
  // Badge/copy for a completed run always comes from the response's own
  // metadata, never from client-side config guesses — a free (Sina)
  // result must never be labeled as Tushare/point-in-time data.
  const resultIsApproximate = result?.isApproximate ?? false;
  const modeLabel = tushareConfigured === null
    ? '正在检测数据源…'
    : tushareConfigured
      ? 'Tushare 精确模式（daily_basic 点时指标 + stk_mins 1 分钟线）'
      : '新浪免费近似模式（5 分钟线，无需 Token）';
  const sourceMode = backtestSourceMode(hasResult, resultIsApproximate, tushareConfigured);
  const signalPriceBasis = signalPriceBasisCopy(sourceMode);

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
        <div className="sidebar-foot"><div className="data-status"><span className="status-dot" />{tushareConfigured === null ? '正在检测数据源…' : tushareConfigured ? 'Tushare 历史回测' : '新浪免费近似回测'}</div><p>{tushareConfigured === null ? '检测中…' : tushareConfigured ? '点时指标 · 1 分钟线' : '5 分钟近似 · 免费源'}</p></div>
      </aside>
      <section className="workspace">
        <header className="topbar">
          <div><p className="eyebrow">策略回测 / 真实历史信号</p><h1>杨永兴尾盘战法</h1></div>
          <div className="topbar-actions"><AuthStatus nextPath="/backtest" /><Link className="back-link" href="/">返回选股 →</Link></div>
        </header>

        <div className="backtest-grid">
          <section className="strategy-card backtest-config">
            <span className="pill">{tushareConfigured === null ? '检测中…' : tushareConfigured ? 'POINT-IN-TIME · LIVE DATA' : 'APPROXIMATE · FREE DATA'}</span>
            <h2>真实回测参数</h2>
            <p className="section-copy">
              {tushareConfigured === null
                ? '正在检测是否配置 Tushare…'
                : tushareConfigured
                  ? '已配置 Tushare：使用历史日线、每日点时指标和 1 分钟线重放信号；先用前五项条件粗筛，再按需读取候选日期分钟线。'
                  : '当前使用新浪免费模式（未配置 Tushare，或已配置但探测未通过）：使用新浪财经免费 K 线数据源，以 5 分钟线近似重放信号；历史总市值、换手率与量比为静态估算值，非交易所口径。'}
            </p>
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
              <small>
                单次区间最多 {maxRangeDays} 个自然日（{modeLabel}）。
                {tushareConfigured === false ? '免费模式下市值 / 换手率 / 量比为估算值，仅供近似参考。' : ''}
              </small>
              <button type="button" onClick={runBacktest} disabled={isRunning || !startDate || !endDate || !codes.trim()}>
                {isRunning ? '正在读取历史行情…' : '运行真实回测'} <span>→</span>
              </button>
            </div>
            {error ? <p className="inline-error" role="alert">{error}</p> : null}
            <div className="backtest-note"><strong>信号价格口径</strong><p>策略需要尾盘收盘仍站稳突破位，因此以{signalPriceBasis}作为信号价，再观察第 N 个交易日收盘收益。</p></div>
          </section>
          <aside className="metric-stack">
            <div className="metric-card"><span>平均收益</span><strong className={hasResult ? result.averageReturnPct >= 0 ? 'positive' : 'negative' : ''}>{hasResult ? signedPercent(result.averageReturnPct) : '—'}</strong><small>{hasResult ? `完成 ${result.completedSignals}/${result.totalSignals} 个信号` : `持有 ${holdingDays} 日`}</small></div>
            <div className="metric-card"><span>信号胜率</span><strong>{hasResult ? `${result.winRatePct.toFixed(1)}%` : '—'}</strong><small>{hasResult ? `扫描 ${result.scannedTradingDays} 个股票交易日` : '等待运行'}</small></div>
            <div className="metric-card"><span>收益中位数</span><strong className={hasResult ? result.medianReturnPct >= 0 ? 'positive' : 'negative' : ''}>{hasResult ? signedPercent(result.medianReturnPct) : '—'}</strong><small>{hasResult ? `分钟复核 ${result.minuteDaysLoaded} 个候选日` : '点时数据口径'}</small></div>
          </aside>
        </div>

        {result ? <BacktestPerformance key={result.generatedAt} signals={result.signals} backtestEndDate={result.endDate} /> : null}

        <section className="results-card">
          <div className="results-head"><div><p className="eyebrow">信号明细</p><h2>{hasResult ? `${resultSignalWord(resultIsApproximate)}信号 ${result.totalSignals} · 完整收益 ${result.completedSignals}` : '等待真实历史回测'}</h2></div><span className="sample-badge">{hasResult ? (resultIsApproximate ? '新浪免费近似数据' : 'TUSHARE 精确数据') : '尚未运行'}</span></div>
          {result?.warnings.length ? <div className="source-note"><strong>口径说明</strong><span>{result.warnings.join('；')}</span></div> : null}
          <div className="table-wrap"><table><thead><tr><th>信号日期</th><th>股票</th><th>信号价</th><th>退出价</th><th>持有期</th><th>区间收益</th><th>突破时刻</th></tr></thead><tbody>
            {result?.signals.map((signal) => <tr key={`${signal.signalDate}-${signal.code}`}><td>{signal.signalDate}</td><td><Link href={`/stocks/${signal.code}`}><strong>{signal.name}</strong><small>{signal.code}</small></Link></td><td>¥{signal.signalPrice.toFixed(2)}</td><td>¥{signal.exitPrice.toFixed(2)}</td><td>{signal.holdingTradingDays} 日</td><td className={signal.returnPct >= 0 ? 'positive' : 'negative'}>{signedPercent(signal.returnPct)}</td><td>{signal.evaluation.intraday.breakoutTime ?? '—'}</td></tr>)}
            {result && result.signals.length === 0 ? <tr><td colSpan={7} className="empty-state">真实数据扫描完成，该区间没有形成可计算收益的{resultSignalWord(resultIsApproximate)}信号</td></tr> : null}
            {!result ? <tr><td colSpan={7} className="empty-state">配置股票和日期后运行；结果不再使用演示样本</td></tr> : null}
          </tbody></table></div>
          <p className="disclaimer">
            {hasResult
              ? `数据来源：${result.source}。${resultIsApproximate ? '5 分钟线近似口径，市值 / 换手率 / 量比为估算值，仅供参考，不代表交易所公布口径。' : 'daily、daily_basic 点时指标与 stk_mins 历史分钟线，1 分钟精确口径。'}回测结果不构成投资建议。`
              : `数据来源：${tushareConfigured === null ? '正在检测数据源…' : tushareConfigured ? 'Tushare Pro 日线、daily_basic 点时指标与 stk_mins 历史分钟线（1 分钟精确口径）' : '新浪财经免费 K 线（5 分钟近似口径，市值 / 换手率 / 量比为估算值）'}。回测结果不构成投资建议。`}
          </p>
        </section>
      </section>
    </main>
  );
}
