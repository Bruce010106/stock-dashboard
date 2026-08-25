'use client';

import Link from 'next/link';
import {
  useCallback,
  useEffect,
  useMemo,
  useRef,
  useSyncExternalStore,
  useState,
} from 'react';
import type { FormEvent } from 'react';
import AuthStatus from '../auth/AuthStatus.tsx';
import {
  calculateHoldingValuation,
  calculatePortfolioTotals,
} from '../../lib/portfolio/calculations.ts';
import {
  createEmptyPortfolioState,
  fetchPortfolioCloud,
  getPortfolioMigrationStatus,
  getPortfolioSnapshot,
  getServerPortfolioSnapshot,
  hasPortfolioData,
  importPortfolioCloud,
  loadPortfolioState,
  markPortfolioMigrationStatus,
  mergePortfolioStates,
  PortfolioCloudError,
  savePortfolioCloud,
  setPortfolioStorageScope,
  setPortfolioSnapshot,
  subscribeToPortfolio,
} from '../../lib/portfolio/storage.ts';
import {
  normalizePortfolioCode,
  validateHoldingDraft,
} from '../../lib/portfolio/validation.ts';
import {
  MAX_HOLDINGS,
  MAX_WATCHLIST_ITEMS,
} from '../../lib/portfolio/types.ts';
import type {
  PortfolioHolding,
  PortfolioCloudResponse,
  PortfolioQuote,
  PortfolioQuotesResponse,
  PortfolioState,
  PortfolioSyncStatus,
} from '../../lib/portfolio/types.ts';
import styles from './portfolio.module.css';

type QuoteState = {
  status: 'idle' | 'loading' | 'ready' | 'degraded' | 'error';
  message?: string;
  updatedAt?: string;
};

type HoldingDraft = {
  code: string;
  quantity: string;
  costPrice: string;
};

type PortfolioSyncState = {
  status: PortfolioSyncStatus;
  message?: string;
  updatedAt?: string;
};

type MigrationPrompt = {
  userId: string;
  email: string | null;
  localState: PortfolioState;
};

const EMPTY_DRAFT: HoldingDraft = { code: '', quantity: '', costPrice: '' };

function formatPrice(value: number | null | undefined): string {
  return value === null || value === undefined || !Number.isFinite(value)
    ? '—'
    : value.toFixed(2);
}

function formatNumber(value: number | null | undefined, digits = 2): string {
  return value === null || value === undefined || !Number.isFinite(value)
    ? '—'
    : value.toLocaleString('zh-CN', {
      minimumFractionDigits: digits,
      maximumFractionDigits: digits,
    });
}

function formatMoney(value: number | null | undefined): string {
  return value === null || value === undefined || !Number.isFinite(value)
    ? '—'
    : `¥${formatNumber(value)}`;
}

function holdingId(): string {
  if (typeof crypto !== 'undefined' && typeof crypto.randomUUID === 'function') {
    return crypto.randomUUID();
  }
  return `holding-${Date.now()}-${Math.random().toString(36).slice(2, 10)}`;
}

function quoteClass(value: number | null | undefined): string {
  if (value === null || value === undefined || value === 0) return styles.neutral;
  return value > 0 ? styles.up : styles.down;
}

function quoteTime(timestamp: string | undefined): string {
  if (!timestamp) return '时间未知';
  const time = new Date(timestamp);
  return Number.isNaN(time.getTime())
    ? '时间未知'
    : time.toLocaleString('zh-CN', { hour12: false });
}

