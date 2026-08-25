import type { PortfolioHolding, PortfolioQuote } from './types.ts';

export type HoldingValuation = {
  holding: PortfolioHolding;
  investedValue: number;
  marketValue: number | null;
  pnl: number | null;
  pnlPct: number | null;
};

export type PortfolioTotals = {
  investedValue: number;
  marketValue: number | null;
  pnl: number | null;
  pnlPct: number | null;
  partialMarketValue: number;
  partialPnl: number;
  pricedCount: number;
  totalCount: number;
};

export function calculateHoldingValuation(
  holding: PortfolioHolding,
  quote?: PortfolioQuote,
): HoldingValuation {
  const investedValue = holding.quantity * holding.costPrice;
  if (!quote || !Number.isFinite(quote.lastPrice) || quote.lastPrice <= 0) {
    return {
      holding,
      investedValue,
      marketValue: null,
      pnl: null,
      pnlPct: null,
    };
  }

  const marketValue = holding.quantity * quote.lastPrice;
  const pnl = marketValue - investedValue;
  return {
    holding,
    investedValue,
    marketValue,
    pnl,
    pnlPct: investedValue > 0 ? (pnl / investedValue) * 100 : null,
  };
}

export function calculatePortfolioTotals(
  holdings: PortfolioHolding[],
  quotes: Readonly<Record<string, PortfolioQuote>>,
): PortfolioTotals {
  const valuations = holdings.map((holding) => calculateHoldingValuation(holding, quotes[holding.code]));
  const priced = valuations.filter((valuation) => valuation.marketValue !== null);
  const investedValue = valuations.reduce((sum, valuation) => sum + valuation.investedValue, 0);
  const partialMarketValue = priced.reduce((sum, valuation) => sum + (valuation.marketValue ?? 0), 0);
  const partialPnl = priced.reduce((sum, valuation) => sum + (valuation.pnl ?? 0), 0);
  const complete = priced.length === valuations.length;
  const pnl = complete ? partialPnl : null;
  return {
    investedValue,
    marketValue: complete ? partialMarketValue : null,
    pnl,
    pnlPct: complete && investedValue > 0 && pnl !== null ? (pnl / investedValue) * 100 : null,
    partialMarketValue,
    partialPnl,
    pricedCount: priced.length,
    totalCount: valuations.length,
  };
}
