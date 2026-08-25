import type { MarketSnapshot } from '../data/market-data-provider';

export const PORTFOLIO_STORAGE_KEY = 'zhiheng-quant:portfolio';
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
