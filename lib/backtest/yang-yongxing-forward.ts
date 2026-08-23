import {
  evaluateYangYongxing,
  type YangYongxingCandidate,
  type YangYongxingResult,
} from '../strategies/yang-yongxing.ts';

export type FutureClose = {
  tradingDaysAfter: number;
  close: number;
};

export type YangYongxingSignalEvent = {
  signalDate: string;
  candidate: YangYongxingCandidate;
  signalPrice: number;
  futureCloses: FutureClose[];
};

export type SignalReturn = {
  signalDate: string;
  code: string;
  name: string;
  signalPrice: number;
  exitPrice: number;
  returnPct: number;
  holdingTradingDays: number;
  evaluation: YangYongxingResult;
};

export type ForwardBacktestResult = {
  holdingTradingDays: number;
  totalSignals: number;
  completedSignals: number;
  averageReturnPct: number;
  medianReturnPct: number;
  winRatePct: number;
  bestReturnPct: number;
  worstReturnPct: number;
  signals: SignalReturn[];
};

function rounded(value: number): number {
  return Math.round(value * 100) / 100;
}

function median(values: number[]): number {
  if (values.length === 0) return 0;
  const sorted = [...values].sort((a, b) => a - b);
  const middle = Math.floor(sorted.length / 2);
  return sorted.length % 2 === 0
    ? (sorted[middle - 1] + sorted[middle]) / 2
    : sorted[middle];
}

export function runYangYongxingForwardBacktest(
  events: YangYongxingSignalEvent[],
  holdingTradingDays: number,
): ForwardBacktestResult {
  if (!Number.isInteger(holdingTradingDays) || holdingTradingDays <= 0) {
    throw new Error('holdingTradingDays 必须是正整数');
  }

  const signals: SignalReturn[] = [];

  for (const event of events) {
    const evaluation = evaluateYangYongxing(event.candidate);
    if (!evaluation.passed || event.signalPrice <= 0) continue;

    const exit = event.futureCloses.find(
      (item) => item.tradingDaysAfter === holdingTradingDays,
    );
    if (!exit || exit.close <= 0) continue;

    signals.push({
      signalDate: event.signalDate,
      code: event.candidate.code,
      name: event.candidate.name,
      signalPrice: event.signalPrice,
      exitPrice: exit.close,
      returnPct: rounded((exit.close / event.signalPrice - 1) * 100),
      holdingTradingDays,
      evaluation,
    });
  }

  const returns = signals.map((signal) => signal.returnPct);
  const average = returns.length
    ? returns.reduce((sum, value) => sum + value, 0) / returns.length
    : 0;
  const winners = returns.filter((value) => value > 0).length;

  return {
    holdingTradingDays,
    totalSignals: events.length,
    completedSignals: signals.length,
    averageReturnPct: rounded(average),
    medianReturnPct: rounded(median(returns)),
    winRatePct: rounded(returns.length ? (winners / returns.length) * 100 : 0),
    bestReturnPct: rounded(returns.length ? Math.max(...returns) : 0),
    worstReturnPct: rounded(returns.length ? Math.min(...returns) : 0),
    signals,
  };
}
