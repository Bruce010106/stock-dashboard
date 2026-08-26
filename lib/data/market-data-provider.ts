export type StockInstrument = {
  code: string;
  name: string;
  exchange: 'SH' | 'SZ' | 'BJ';
  listingDate?: string;
  delistingDate?: string;
  isSt?: boolean;
};

export type DailyMarketBar = {
  code: string;
  date: string;
  open: number;
  high: number;
  low: number;
  close: number;
  previousClose: number;
  volume: number;
  amountYuan: number;
  volumeRatio?: number;
  turnoverRatePct?: number;
  totalMarketCapYuan?: number;
  limitUpPrice?: number;
  isLimitUp?: boolean;
};

export type MinuteMarketBar = {
  code: string;
  date: string;
  time: string;
  open: number;
  high: number;
  low: number;
  close: number;
  volume: number;
  amountYuan?: number;
};

export type MarketSnapshot = {
  code: string;
  name?: string;
  timestamp: string;
  lastPrice: number;
  previousClose: number;
  volumeRatio: number;
  turnoverRatePct: number;
  totalMarketCapYuan: number;
  circulatingMarketCapYuan?: number;
};

export interface MarketDataProvider {
  readonly name: string;
  getUniverse(asOfDate: string): Promise<StockInstrument[]>;
  getDailyBars(
    codes: string[],
    startDate: string,
    endDate: string,
  ): Promise<DailyMarketBar[]>;
  getMinuteBars(codes: string[], date: string): Promise<MinuteMarketBar[]>;
  getSnapshots(codes: string[]): Promise<MarketSnapshot[]>;
}

/**
 * Lives here (rather than in lib/backtest/live-yang-yongxing.ts) so data
 * providers can implement it without importing from the backtest module,
 * which would create a circular import.
 */
export interface HistoricalBacktestDataProvider {
  getUniverse(asOfDate: string): Promise<StockInstrument[]>;
  getDailyBars(codes: string[], startDate: string, endDate: string): Promise<DailyMarketBar[]>;
  getHistoricalMinuteBars(code: string, date: string): Promise<MinuteMarketBar[]>;
}

export const A_STOCK_DATA_FIELD_CONTRACT = {
  universe: ['code', 'name', 'exchange', 'listingDate', 'delistingDate', 'isSt'],
  daily: ['open', 'high', 'low', 'close', 'previousClose', 'volume', 'amountYuan', 'turnoverRatePct', 'totalMarketCapYuan', 'limitUpPrice'],
  minute: ['date', 'time', 'open', 'high', 'low', 'close', 'volume', 'amountYuan'],
  snapshot: ['lastPrice', 'previousClose', 'volumeRatio', 'turnoverRatePct', 'totalMarketCapYuan'],
} as const;
