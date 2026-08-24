import assert from 'node:assert/strict';
import test from 'node:test';
import {
  runLiveYangYongxingBacktest,
  type HistoricalBacktestDataProvider,
} from '../lib/backtest/live-yang-yongxing.ts';
import type {
  DailyMarketBar,
  MinuteMarketBar,
  StockInstrument,
} from '../lib/data/market-data-provider.ts';

const code = '002892';
const history: DailyMarketBar[] = Array.from({ length: 30 }, (_, index) => ({
  code,
  date: `2026-06-${String(index + 1).padStart(2, '0')}`,
  open: 10,
  high: index === 12 ? 11 : 10.1,
  low: 9.9,
  close: index === 12 ? 11 : 10,
  previousClose: 10,
  volume: 1_000,
  amountYuan: 1_000_000,
  isLimitUp: index === 12,
}));

const dailyBars: DailyMarketBar[] = [
  ...history,
  {
    code,
    date: '2026-07-01',
    open: 10,
    high: 10.7,
    low: 9.95,
    close: 10.4,
    previousClose: 10,
    volume: 2_000,
    amountYuan: 2_000_000,
    volumeRatio: 1.5,
    turnoverRatePct: 7,
    totalMarketCapYuan: 10_000_000_000,
  },
  ...[10.5, 10.6, 10.8, 10.9, 11].map((close, index) => ({
    code,
    date: `2026-07-0${index + 2}`,
    open: close,
    high: close,
    low: close,
    close,
    previousClose: index === 0 ? 10.4 : [10.5, 10.6, 10.8, 10.9][index - 1],
    volume: 1_000,
    amountYuan: 1_000_000,
  })),
];

const minuteBars: MinuteMarketBar[] = [
  { code, date: '2026-07-01', time: '14:29', open: 10.4, high: 10.5, low: 10.4, close: 10.45, volume: 100 },
  { code, date: '2026-07-01', time: '14:35', open: 10.45, high: 10.62, low: 10.51, close: 10.6, volume: 100 },
  { code, date: '2026-07-01', time: '14:40', open: 10.6, high: 10.68, low: 10.56, close: 10.64, volume: 100 },
  { code, date: '2026-07-01', time: '14:48', open: 10.64, high: 10.64, low: 10.52, close: 10.58, volume: 100 },
  { code, date: '2026-07-01', time: '15:00', open: 10.58, high: 10.66, low: 10.55, close: 10.63, volume: 100 },
];

test('真实回测先按日线粗筛，再加载分钟线并计算未来收益', async () => {
  const minuteRequests: string[] = [];
  const provider: HistoricalBacktestDataProvider = {
    async getUniverse(): Promise<StockInstrument[]> {
      return [{ code, name: '科力尔', exchange: 'SZ', isSt: false }];
    },
    async getDailyBars() {
      return dailyBars;
    },
    async getHistoricalMinuteBars(requestedCode, date) {
      minuteRequests.push(`${requestedCode}-${date}`);
      return minuteBars;
    },
  };

  const result = await runLiveYangYongxingBacktest({
    codes: [code],
    startDate: '2026-07-01',
    endDate: '2026-07-02',
    holdingTradingDays: 5,
  }, provider, { skipConfigurationCheck: true });

  assert.deepEqual(minuteRequests, ['002892-2026-07-01']);
  assert.equal(result.scannedTradingDays, 2);
  assert.equal(result.prefilteredDays, 1);
  assert.equal(result.totalSignals, 1);
  assert.equal(result.completedSignals, 1);
  assert.equal(result.signals[0]?.name, '科力尔');
  assert.equal(result.signals[0]?.signalPrice, 10.63);
  assert.equal(result.signals[0]?.exitPrice, 11);
  assert.equal(result.signals[0]?.returnPct, 3.48);
});
