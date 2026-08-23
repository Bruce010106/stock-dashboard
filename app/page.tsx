'use client';

import { useState } from 'react';
import Link from 'next/link';

const rules = [
  { label: '当日涨幅', value: '3.00% — 5.00%', status: '严格区间' },
  { label: '近 30 日涨停', value: '至少 1 次', status: '历史窗口' },
  { label: '总市值', value: '< 200 亿元', status: '硬过滤' },
  { label: '当日量比', value: '> 1.00', status: '量能确认' },
  { label: '换手率', value: '5.00% — 10.00%', status: '活跃区间' },
  { label: '14:30 后走势', value: '新高后回踩不破', status: '分时确认' },
];

type ScreenResult = {
  code: string;
  name: string;
  changePct: number;
  totalMarketCapYuan: number;
  volumeRatio: number;
  turnoverRatePct: number;
  breakoutTime?: string;
  score: number;
};

type ScreenNearMiss = ScreenResult & {
  failedRuleLabel: string;
  reason: string;
};

type ScreenResponse = {
  tradeDate: string;
  generatedAt: string;
  historyMode: 'tushare' | 'tencent-fallback';
  scanned: number;
  quoted: number;
  funnel: { realtimeRules: number; recentLimitUp: number; intradayConfirmed: number };
  results: ScreenResult[];
  nearMisses: ScreenNearMiss[];
  warnings: string[];
  error?: string;
};

