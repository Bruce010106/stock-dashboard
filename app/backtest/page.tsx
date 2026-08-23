'use client';

import { useMemo, useState } from 'react';

const rows = [
  { date: '2026-08-04', stock: '科力尔', code: '002892', entry: 14.86, returns: { 1: 1.62, 3: 4.31, 5: 6.08, 10: 3.74 } },
  { date: '2026-07-22', stock: '晋拓股份', code: '603211', entry: 18.24, returns: { 1: -0.82, 3: 2.14, 5: 3.26, 10: 7.11 } },
  { date: '2026-06-18', stock: '欧晶科技', code: '001269', entry: 31.52, returns: { 1: 2.05, 3: -1.14, 5: 1.88, 10: 5.42 } },
] as const;

export default function BacktestPage() {
  const [holdingDays, setHoldingDays] = useState<1 | 3 | 5 | 10>(5);
  const selectedReturns = useMemo(
    () => rows.map((row) => row.returns[holdingDays]),
    [holdingDays],
  );
  const average = selectedReturns.reduce((sum, value) => sum + value, 0) / selectedReturns.length;
  const winRate = selectedReturns.filter((value) => value > 0).length / selectedReturns.length * 100;

  return (
    <main className="app-shell">
      <aside className="sidebar">
        <div className="brand"><span className="brand-mark">量</span><span>知衡 Quant</span></div>
        <nav aria-label="主导航">
          <a className="nav-item" href="/"><span>◈</span>策略选股</a>
          <a className="nav-item active" href="/backtest"><span>↗</span>策略回测</a>
          <a className="nav-item" href="/data"><span>⌘</span>数据中心</a>
        </nav>
        <div className="sidebar-foot"><div className="data-status"><span className="status-dot" />信号表现模式</div><p>独立信号 · 不叠加仓位</p></div>
      </aside>
      <section className="workspace">
        <header className="topbar">
          <div><p className="eyebrow">策略回测 / 信号表现</p><h1>杨永兴尾盘战法</h1></div>
          <a className="back-link" href="/">返回选股 →</a>
        </header>

        <div className="backtest-grid">
          <section className="strategy-card backtest-config">
            <span className="pill">POINT-IN-TIME</span>
            <h2>回测参数</h2>
            <p className="section-copy">以尾盘形态确认分钟的收盘价作为信号价，观察随后固定交易日的收益；所有筛选字段均使用当时可见数据。</p>
            <div className="field-grid">
              <label>回测区间<input value="2024-01-01 — 2026-08-21" readOnly /></label>
              <label>股票池<input value="沪深京 A 股（排除 ST / 退市）" readOnly /></label>
              <label>持有交易日
                <select value={holdingDays} onChange={(event) => setHoldingDays(Number(event.target.value) as 1 | 3 | 5 | 10)}>
                  <option value={1}>1 个交易日</option><option value={3}>3 个交易日</option><option value={5}>5 个交易日</option><option value={10}>10 个交易日</option>
                </select>
              </label>
              <label>价格口径<input value="信号分钟价 → 第 N 日收盘价" readOnly /></label>
            </div>
            <div className="backtest-note"><strong>为什么使用信号表现回测？</strong><p>原战法只定义了选股条件，没有规定止盈、止损或卖出日。固定持有期避免擅自发明退出规则；后续可再增加完整账户回测。</p></div>
          </section>
          <aside className="metric-stack">
            <div className="metric-card"><span>平均收益</span><strong className={average >= 0 ? 'positive' : ''}>{average >= 0 ? '+' : ''}{average.toFixed(2)}%</strong><small>持有 {holdingDays} 日</small></div>
            <div className="metric-card"><span>信号胜率</span><strong>{winRate.toFixed(1)}%</strong><small>示例信号 3 次</small></div>
            <div className="metric-card"><span>回测口径</span><strong className="metric-text">无未来数据</strong><small>分钟级触发</small></div>
          </aside>
        </div>

        <section className="results-card">
          <div className="results-head"><div><p className="eyebrow">信号明细</p><h2>历史命中样本</h2></div><span className="sample-badge">演示数据</span></div>
          <div className="table-wrap"><table><thead><tr><th>信号日期</th><th>股票</th><th>信号价</th><th>持有期</th><th>区间收益</th><th>形态确认</th></tr></thead><tbody>
            {rows.map((row) => { const value = row.returns[holdingDays]; return <tr key={row.date + row.code}><td>{row.date}</td><td><strong>{row.stock}</strong><small>{row.code}</small></td><td>¥{row.entry.toFixed(2)}</td><td>{holdingDays} 日</td><td className={value >= 0 ? 'positive' : 'negative'}>{value >= 0 ? '+' : ''}{value.toFixed(2)}%</td><td><span className="score">✓</span></td></tr>; })}
          </tbody></table></div>
          <p className="disclaimer">页面为交互与回测口径演示；真实历史信号将由 a-stock-data 日线、分钟线和点时市值数据生成。</p>
        </section>
      </section>
    </main>
  );
}
