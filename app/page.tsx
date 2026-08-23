'use client';

import { useState } from 'react';

const rules = [
  { label: '当日涨幅', value: '3.00% — 5.00%', status: '严格区间' },
  { label: '近 30 日涨停', value: '至少 1 次', status: '历史窗口' },
  { label: '总市值', value: '< 200 亿元', status: '硬过滤' },
  { label: '当日量比', value: '> 1.00', status: '量能确认' },
  { label: '换手率', value: '5.00% — 10.00%', status: '活跃区间' },
  { label: '14:30 后走势', value: '新高后回踩不破', status: '分时确认' },
];

const candidates = [
  { code: '002892', name: '科力尔', change: '+4.38%', marketCap: '86.4 亿', volumeRatio: '1.76', turnover: '7.21%', time: '14:41', score: 92 },
  { code: '603211', name: '晋拓股份', change: '+3.91%', marketCap: '54.8 亿', volumeRatio: '1.42', turnover: '6.63%', time: '14:36', score: 88 },
  { code: '001269', name: '欧晶科技', change: '+4.72%', marketCap: '98.2 亿', volumeRatio: '2.08', turnover: '8.94%', time: '14:47', score: 86 },
];

export default function Home() {
  const [isRunning, setIsRunning] = useState(false);
  const [lastRun, setLastRun] = useState('15:02:18');

  function runScreen() {
    setIsRunning(true);
    window.setTimeout(() => {
      setIsRunning(false);
      setLastRun(new Date().toLocaleTimeString('zh-CN', { hour12: false }));
    }, 650);
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
          <a className="nav-item" href="/backtest"><span>↗</span>策略回测</a>
          <a className="nav-item" href="/data"><span>⌘</span>数据中心</a>
        </nav>
        <div className="sidebar-foot">
          <div className="data-status"><span className="status-dot amber" />a-stock-data · 开发</div>
          <p>当前页面使用演示数据</p>
          <p>最近运行 {lastRun}</p>
        </div>
      </aside>

      <section className="workspace" id="strategy">
        <header className="topbar">
          <div>
            <p className="eyebrow">策略工作台 / 尾盘信号</p>
            <h1>杨永兴尾盘战法</h1>
          </div>
          <div className="trade-state"><span className="status-dot" />沪深市场已收盘</div>
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
                {isRunning ? '正在扫描全市场…' : '运行今日选股'} <span>→</span>
              </button>
            </div>
          </section>

          <aside className="summary-card">
            <div className="summary-title"><span>今日扫描</span><span>2026.08.21</span></div>
            <div className="scan-number">5,412<small>只股票</small></div>
            <div className="funnel">
              <div><span>涨幅 3%—5%</span><strong>186</strong></div>
              <div><span>近 30 日有涨停</span><strong>47</strong></div>
              <div><span>市值 / 量比 / 换手</span><strong>12</strong></div>
              <div className="funnel-final"><span>分时形态确认</span><strong>3</strong></div>
            </div>
            <div className="quality"><span>数据完整度</span><strong>99.7%</strong></div>
            <div className="quality-bar"><i /></div>
          </aside>
        </div>

        <section className="results-card">
          <div className="results-head">
            <div><p className="eyebrow">筛选结果</p><h2>今日候选 · 3</h2></div>
            <button className="ghost-button" type="button">导出清单</button>
          </div>
          <div className="table-wrap">
            <table>
              <thead><tr><th>股票</th><th>当日涨幅</th><th>总市值</th><th>量比</th><th>换手率</th><th>突破时刻</th><th>策略评分</th></tr></thead>
              <tbody>
                {candidates.map((stock) => (
                  <tr key={stock.code}>
                    <td><strong>{stock.name}</strong><small>{stock.code}</small></td>
                    <td className="positive">{stock.change}</td>
                    <td>{stock.marketCap}</td><td>{stock.volumeRatio}</td><td>{stock.turnover}</td><td>{stock.time}</td>
                    <td><span className="score">{stock.score}</span></td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
          <p className="disclaimer">示例结果用于界面与规则验证，不构成投资建议；正式结果将由 a-stock-data 实时数据计算。</p>
        </section>
      </section>
    </main>
  );
}
