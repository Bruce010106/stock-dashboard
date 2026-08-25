export type MiniBar = {
  date: string;
  open: number;
  high: number;
  low: number;
  close: number;
};

export type ScreenResult = {
  code: string;
  name: string;
  lastPrice?: number;
  changePct: number;
  totalMarketCapYuan: number;
  amountYuan?: number;
  volumeRatio: number;
  turnoverRatePct: number;
  miniBars?: MiniBar[];
  breakoutTime?: string;
  score: number;
  isSt?: boolean;
};

export type ScreenNearMiss = ScreenResult & {
  failedRuleLabel: string;
  reason: string;
};

export type ScreenerRow = ScreenResult & {
  conclusion: '严格命中' | '近似候选';
  reason: string;
};

export type ScreenerFilters = {
  query: string;
  minPrice: string;
  maxPrice: string;
  minChange: string;
  maxChange: string;
  minMarketCapYi: string;
  maxMarketCapYi: string;
  minAmountYi: string;
  maxAmountYi: string;
  excludeSt: boolean;
};

export const DEFAULT_SCREENER_FILTERS: ScreenerFilters = {
  query: '',
  minPrice: '',
  maxPrice: '',
  minChange: '',
  maxChange: '',
  minMarketCapYi: '',
  maxMarketCapYi: '',
  minAmountYi: '',
  maxAmountYi: '',
  excludeSt: true,
};
