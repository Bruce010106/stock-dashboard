'use client';

import { useMemo, useState } from 'react';
import Link from 'next/link';
import AuthStatus from '../components/auth/AuthStatus';
import { MiniCandlestick } from '../components/screener/MiniCandlestick';
import styles from '../components/screener/Screener.module.css';
import {
  combineScreenerRows,
  filterScreenerRows,
  paginateScreenerRows,
  sortScreenerRows,
  type SortDirection,
  type SortKey,
} from '../components/screener/screener-utils';
import {
  DEFAULT_SCREENER_FILTERS,
  type ScreenNearMiss,
  type ScreenResult,
  type ScreenerFilters,
} from '../components/screener/types';

const rules = [
  { label: '当日涨幅', value: '3.00% — 5.00%', status: '严格区间' },
  { label: '近 30 日涨停', value: '至少 1 次', status: '历史窗口' },
  { label: '总市值', value: '< 200 亿元', status: '硬过滤' },
  { label: '当日量比', value: '> 1.00', status: '量能确认' },
  { label: '换手率', value: '5.00% — 10.00%', status: '活跃区间' },
  { label: '14:30 后走势', value: '新高后回踩不破', status: '分时确认' },
];

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
  const [filters, setFilters] = useState<ScreenerFilters>(DEFAULT_SCREENER_FILTERS);
  const [sortKey, setSortKey] = useState<SortKey>('score');
  const [sortDirection, setSortDirection] = useState<SortDirection>('desc');
  const [currentPage, setCurrentPage] = useState(1);
  const [pageSize, setPageSize] = useState(10);
  const strictResults = useMemo(() => scan?.results ?? [], [scan]);
  const nearMisses = useMemo(() => scan?.nearMisses ?? [], [scan]);
  const allRows = useMemo(
    () => combineScreenerRows(strictResults, nearMisses),
    [strictResults, nearMisses],
  );
  const filteredRows = useMemo(
    () => filterScreenerRows(allRows, filters),
    [allRows, filters],
  );
  const sortedRows = useMemo(
    () => sortScreenerRows(filteredRows, sortKey, sortDirection),
    [filteredRows, sortKey, sortDirection],
  );
  const pagination = useMemo(
    () => paginateScreenerRows(sortedRows, currentPage, pageSize),
    [sortedRows, currentPage, pageSize],
  );
  const hasCandidates = allRows.length > 0;
  const priceAvailable = allRows.some((row) => row.lastPrice !== undefined);
  const amountAvailable = allRows.some((row) => (row.amountYuan ?? 0) > 0);

  function updateFilter<Key extends keyof ScreenerFilters>(key: Key, value: ScreenerFilters[Key]) {
    setCurrentPage(1);
    setFilters((current) => ({ ...current, [key]: value }));
  }

  function handleSort(nextKey: SortKey) {
    setCurrentPage(1);
    if (sortKey === nextKey) {
      setSortDirection((direction) => direction === 'asc' ? 'desc' : 'asc');
      return;
    }
    setSortKey(nextKey);
    setSortDirection(nextKey === 'name' || nextKey === 'code' ? 'asc' : 'desc');
  }

  function sortIndicator(key: SortKey): string {
    if (sortKey !== key) return '↕';
    return sortDirection === 'asc' ? '↑' : '↓';
  }

  function sortAriaValue(key: SortKey): 'ascending' | 'descending' | 'none' {
    return sortKey === key ? (sortDirection === 'asc' ? 'ascending' : 'descending') : 'none';
  }

  function formatYi(value: number | undefined, digits = 1): string {
    if (value === undefined || !Number.isFinite(value)) return '—';
    return `${(value / 100_000_000).toFixed(digits)} 亿`;
  }

  function formatPrice(value: number | undefined): string {
    if (value === undefined || !Number.isFinite(value)) return '—';
    return value.toFixed(value >= 100 ? 1 : 2);
  }

  async function runScreen() {
    setIsRunning(true);
    setError('');
    try {
      const response = await fetch('/api/screen/yang-yongxing');
      const payload = await response.json() as ScreenResponse;
      if (!response.ok) throw new Error(payload.error ?? '真实行情扫描失败');
      setScan(payload);
      setCurrentPage(1);
      setLastRun(new Date(payload.generatedAt).toLocaleTimeString('zh-CN', { hour12: false }));
    } catch (cause) {
      setError(cause instanceof Error ? cause.message : '真实行情扫描失败');
    } finally {
      setIsRunning(false);
    }
  }

  function exportResults() {
    if (!scan || !hasCandidates) return;
    const rows = [
      ['代码', '名称', '现价', '涨幅%', '总市值(亿)', '成交额(亿)', '量比', '换手率%', '突破时刻', '评分', '结论', '未通过原因'],
      ...sortedRows.map((stock) => [
        stock.code,
        stock.name,
        formatPrice(stock.lastPrice),
        stock.changePct.toFixed(2),
        (stock.totalMarketCapYuan / 100_000_000).toFixed(2),
        stock.amountYuan === undefined ? '' : (stock.amountYuan / 100_000_000).toFixed(2),
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
          <Link className="nav-item" href="/portfolio"><span>◎</span>自选与持仓</Link>
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
          <div className="topbar-actions">
            <div className="trade-state"><span className="status-dot" />{scan ? `最新交易日 ${scan.tradeDate}` : '真实行情按需读取'}</div>
            <AuthStatus nextPath="/" />
          </div>
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
                <select id="universe" value="all" disabled aria-describedby="universe-note">
                  <option value="all">沪深京 A 股 · 排除 ST / 退市</option>
                </select>
                <small id="universe-note">当前接口仅支持全市场扫描，指数成分股筛选尚未接入。</small>
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
          <div className={styles.filterBar} aria-label="结果二次筛选">
            <div className={styles.filterTopline}>
              <div>
                <p className={styles.filterTitle}>二次筛选与排序</p>
                <p className={styles.filterHint}>点击表头可排序；筛选仅作用于本次扫描已返回的候选，不会重新请求行情。</p>
              </div>
              <button className={styles.resetButton} type="button" onClick={() => { setCurrentPage(1); setFilters(DEFAULT_SCREENER_FILTERS); }}>
                清除条件
              </button>
            </div>
            <div className={styles.filterGrid}>
              <label className={styles.filterField}>
                <span>代码 / 名称</span>
                <input
                  type="search"
                  value={filters.query}
                  onChange={(event) => updateFilter('query', event.target.value)}
                  placeholder="例如 600 或 平安"
                  aria-label="按股票代码或名称筛选"
                />
              </label>
              <label className={styles.filterField}>
                <span>最低价</span>
                <input
                  type="number"
                  step="0.01"
                  value={filters.minPrice}
                  onChange={(event) => updateFilter('minPrice', event.target.value)}
                  placeholder="不限"
                  disabled={!priceAvailable}
                  aria-label="最低价格"
                />
              </label>
              <label className={styles.filterField}>
                <span>最高价</span>
                <input
                  type="number"
                  step="0.01"
                  value={filters.maxPrice}
                  onChange={(event) => updateFilter('maxPrice', event.target.value)}
                  placeholder="不限"
                  disabled={!priceAvailable}
                  aria-label="最高价格"
                />
              </label>
              <label className={styles.filterField}>
                <span>最低涨幅%</span>
                <input
                  type="number"
                  step="0.01"
                  value={filters.minChange}
                  onChange={(event) => updateFilter('minChange', event.target.value)}
                  placeholder="不限"
                  aria-label="最低涨跌幅百分比"
                />
              </label>
              <label className={styles.filterField}>
                <span>最高涨幅%</span>
                <input
                  type="number"
                  step="0.01"
                  value={filters.maxChange}
                  onChange={(event) => updateFilter('maxChange', event.target.value)}
                  placeholder="不限"
                  aria-label="最高涨跌幅百分比"
                />
              </label>
              <label className={styles.filterField}>
                <span>最低市值(亿)</span>
                <input
                  type="number"
                  min="0"
                  step="0.1"
                  value={filters.minMarketCapYi}
                  onChange={(event) => updateFilter('minMarketCapYi', event.target.value)}
                  placeholder="不限"
                  disabled={!hasCandidates}
                  aria-label="最低总市值，单位亿元"
                />
              </label>
              <label className={styles.filterField}>
                <span>最高市值(亿)</span>
                <input
                  type="number"
                  min="0"
                  step="0.1"
                  value={filters.maxMarketCapYi}
                  onChange={(event) => updateFilter('maxMarketCapYi', event.target.value)}
                  placeholder="不限"
                  disabled={!hasCandidates}
                  aria-label="最高总市值，单位亿元"
                />
              </label>
              <label className={styles.filterField}>
                <span>最低成交额(亿)</span>
                <input
                  type="number"
                  min="0"
                  step="0.1"
                  value={filters.minAmountYi}
                  onChange={(event) => updateFilter('minAmountYi', event.target.value)}
                  placeholder={amountAvailable ? '不限' : '暂无字段'}
                  disabled={!amountAvailable}
                  aria-label="最低成交额，单位亿元"
                />
              </label>
              <label className={styles.filterField}>
                <span>最高成交额(亿)</span>
                <input
                  type="number"
                  min="0"
                  step="0.1"
                  value={filters.maxAmountYi}
                  onChange={(event) => updateFilter('maxAmountYi', event.target.value)}
                  placeholder={amountAvailable ? '不限' : '暂无字段'}
                  disabled={!amountAvailable}
                  aria-label="最高成交额，单位亿元"
                />
              </label>
              <label className={styles.filterCheck}>
                <input
                  type="checkbox"
                  checked={filters.excludeSt}
                  onChange={(event) => updateFilter('excludeSt', event.target.checked)}
                />
                <span>排除 ST / 退市</span>
              </label>
            </div>
            {!priceAvailable || !amountAvailable ? (
              <p className={styles.availabilityNote} role="note">
                {!priceAvailable ? '本次结果未返回现价，价格筛选已停用。' : ''}
                {!priceAvailable && !amountAvailable ? ' ' : ''}
                {!amountAvailable ? '成交额字段未由当前数据源提供，已停用成交额筛选。' : ''}
              </p>
            ) : null}
          </div>
          {filteredRows.length > 0 ? (
            <div className={styles.tableMeta} role="status" aria-live="polite">
              <span>当前显示 {(pagination.page - 1) * pageSize + 1}—{Math.min(pagination.page * pageSize, sortedRows.length)} / {sortedRows.length} 条</span>
              <span>排序：{sortKey === 'score' ? '评分' : sortKey === 'name' ? '名称' : sortKey === 'lastPrice' ? '现价' : sortKey === 'changePct' ? '涨幅' : sortKey === 'totalMarketCapYuan' ? '总市值' : sortKey === 'amountYuan' ? '成交额' : sortKey === 'volumeRatio' ? '量比' : '换手率'} {sortDirection === 'asc' ? '升序' : '降序'}</span>
            </div>
          ) : null}
          <div className="table-wrap">
            <table>
              <thead>
                <tr>
                  <th scope="col" aria-sort={sortAriaValue('name')}>
                    <button className={styles.sortButton} type="button" onClick={() => handleSort('name')} aria-label={`按股票名称排序，当前${sortAriaValue('name')}`}>
                      股票 / 近12日日线 <span className={styles.sortIcon} aria-hidden="true">{sortIndicator('name')}</span>
                    </button>
                  </th>
                  <th scope="col" aria-sort={sortAriaValue('changePct')}>
                    <button className={styles.sortButton} type="button" onClick={() => handleSort('changePct')} aria-label={`按当日涨幅排序，当前${sortAriaValue('changePct')}`}>
                      当日涨幅 <span className={styles.sortIcon} aria-hidden="true">{sortIndicator('changePct')}</span>
                    </button>
                  </th>
                  <th scope="col" aria-sort={sortAriaValue('lastPrice')}>
                    <button className={styles.sortButton} type="button" onClick={() => handleSort('lastPrice')} aria-label={`按最新价格排序，当前${sortAriaValue('lastPrice')}`}>
                      最新价 <span className={styles.sortIcon} aria-hidden="true">{sortIndicator('lastPrice')}</span>
                    </button>
                  </th>
                  <th scope="col" aria-sort={sortAriaValue('totalMarketCapYuan')}>
                    <button className={styles.sortButton} type="button" onClick={() => handleSort('totalMarketCapYuan')} aria-label={`按总市值排序，当前${sortAriaValue('totalMarketCapYuan')}`}>
                      总市值 <span className={styles.sortIcon} aria-hidden="true">{sortIndicator('totalMarketCapYuan')}</span>
                    </button>
                  </th>
                  <th scope="col" aria-sort={sortAriaValue('amountYuan')}>
                    <button className={styles.sortButton} type="button" onClick={() => handleSort('amountYuan')} aria-label={`按成交额排序，当前${sortAriaValue('amountYuan')}`}>
                      成交额 <span className={styles.sortIcon} aria-hidden="true">{sortIndicator('amountYuan')}</span>
                    </button>
                  </th>
                  <th scope="col" aria-sort={sortAriaValue('volumeRatio')}>
                    <button className={styles.sortButton} type="button" onClick={() => handleSort('volumeRatio')} aria-label={`按量比排序，当前${sortAriaValue('volumeRatio')}`}>
                      量比 <span className={styles.sortIcon} aria-hidden="true">{sortIndicator('volumeRatio')}</span>
                    </button>
                  </th>
                  <th scope="col" aria-sort={sortAriaValue('turnoverRatePct')}>
                    <button className={styles.sortButton} type="button" onClick={() => handleSort('turnoverRatePct')} aria-label={`按换手率排序，当前${sortAriaValue('turnoverRatePct')}`}>
                      换手率 <span className={styles.sortIcon} aria-hidden="true">{sortIndicator('turnoverRatePct')}</span>
                    </button>
                  </th>
                  <th scope="col">突破时刻</th>
                  <th scope="col">筛选结论</th>
                </tr>
              </thead>
              <tbody>
                {pagination.items.map((stock) => (
                  <tr className={stock.conclusion === '近似候选' ? 'near-miss-row' : undefined} key={stock.code}>
                    <td>
                      <div className={styles.stockCell}>
                        <MiniCandlestick name={stock.name} bars={stock.miniBars} changePct={stock.changePct} />
                        <Link className={styles.stockLink} href={`/stocks/${stock.code}`}>
                          <span className={styles.stockIdentity}><strong>{stock.name}</strong><small>{stock.code}</small></span>
                        </Link>
                      </div>
                    </td>
                    <td className={stock.changePct >= 0 ? 'positive' : 'negative'}>{stock.changePct >= 0 ? '+' : ''}{stock.changePct.toFixed(2)}%</td>
                    <td>{formatPrice(stock.lastPrice)}</td>
                    <td>{formatYi(stock.totalMarketCapYuan)}</td>
                    <td>{formatYi(stock.amountYuan)}</td>
                    <td>{stock.volumeRatio.toFixed(2)}</td>
                    <td>{stock.turnoverRatePct.toFixed(2)}%</td>
                    <td>{stock.breakoutTime ?? (stock.conclusion === '近似候选' ? '未突破' : '—')}</td>
                    <td className="result-reason"><span className={`result-badge ${stock.conclusion === '严格命中' ? 'matched' : 'near'}`}>{stock.conclusion}</span><small>{stock.conclusion === '严格命中' ? '六项条件全部通过' : stock.reason}</small></td>
                  </tr>
                ))}
                {scan && hasCandidates && filteredRows.length === 0 ? <tr><td colSpan={9} className="empty-state">没有符合当前二次筛选条件的结果</td></tr> : null}
                {scan && !hasCandidates ? <tr><td colSpan={9} className="empty-state">扫描完成，该交易日没有严格命中或近似候选</td></tr> : null}
                {!scan ? <tr><td colSpan={9} className="empty-state">点击“运行今日选股”读取真实市场行情</td></tr> : null}
              </tbody>
            </table>
          </div>
          {filteredRows.length > 0 ? (
            <nav className={styles.pagination} aria-label="筛选结果分页">
              <div className={styles.paginationGroup}>
                <button className={styles.pageButton} type="button" onClick={() => setCurrentPage((page) => Math.max(1, page - 1))} disabled={pagination.page <= 1} aria-label="上一页">上一页</button>
                <span className={styles.pageLabel}>第 {pagination.page} / {pagination.pageCount} 页</span>
                <button className={styles.pageButton} type="button" onClick={() => setCurrentPage((page) => Math.min(pagination.pageCount, page + 1))} disabled={pagination.page >= pagination.pageCount} aria-label="下一页">下一页</button>
              </div>
              <label>
                <span className={styles.visuallyHidden}>每页条数</span>
                <select className={styles.pageSizeSelect} value={pageSize} onChange={(event) => { setCurrentPage(1); setPageSize(Number(event.target.value)); }} aria-label="每页条数">
                  <option value={10}>每页 10 条</option>
                  <option value={20}>每页 20 条</option>
                  <option value={50}>每页 50 条</option>
                </select>
              </label>
            </nav>
          ) : null}
          <p className="disclaimer">结果由腾讯实时行情、腾讯分钟线及 Tushare/腾讯历史日线计算，不构成投资建议。</p>
        </section>
      </section>
    </main>
  );
}