export default function PortfolioDashboard() {
  const portfolio = useSyncExternalStore(
    subscribeToPortfolio,
    getPortfolioSnapshot,
    getServerPortfolioSnapshot,
  );
  const [watchlistInput, setWatchlistInput] = useState('');
  const [watchlistError, setWatchlistError] = useState('');
  const [holdingDraft, setHoldingDraft] = useState<HoldingDraft>(EMPTY_DRAFT);
  const [holdingError, setHoldingError] = useState('');
  const [editingHoldingId, setEditingHoldingId] = useState<string | null>(null);
  const [quotes, setQuotes] = useState<Record<string, PortfolioQuote>>({});
  const [quoteState, setQuoteState] = useState<QuoteState>({ status: 'idle' });
  const [refreshToken, setRefreshToken] = useState(0);
  const [syncState, setSyncState] = useState<PortfolioSyncState>({ status: 'checking' });
  const [cloudAccount, setCloudAccount] = useState<Pick<PortfolioCloudResponse, 'userId' | 'email'> | null>(null);
  const [migrationPrompt, setMigrationPrompt] = useState<MigrationPrompt | null>(null);
  const [migrationBusy, setMigrationBusy] = useState(false);
  const syncRequestRef = useRef(0);

  const updatePortfolio = useCallback(
    (updater: (current: PortfolioState) => PortfolioState) => {
      const current = getPortfolioSnapshot();
      const next = updater(current);
      setPortfolioSnapshot(next);
      const account = cloudAccount;
      if (!account?.userId) return;
      const requestId = ++syncRequestRef.current;
      setSyncState({ status: 'syncing' });
      void savePortfolioCloud(next, {
        userId: account.userId,
        previousState: current,
      }).then(() => {
        if (requestId !== syncRequestRef.current) return;
        setSyncState({ status: 'synced', updatedAt: new Date().toISOString() });
      }).catch((error: unknown) => {
        if (requestId !== syncRequestRef.current) return;
        setSyncState({
          status: 'error',
          message: error instanceof Error ? error.message : '云端同步失败，请稍后重试',
        });
      });
    },
    [cloudAccount],
  );

  useEffect(() => {
    const controller = new AbortController();
    let active = true;

    async function initializeCloud() {
      setSyncState({ status: 'checking' });
      let cloud: PortfolioCloudResponse;
      try {
        cloud = await fetchPortfolioCloud({ signal: controller.signal });
      } catch (error) {
        if (!active || controller.signal.aborted) return;
        setCloudAccount(null);
        setPortfolioStorageScope(null);
        const status = error instanceof PortfolioCloudError &&
          error.status > 0 && error.status !== 404 && error.status !== 503
          ? 'error'
          : 'offline';
        setSyncState({
          status,
          message: error instanceof Error ? error.message : '当前使用离线缓存，联网后可同步',
        });
        return;
      }
      if (!active || controller.signal.aborted) return;

      if (!cloud.authenticated || !cloud.userId) {
        setCloudAccount(null);
        setPortfolioStorageScope(null);
        setSyncState({ status: 'local' });
        return;
      }

      let localState = createEmptyPortfolioState();
      try {
        if (typeof window !== 'undefined') localState = loadPortfolioState(window.localStorage);
      } catch {
        // The storage helper already handles unavailable/private localStorage.
      }
      setPortfolioStorageScope(cloud.userId);
      setPortfolioSnapshot(cloud.portfolio);
      setCloudAccount({ userId: cloud.userId, email: cloud.email });
      setSyncState({ status: 'synced', updatedAt: new Date().toISOString() });

      const migrationStatus = getPortfolioMigrationStatus(cloud.userId);
      if (hasPortfolioData(localState) && !migrationStatus) {
        setMigrationPrompt({
          userId: cloud.userId,
          email: cloud.email,
          localState,
        });
      }
    }

    void initializeCloud();
    return () => {
      active = false;
      controller.abort();
    };
  }, []);

  async function mergeAnonymousPortfolio() {
    if (!migrationPrompt || migrationBusy) return;
    setMigrationBusy(true);
    syncRequestRef.current += 1;
    setSyncState({ status: 'syncing' });
    try {
      const mergedPreview = mergePortfolioStates(
        getPortfolioSnapshot(),
        migrationPrompt.localState,
      );
      const response = await importPortfolioCloud(migrationPrompt.localState);
      setPortfolioSnapshot(hasPortfolioData(response.portfolio) ? response.portfolio : mergedPreview);
      markPortfolioMigrationStatus(migrationPrompt.userId, 'merged');
      setMigrationPrompt(null);
      setSyncState({ status: 'synced', updatedAt: new Date().toISOString() });
    } catch (error) {
      setSyncState({
        status: 'error',
        message: error instanceof Error ? error.message : '合并失败，请稍后重试',
      });
    } finally {
      setMigrationBusy(false);
    }
  }

  function deferAnonymousPortfolio() {
    if (!migrationPrompt || migrationBusy) return;
    markPortfolioMigrationStatus(migrationPrompt.userId, 'deferred');
    setMigrationPrompt(null);
    setSyncState({ status: 'synced', updatedAt: new Date().toISOString() });
  }

  const quoteCodes = useMemo(() => {
    const codes = new Set(portfolio.watchlist);
    for (const holding of portfolio.holdings) codes.add(holding.code);
    return [...codes];
  }, [portfolio.holdings, portfolio.watchlist]);
  const quoteCodesKey = quoteCodes.join(',');

  useEffect(() => {
    let active = true;
    if (quoteCodes.length === 0) {
      return () => {
        active = false;
      };
    }

    const controller = new AbortController();
    async function fetchQuotes() {
      try {
        const response = await fetch(
          `/api/portfolio/quotes?codes=${encodeURIComponent(quoteCodesKey)}`,
          { signal: controller.signal, cache: 'no-store' },
        );
        const payload = await response.json() as PortfolioQuotesResponse & { error?: string };
        if (!response.ok) {
          throw new Error(payload.warning ?? payload.error ?? '实时行情暂时不可用');
        }
        if (!active) return;
        const nextQuotes: Record<string, PortfolioQuote> = {};
        for (const quote of payload.snapshots) nextQuotes[quote.code] = quote;
        setQuotes(nextQuotes);
        setQuoteState({
          status: payload.degraded ? 'degraded' : 'ready',
          message: payload.warning,
          updatedAt: payload.generatedAt,
        });
      } catch (error) {
        if (!active || controller.signal.aborted) return;
        setQuotes({});
        setQuoteState({
          status: 'error',
          message: error instanceof Error
            ? error.message
            : '实时行情服务暂时不可用，请稍后重试',
        });
      }
    }
    void fetchQuotes();
    return () => {
      active = false;
      controller.abort();
    };
  }, [quoteCodesKey, quoteCodes.length, refreshToken]);

  const totals = useMemo(
    () => calculatePortfolioTotals(portfolio.holdings, quotes),
    [portfolio.holdings, quotes],
  );

  const refreshQuotes = useCallback(() => {
    if (quoteCodes.length > 0) setRefreshToken((token) => token + 1);
  }, [quoteCodes.length]);

  function addWatchlist(event: FormEvent<HTMLFormElement>) {
    event.preventDefault();
    const code = normalizePortfolioCode(watchlistInput);
    if (!code.ok) {
      setWatchlistError(code.error);
      return;
    }
    if (portfolio.watchlist.includes(code.value)) {
      setWatchlistError('这只股票已经在自选列表中');
      return;
    }
    if (portfolio.watchlist.length >= MAX_WATCHLIST_ITEMS) {
      setWatchlistError(`自选股最多保存 ${MAX_WATCHLIST_ITEMS} 只`);
      return;
    }
    updatePortfolio((current) => ({
      ...current,
      watchlist: [...current.watchlist, code.value],
    }));
    setWatchlistInput('');
    setWatchlistError('');
  }

  function removeWatchlist(code: string) {
    updatePortfolio((current) => ({
      ...current,
      watchlist: current.watchlist.filter((item) => item !== code),
    }));
  }

  function updateHoldingDraft(field: keyof HoldingDraft, value: string) {
    setHoldingDraft((current) => ({ ...current, [field]: value }));
  }

  function saveHolding(event: FormEvent<HTMLFormElement>) {
    event.preventDefault();
    const validated = validateHoldingDraft(holdingDraft);
    if (!validated.ok) {
      setHoldingError(validated.error);
      return;
    }

    if (editingHoldingId) {
      updatePortfolio((current) => ({
        ...current,
        holdings: current.holdings.map((holding) => holding.id === editingHoldingId
          ? { ...holding, ...validated.value }
          : holding),
      }));
    } else {
      if (portfolio.holdings.length >= MAX_HOLDINGS) {
        setHoldingError(`持仓记录最多保存 ${MAX_HOLDINGS} 条`);
        return;
      }
      updatePortfolio((current) => ({
        ...current,
        holdings: [...current.holdings, { id: holdingId(), ...validated.value }],
      }));
    }
    setHoldingDraft(EMPTY_DRAFT);
    setEditingHoldingId(null);
    setHoldingError('');
  }

  function editHolding(holding: PortfolioHolding) {
    setEditingHoldingId(holding.id);
    setHoldingDraft({
      code: holding.code,
      quantity: String(holding.quantity),
      costPrice: String(holding.costPrice),
    });
    setHoldingError('');
  }

  function cancelEdit() {
    setEditingHoldingId(null);
    setHoldingDraft(EMPTY_DRAFT);
    setHoldingError('');
  }

  function removeHolding(id: string) {
    updatePortfolio((current) => ({
      ...current,
      holdings: current.holdings.filter((holding) => holding.id !== id),
    }));
    if (editingHoldingId === id) cancelEdit();
  }

  const syncStatusLabel = syncState.status === 'checking'
    ? '正在检查云端…'
    : syncState.status === 'syncing'
      ? '正在同步…'
      : syncState.status === 'synced'
        ? cloudAccount?.email ? `已同步 · ${cloudAccount.email}` : '已同步'
        : syncState.status === 'offline'
          ? '离线缓存'
          : syncState.status === 'error'
            ? '同步失败'
            : '未登录 · 本地保存';

  return (
    <main className={styles.page}>
      <header className={styles.header}>
        <div>
          <Link className={styles.backLink} href="/">← 返回策略选股</Link>
          <p className={styles.eyebrow}>组合管理 / {cloudAccount ? '云端保存' : '本地保存'}</p>
          <h1>自选股与持仓</h1>
          <p className={styles.subtitle}>
            {cloudAccount
              ? '已登录，记录会保存到云端并在当前设备保留缓存；价格来自实时行情接口。'
              : '未登录时保存在当前浏览器；登录后可同步到云端。价格来自实时行情接口。'}
          </p>
        </div>
        <div className={styles.accountStack}>
          <AuthStatus nextPath="/portfolio" />
          <div className={styles.statusPill} data-state={syncState.status} title={syncState.message}>
            <span className={styles.statusDot} />
            <span>{syncStatusLabel}</span>
          </div>
        </div>
      </header>

      <section className={styles.notice} aria-live="polite">
        <div>
          <strong>行情口径</strong>
          <span>
            {quoteState.updatedAt
              ? `腾讯实时快照 · 更新于 ${quoteTime(quoteState.updatedAt)}`
              : '添加股票后按需读取腾讯实时快照'}
          </span>
        </div>
        <button
          className={styles.secondaryButton}
          type="button"
          onClick={refreshQuotes}
          disabled={quoteState.status === 'loading' || quoteCodes.length === 0}
        >
          {quoteState.status === 'loading' ? '读取中…' : '刷新报价'}
        </button>
      </section>
      {syncState.message ? (
        <p className={syncState.status === 'error' ? styles.error : styles.warning} role="status">
          {syncState.message}
        </p>
      ) : null}
      {quoteState.message ? (
        <p className={quoteState.status === 'error' ? styles.error : styles.warning} role="status">
          {quoteState.message}
        </p>
      ) : null}

      <section className={styles.metrics} aria-label="持仓汇总">
        <article className={styles.metricCard}>
          <span>持仓市值</span>
          <strong>{totals.totalCount > 0 ? formatMoney(totals.marketValue) : '—'}</strong>
          <small>{totals.totalCount > 0 ? `报价覆盖 ${totals.pricedCount}/${totals.totalCount}` : '尚未录入持仓'}</small>
        </article>
        <article className={styles.metricCard}>
          <span>持仓盈亏</span>
          <strong className={quoteClass(totals.pnl)}>{totals.totalCount > 0 ? formatMoney(totals.pnl) : '—'}</strong>
          <small>{totals.totalCount > 0 ? '按成本价与最新价计算' : '尚未录入持仓'}</small>
        </article>
        <article className={styles.metricCard}>
          <span>盈亏比例</span>
          <strong className={quoteClass(totals.pnlPct)}>{totals.totalCount > 0 ? `${formatNumber(totals.pnlPct)}%` : '—'}</strong>
          <small>{totals.totalCount > 0 && totals.pnl === null ? '报价不完整，暂不汇总' : '已报价持仓的成本口径'}</small>
        </article>
        <article className={styles.metricCard}>
          <span>{cloudAccount ? '当前记录' : '本地记录'}</span>
          <strong>{portfolio.watchlist.length + portfolio.holdings.length}</strong>
          <small>{cloudAccount ? '云端主数据 · ' : ''}自选 {portfolio.watchlist.length} · 持仓 {portfolio.holdings.length}</small>
        </article>
      </section>

      <div className={styles.columns}>
        <section className={styles.panel} aria-labelledby="watchlist-title">
          <div className={styles.panelHeader}>
            <div>
              <p className={styles.sectionKicker}>WATCHLIST</p>
              <h2 id="watchlist-title">自选股</h2>
            </div>
            <span className={styles.countBadge}>{portfolio.watchlist.length}/{MAX_WATCHLIST_ITEMS}</span>
          </div>
          <form className={styles.addForm} onSubmit={addWatchlist}>
            <label htmlFor="watchlist-code">添加 A 股代码</label>
            <div className={styles.formRow}>
              <input
                id="watchlist-code"
                value={watchlistInput}
                onChange={(event) => setWatchlistInput(event.target.value)}
                placeholder="例如 600519 或 SZ000001"
                inputMode="text"
                autoComplete="off"
                aria-describedby="watchlist-help"
              />
              <button type="submit">加入自选</button>
            </div>
            <small id="watchlist-help">支持 6 位代码，也支持 sh/sz/bj 前缀和交易所后缀。</small>
            {watchlistError ? <span className={styles.formError} role="alert">{watchlistError}</span> : null}
          </form>

          <div className={styles.tableWrap}>
            <table className={styles.table}>
              <thead>
                <tr><th>股票</th><th>最新价</th><th>涨跌</th><th aria-label="操作" /></tr>
              </thead>
              <tbody>
                {portfolio.watchlist.map((code) => {
                  const quote = quotes[code];
                  return (
                    <tr key={code}>
                      <td><Link className={styles.stockLink} href={`/stocks/${code}`}><strong>{code}</strong></Link><small>{quote ? quoteTime(quote.timestamp) : '等待有效报价'}</small></td>
                      <td>{formatPrice(quote?.lastPrice)}</td>
                      <td className={quoteClass(quote?.changePct)}>{quote ? `${quote.changePct > 0 ? '+' : ''}${formatNumber(quote.changePct)}%` : '—'}</td>
                      <td><button className={styles.textButton} type="button" onClick={() => removeWatchlist(code)}>删除</button></td>
                    </tr>
                  );
                })}
                {portfolio.watchlist.length === 0 ? (
                  <tr><td className={styles.empty} colSpan={4}>还没有自选股，添加代码后会读取真实最新报价。</td></tr>
                ) : null}
              </tbody>
            </table>
          </div>
        </section>

        <section className={styles.panel} aria-labelledby="holdings-title">
          <div className={styles.panelHeader}>
            <div>
              <p className={styles.sectionKicker}>POSITIONS</p>
              <h2 id="holdings-title">持仓记录</h2>
            </div>
            <span className={styles.countBadge}>{portfolio.holdings.length}/{MAX_HOLDINGS}</span>
          </div>
          <form className={styles.holdingForm} onSubmit={saveHolding}>
            <div className={styles.field}>
              <label htmlFor="holding-code">代码</label>
              <input id="holding-code" value={holdingDraft.code} onChange={(event) => updateHoldingDraft('code', event.target.value)} placeholder="600519" inputMode="text" autoComplete="off" />
            </div>
            <div className={styles.field}>
              <label htmlFor="holding-quantity">数量（股）</label>
              <input id="holding-quantity" value={holdingDraft.quantity} onChange={(event) => updateHoldingDraft('quantity', event.target.value)} placeholder="100" inputMode="numeric" type="number" min="1" step="1" />
            </div>
            <div className={styles.field}>
              <label htmlFor="holding-cost">成本价（元）</label>
              <input id="holding-cost" value={holdingDraft.costPrice} onChange={(event) => updateHoldingDraft('costPrice', event.target.value)} placeholder="100.00" inputMode="decimal" type="number" min="0.01" step="0.01" />
            </div>
            <div className={styles.formActions}>
              <button type="submit">{editingHoldingId ? '保存修改' : '录入持仓'}</button>
              {editingHoldingId ? <button className={styles.secondaryButton} type="button" onClick={cancelEdit}>取消</button> : null}
            </div>
            {holdingError ? <span className={styles.formError} role="alert">{holdingError}</span> : null}
          </form>

          <div className={styles.tableWrap}>
            <table className={`${styles.table} ${styles.holdingsTable}`}>
              <thead>
                <tr><th>股票</th><th>数量 / 成本</th><th>最新价</th><th>市值</th><th>盈亏</th><th aria-label="操作" /></tr>
              </thead>
              <tbody>
                {portfolio.holdings.map((holding) => {
                  const quote = quotes[holding.code];
                  const valuation = calculateHoldingValuation(holding, quote);
                  return (
                    <tr key={holding.id}>
                      <td><Link className={styles.stockLink} href={`/stocks/${holding.code}`}><strong>{holding.code}</strong></Link><small>{quote ? quoteTime(quote.timestamp) : '等待有效报价'}</small></td>
                      <td>{formatNumber(holding.quantity, 0)}<small>成本 ¥{formatPrice(holding.costPrice)}</small></td>
                      <td>{formatPrice(quote?.lastPrice)}</td>
                      <td>{formatMoney(valuation.marketValue)}</td>
                      <td className={quoteClass(valuation.pnl)}>
                        {formatMoney(valuation.pnl)}
                        {valuation.pnlPct !== null ? <small>{valuation.pnlPct > 0 ? '+' : ''}{formatNumber(valuation.pnlPct)}%</small> : null}
                      </td>
                      <td className={styles.actionsCell}>
                        <button className={styles.textButton} type="button" onClick={() => editHolding(holding)}>编辑</button>
                        <button className={styles.textButtonDanger} type="button" onClick={() => removeHolding(holding.id)}>删除</button>
                      </td>
                    </tr>
                  );
                })}
                {portfolio.holdings.length === 0 ? (
                  <tr><td className={styles.empty} colSpan={6}>录入代码、数量和成本价后，这里会按真实最新价计算市值与盈亏。</td></tr>
                ) : null}
              </tbody>
            </table>
          </div>
        </section>
      </div>

      <footer className={styles.footer}>
        <span>{cloudAccount ? '云端数据 schema v1 · 当前设备保留用户缓存' : '本地数据 schema v1 · 清理浏览器数据会删除自选和持仓记录'}</span>
        <span>行情缺失时显示“—”，不会用模拟价格替代</span>
      </footer>

      {migrationPrompt ? (
        <div className={styles.modalBackdrop} role="presentation">
          <section
            className={styles.migrationModal}
            role="dialog"
            aria-modal="true"
            aria-labelledby="portfolio-migration-title"
          >
            <p className={styles.sectionKicker}>FIRST SYNC</p>
            <h2 id="portfolio-migration-title">发现当前浏览器里的自选和持仓</h2>
            <p>
              {migrationPrompt.email ? `账号 ${migrationPrompt.email} 已登录。` : '你已登录云端账号。'}
              是否把本机记录合并到云端？自选股会自动去重，持仓记录会全部保留。
            </p>
            <div className={styles.modalActions}>
              <button type="button" onClick={() => void mergeAnonymousPortfolio()} disabled={migrationBusy}>
                {migrationBusy ? '合并中…' : '合并到云端（推荐）'}
              </button>
              <button className={styles.secondaryButton} type="button" onClick={deferAnonymousPortfolio} disabled={migrationBusy}>
                暂不处理
              </button>
            </div>
          </section>
        </div>
      ) : null}
    </main>
  );
}
