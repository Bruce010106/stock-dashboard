import assert from 'node:assert/strict';
import test from 'node:test';
import { runYangYongxingForwardBacktest } from '../lib/backtest/yang-yongxing-forward.ts';
import type { DailyBar, MinuteBar } from '../lib/strategies/yang-yongxing.ts';

const history: DailyBar[] = Array.from({ length: 30 }, (_, index) => ({
  date: `2026-06-${String(index + 1).padStart(2, '0')}`,
  close: 10,
  isLimitUp: index === 2,
}));
const minutes: MinuteBar[] = [
  { time: '14:29', high: 10, low: 9.9, close: 9.95 },
  { time: '14:35', high: 10.2, low: 10.01, close: 10.18 },
  { time: '14:45', high: 10.22, low: 10.05, close: 10.1 },
  { time: '15:00', high: 10.18, low: 10.04, close: 10.15 },
];

test('按指定持有交易日计算独立信号收益', () => {
  const result = runYangYongxingForwardBacktest([
    {
      signalDate: '2026-07-01',
      signalPrice: 10,
      candidate: { code: '000001', name: '示例', changePct: 4, totalMarketCapYuan: 10_000_000_000, volumeRatio: 1.5, turnoverRatePct: 7, recentDailyBars: history, minuteBars: minutes },
      futureCloses: [{ tradingDaysAfter: 5, close: 10.8 }],
    },
  ], 5);

  assert.equal(result.completedSignals, 1);
  assert.equal(result.averageReturnPct, 8);
  assert.equal(result.winRatePct, 100);
});
