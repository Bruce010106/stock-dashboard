'use client';

import { useEffect, useState } from 'react';
import Link from 'next/link';
import AuthStatus from '../../components/auth/AuthStatus';

type ProviderStatus = {
  id: string;
  name: string;
  configured: boolean;
  healthy: boolean;
  role: string;
  error?: string;
};

type DataStatus = {
  checkedAt: string;
  healthy: boolean;
  latestQuoteAt?: string;
  historyMode: 'tushare' | 'tencent-fallback';
  backtestMode?: 'tushare-exact' | 'sina-free-approximate';
  providers: ProviderStatus[];
  warnings?: string[];
};

export default function DataPage() {
  const [status, setStatus] = useState<DataStatus | null>(null);

  useEffect(() => {
    let active = true;
    fetch('/api/data-status')
      .then(async (response) => {
        const payload = await response.json() as DataStatus;
        if (active) setStatus(payload);
      })
      .catch(() => undefined);
    return () => { active = false; };
  }, []);

  const datasets = status?.providers ?? [
    { id: 'tencent', name: '腾讯实时行情', configured: true, healthy: false, role: '正在检查连接' },
    { id: 'eastmoney', name: '全市场股票池', configured: true, healthy: false, role: '正在检查连接' },
    { id: 'tushare', name: 'Tushare Pro', configured: false, healthy: false, role: '可选精确增强源；未配置时回测自动使用新浪财经免费近似源' },
  ];

  return (
    <main className="app-shell">
      <aside className="sidebar">
        <div className="brand"><span className="brand-mark">量</span><span>知衡 Quant</span></div>
        <nav aria-label="主导航">
          <Link className="nav-item" href="/"><span>◈</span>策略选股</Link>
          <Link className="nav-item" href="/backtest"><span>↗</span>策略回测</Link>
          <Link className="nav-item" href="/portfolio"><span>◎</span>自选与持仓</Link>
          <Link className="nav-item active" href="/data"><span>⌘</span>数据中心</Link>
        </nav>
        <div className="sidebar-foot"><div className="data-status"><span className={`status-dot ${status?.healthy ? '' : 'amber'}`} />{status === null ? '连接检查中' : status.healthy ? '核心数据在线' : '核心数据异常'}</div><p>a-stock-data Provider</p></div>
      </aside>
      <section className="workspace">
        <header className="topbar"><div><p className="eyebrow">数据中心 / Provider</p><h1>a-stock-data</h1></div><div className="topbar-actions"><AuthStatus nextPath="/data" /><Link className="back-link" href="/">返回选股 →</Link></div></header>
        <div className="data-hero">
          <div><span className="pill">LIVE · SERVER ONLY</span><h2>真实市场数据连接</h2><p>腾讯行情提供实时快照和分钟线，东方财富提供股票池，Tushare 提供点时历史指标；策略仍只依赖统一字段，不直接耦合上游格式。</p></div>
          <div className="contract-state"><strong>{status?.providers.filter((item) => item.healthy).length ?? '—'}</strong><span>可用数据源</span><small>{status?.latestQuoteAt ? `行情 ${new Date(status.latestQuoteAt).toLocaleString('zh-CN', { hour12: false })}` : '正在探测真实行情'}</small></div>
        </div>
        <section className="results-card data-table-card">
          <div className="results-head"><div><p className="eyebrow">接入矩阵</p><h2>生产数据源</h2></div><span className="sample-badge">{status?.historyMode === 'tushare' ? '严谨历史口径' : '免费降级口径'}</span></div>
          {status?.warnings?.length ? <div className="source-note"><strong>连接说明</strong><span>{status.warnings.join('；')}</span></div> : null}
          <div className="table-wrap"><table><thead><tr><th>数据源</th><th>用途</th><th>配置</th><th>当前状态</th></tr></thead><tbody>
            {datasets.map((dataset) => <tr key={dataset.id}><td><strong>{dataset.name}</strong>{dataset.error ? <small>{dataset.error}</small> : null}</td><td>{dataset.role}</td><td>{dataset.configured ? '已配置' : '缺少密钥'}</td><td><span className={`contract-badge ${dataset.healthy ? '' : 'contract-badge-warn'}`}>{dataset.healthy ? '已接通' : status === null ? '检查中' : dataset.configured ? '连接异常' : '使用降级源'}</span></td></tr>)}
          </tbody></table></div>
        </section>
        <div className="data-cautions">
          <article><span>01</span><div><strong>先粗筛，再读取分钟线</strong><p>全市场实时快照批量读取；只有通过前五项条件的少量股票继续读取日线和分钟线。</p></div></article>
          <article><span>02</span><div><strong>自动降级</strong><p>未配置或暂时无法访问 Tushare 时，今日选股自动使用腾讯不复权日线，不影响实时筛选入口；策略回测自动切换到新浪财经免费数据源（5 分钟近似口径，单次区间最长 30 天），仍可正常运行，不会因缺少 Tushare 而不可用。</p></div></article>
          <article><span>03</span><div><strong>密钥仅在服务端</strong><p>TUSHARE_TOKEN 不会发送到浏览器，也不会使用 NEXT_PUBLIC_ 前缀打入手机端代码。</p></div></article>
        </div>
      </section>
    </main>
  );
}
