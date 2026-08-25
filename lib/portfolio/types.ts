import type { MarketSnapshot } from '../data/market-data-provider';

export const PORTFOLIO_STORAGE_KEY = 'zhiheng-quant:portfolio';
/** A separate browser cache is kept for each authenticated account. */
export const PORTFOLIO_USER_STORAGE_PREFIX = 'zhiheng-quant:portfolio:user:';
/** Records whether the anonymous-browser import prompt was handled per account. */
export const PORTFOLIO_MIGRATION_STATUS_PREFIX = 'zhiheng-quant:portfolio:migration:';
export const PORTFOLIO_SCHEMA_VERSION = 1 as const;

/** Local-only limits keep a corrupted browser payload from becoming expensive. */
export const MAX_WATCHLIST_ITEMS = 50;
export const MAX_HOLDINGS = 100;
export const MAX_HOLDING_QUANTITY = 100_000_000;
export const MAX_HOLDING_COST_PRICE = 1_000_000;

export type PortfolioHolding = {
  id: string;
  code: string;
  quantity: number;
  costPrice: number;
};

export type PortfolioState = {
  schemaVersion: typeof PORTFOLIO_SCHEMA_VERSION;
  watchlist: string[];
  holdings: PortfolioHolding[];
};

export type PortfolioCloudResponse = {
  authenticated: boolean;
  userId: string | null;
  email: string | null;
  portfolio: PortfolioState;
};

export type PortfolioMigrationStatus = 'merged' | 'deferred';

export type PortfolioSyncStatus =
  | 'checking'
  | 'local'
  | 'syncing'
  | 'synced'
  | 'offline'
  | 'error';

export type PortfolioQuote = Pick<
  MarketSnapshot,
  | 'code'
  | 'timestamp'
  | 'lastPrice'
  | 'previousClose'
  | 'volumeRatio'
  | 'turnoverRatePct'
  | 'totalMarketCapYuan'
> & {
  changePct: number;
};

export type PortfolioQuotesResponse = {
  generatedAt: string;
  provider: string;
  snapshots: PortfolioQuote[];
  missingCodes: string[];
  degraded: boolean;
  warning?: string;
};