export default function Home() {
  const [isRunning, setIsRunning] = useState(false);
  const [lastRun, setLastRun] = useState('尚未运行');
  const [scan, setScan] = useState<ScreenResponse | null>(null);
  const [error, setError] = useState('');
  const strictResults = scan?.results ?? [];
  const nearMisses = scan?.nearMisses ?? [];
  const hasCandidates = strictResults.length + nearMisses.length > 0;

  async function runScreen() {
    setIsRunning(true);
    setError('');
    try {
      const response = await fetch('/api/screen/yang-yongxing');
      const payload = await response.json() as ScreenResponse;
      if (!response.ok) throw new Error(payload.error ?? '真实行情扫描失败');
      setScan(payload);
      setLastRun(new Date(payload.generatedAt).toLocaleTimeString('zh-CN', { hour12: false }));
    } catch (cause) {
      setError(cause instanceof Error ? cause.message : '真实行情扫描失败');
    } finally {
      setIsRunning(false);
    }
  }

  function exportResults() {
    if (!scan || !hasCandidates) return;
    const exportRows = [
      ...strictResults.map((stock) => ({ ...stock, conclusion: '严格命中', reason: '' })),
      ...nearMisses.map((stock) => ({ ...stock, conclusion: '近似候选', reason: stock.reason })),
    ];
    const rows = [
      ['代码', '名称', '涨幅%', '总市值(亿)', '量比', '换手率%', '突破时刻', '评分', '结论', '未通过原因'],
      ...exportRows.map((stock) => [
        stock.code,
        stock.name,
        stock.changePct.toFixed(2),
        (stock.totalMarketCapYuan / 100_000_000).toFixed(2),
        stock.volumeRatio.toFixed(2),
        stock.turnoverRatePct.toFixed(2),
        stock.breakoutTime ?? '',
        String(stock.score),
        stock.conclusion,
        stock.reason,
      ]),
    ];
    const csv = `\uFEFF${rows.map((row) => row.map((cell) => `"${String(cell).replaceAll('"', '""')}"`).join(',')).join('\n')}`;
    const link = document.createElement('a');
    link.href = URL.createObjectURL(new Blob([csv], { type: 'text/csv;charset=utf-8' }));
    link.download = `杨永兴尾盘战法-${scan.tradeDate}.csv`;
    link.click();
    URL.revokeObjectURL(link.href);
  }

  return (
    <main className="app-shell">
      <aside className="sidebar">
        <div className="brand">
          <span className="brand-mark">量</span>
          <span>知衡 Quant</span>
        </div>
        <nav aria-label="主导航">
          <a className="nav-item active" href="#strategy"><span>◈</span>策略选股</a>
          <Link className="nav-item" href="/backtest"><span>↗</span>策略回测</Link>
          <Link className="nav-item" href="/data"><span>⌘</span>数据中心</Link>
        </nav>
        <div className="sidebar-foot">
          <div className="data-status"><span className="status-dot" />a-stock-data · 已接通</div>
          <p>{scan?.historyMode === 'tushare' ? '腾讯 + Tushare 严谨口径' : '腾讯实时 · 免费历史降级'}</p>
          <p>最近运行 {lastRun}</p>
        </div>
      </aside>

      <section className="workspace" id="strategy">
        <header className="topbar">
          <div>
            <p className="eyebrow">策略工作台 / 尾盘信号</p>
            <h1>杨永兴尾盘战法</h1>
          </div>
          <div className="trade-state"><span className="status-dot" />{scan ? `最新交易日 ${scan.tradeDate}` : '真实行情按需读取'}</div>
        </header>

        <div className="content-grid">
          <section className="strategy-card">
            <div className="card-head">
              <div>
                <span className="pill">TAIL-1430 · V1</span>
                <h2>尾盘强势确认</h2>
                <p>捕捉近期有涨停基因、尾盘放量突破且承接稳定的中小市值标的。</p>
              </div>
              <div className="run-state">6 / 6<br/><small>条件启用</small></div>
            </div>

            <div className="rule-grid">
              {rules.map((rule, index) => (
                <article className="rule" key={rule.label}>
                  <div className="rule-index">{String(index + 1).padStart(2, '0')}</div>
                  <div>
                    <p>{rule.label}</p>
                    <strong>{rule.value}</strong>
                  </div>
                  <span className="rule-status">{rule.status}</span>
                </article>
              ))}
            </div>

            <div className="definition-note">
              <span>分时判定口径</span>
              <p>14:30 后首次突破此前日内最高价；突破后所有分钟 K 线最低价不低于突破价，且收盘仍站在突破价之上。</p>
            </div>

            <div className="action-row">
              <div className="universe">
                <label htmlFor="universe">股票池</label>
                <select id="universe" defaultValue="all">
                  <option value="all">沪深京 A 股 · 排除 ST / 退市</option>
                  <option value="hs300">沪深 300</option>
                  <option value="zz500">中证 500</option>
                </select>
              </div>
              <button type="button" onClick={runScreen} disabled={isRunning}>
                {isRunning ? '正在读取真实行情…' : '运行今日选股'} <span>→</span>
              </button>
            </div>
            {error ? <p className="inline-error" role="alert">{error}</p> : null}
            {scan ? (
              <div className="scan-complete" role="status">
                <span className="scan-complete-icon">✓</span>
                <div>
                  <strong>扫描完成：严格命中 {strictResults.length} 只</strong>
                  <p>{nearMisses.length > 0 ? `另有 ${nearMisses.length} 只通过前五项条件，具体淘汰原因见下方。` : '本次没有接近全部条件的候选股票。'}</p>
                </div>
              </div>
            ) : null}
          </section>

          <aside className="summary-card">
            <div className="summary-title"><span>真实行情扫描</span><span>{scan?.tradeDate ?? '等待运行'}</span></div>
            <div className="scan-number">{(scan?.scanned ?? 0).toLocaleString('zh-CN')}<small>只股票</small></div>
            <div className="funnel">
              <div><span>有效实时报价</span><strong>{scan?.quoted ?? 0}</strong></div>
              <div><span>涨幅 / 市值 / 量能 / 换手</span><strong>{scan?.funnel.realtimeRules ?? 0}</strong></div>
              <div><span>近 30 日有涨停</span><strong>{scan?.funnel.recentLimitUp ?? 0}</strong></div>
              <div className="funnel-final"><span>分时形态确认</span><strong>{scan?.funnel.intradayConfirmed ?? 0}</strong></div>
            </div>
            <div className="quality"><span>报价覆盖率</span><strong>{scan?.scanned ? `${(scan.quoted / scan.scanned * 100).toFixed(1)}%` : '—'}</strong></div>
            <div className="quality-bar"><i style={{ width: scan?.scanned ? `${Math.min(100, scan.quoted / scan.scanned * 100)}%` : '0%' }} /></div>
          </aside>
        </div>

        <section className="results-card">
          <div className="results-head">
            <div><p className="eyebrow">筛选结果</p><h2>严格命中 {strictResults.length} · 近似候选 {nearMisses.length}</h2></div>
            <button className="ghost-button" type="button" onClick={exportResults} disabled={!hasCandidates}>导出清单</button>
          </div>
          {scan?.warnings.length ? <div className="source-note"><strong>数据源说明</strong><span>{scan.warnings.join('；')}</span></div> : null}
          <div className="table-wrap">
            <table>
              <thead><tr><th>股票</th><th>当日涨幅</th><th>总市值</th><th>量比</th><th>换手率</th><th>突破时刻</th><th>筛选结论</th></tr></thead>
              <tbody>
                {strictResults.map((stock) => (
                  <tr key={stock.code}>
                    <td><strong>{stock.name}</strong><small>{stock.code}</small></td>
                    <td className="positive">+{stock.changePct.toFixed(2)}%</td>
                    <td>{(stock.totalMarketCapYuan / 100_000_000).toFixed(1)} 亿</td><td>{stock.volumeRatio.toFixed(2)}</td><td>{stock.turnoverRatePct.toFixed(2)}%</td><td>{stock.breakoutTime ?? '—'}</td>
                    <td className="result-reason"><span className="result-badge matched">严格命中</span><small>六项条件全部通过</small></td>
                  </tr>
                ))}
                {nearMisses.map((stock) => (
                  <tr className="near-miss-row" key={stock.code}>
                    <td><strong>{stock.name}</strong><small>{stock.code}</small></td>
                    <td className="positive">+{stock.changePct.toFixed(2)}%</td>
                    <td>{(stock.totalMarketCapYuan / 100_000_000).toFixed(1)} 亿</td><td>{stock.volumeRatio.toFixed(2)}</td><td>{stock.turnoverRatePct.toFixed(2)}%</td><td>{stock.breakoutTime ?? '未突破'}</td>
                    <td className="result-reason"><span className="result-badge near">近似候选</span><small>{stock.reason}</small></td>
                  </tr>
                ))}
                {scan && !hasCandidates ? <tr><td colSpan={7} className="empty-state">扫描完成，该交易日没有严格命中或近似候选</td></tr> : null}
                {!scan ? <tr><td colSpan={7} className="empty-state">点击“运行今日选股”读取真实市场行情</td></tr> : null}
              </tbody>
            </table>
          </div>
          <p className="disclaimer">结果由腾讯实时行情、腾讯分钟线及 Tushare/腾讯历史日线计算，不构成投资建议。</p>
        </section>
      </section>
    </main>
  );
}
