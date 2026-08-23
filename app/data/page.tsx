const datasets = [
  { name: '股票池与状态', source: 'BaoStock / 东财', use: '点时股票池、ST、上市退市', status: '契约就绪' },
  { name: '历史日线', source: 'mootdx / 腾讯', use: '涨幅、近 30 日涨停、回测', status: '契约就绪' },
  { name: '复权因子', source: '新浪', use: '指标计算与除权处理', status: '契约就绪' },
  { name: '实时快照', source: '腾讯', use: '市值、量比、换手率', status: '契约就绪' },
  { name: '分钟行情', source: '腾讯 / 通达信', use: '14:30 后新高与回踩确认', status: '契约就绪' },
];

export default function DataPage() {
  return (
    <main className="app-shell">
      <aside className="sidebar">
        <div className="brand"><span className="brand-mark">量</span><span>知衡 Quant</span></div>
        <nav aria-label="主导航">
          <a className="nav-item" href="/"><span>◈</span>策略选股</a>
          <a className="nav-item" href="/backtest"><span>↗</span>策略回测</a>
          <a className="nav-item active" href="/data"><span>⌘</span>数据中心</a>
        </nav>
        <div className="sidebar-foot"><div className="data-status"><span className="status-dot amber" />开发连接</div><p>a-stock-data Provider</p></div>
      </aside>
      <section className="workspace">
        <header className="topbar"><div><p className="eyebrow">数据中心 / Provider</p><h1>a-stock-data</h1></div><a className="back-link" href="/">返回选股 →</a></header>
        <div className="data-hero">
          <div><span className="pill">APACHE-2.0 · 54 ENDPOINTS</span><h2>市场数据适配契约</h2><p>平台策略只依赖统一字段，不直接耦合上游接口。正式连接器会负责代码归一、批量请求、限流、重试、降级和本地缓存。</p></div>
          <div className="contract-state"><strong>4</strong><span>标准数据模型</span><small>股票池 / 日线 / 分钟 / 快照</small></div>
        </div>
        <section className="results-card data-table-card">
          <div className="results-head"><div><p className="eyebrow">接入矩阵</p><h2>杨永兴策略所需数据</h2></div><span className="sample-badge">接口契约阶段</span></div>
          <div className="table-wrap"><table><thead><tr><th>数据集</th><th>优先数据源</th><th>策略用途</th><th>当前状态</th></tr></thead><tbody>
            {datasets.map((dataset) => <tr key={dataset.name}><td><strong>{dataset.name}</strong></td><td>{dataset.source}</td><td>{dataset.use}</td><td><span className="contract-badge">{dataset.status}</span></td></tr>)}
          </tbody></table></div>
        </section>
        <div className="data-cautions">
          <article><span>01</span><div><strong>批量同步，而非逐股即时抓取</strong><p>盘后增量同步日线与复权数据；盘中只读取候选股票的分钟行情，避免触发上游风控。</p></div></article>
          <article><span>02</span><div><strong>点时口径</strong><p>历史回测使用当时可见的股票池、市值、ST 与涨停价，避免幸存者偏差和未来数据泄漏。</p></div></article>
          <article><span>03</span><div><strong>信号与成交分离</strong><p>分时条件使用分钟数据确认；退出方式由回测参数明确指定，不从未来行情反推信号。</p></div></article>
        </div>
      </section>
    </main>
  );
}
