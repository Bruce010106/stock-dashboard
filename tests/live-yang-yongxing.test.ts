import assert from 'node:assert/strict';
import test from 'node:test';
import {
  LiveBacktestDataError,
  runLiveYangYongxingBacktest,
  selectDefaultProvider,
  type HistoricalBacktestDataProvider,
} from '../lib/backtest/live-yang-yongxing.ts';
import { SINA_PROVIDER_METADATA, SinaMarketDataProvider } from '../lib/data/sina-provider.ts';
import { TushareMarketDataProvider } from '../lib/data/tushare-provider.ts';
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

function baseProvider(overrides: Partial<HistoricalBacktestDataProvider> = {}): HistoricalBacktestDataProvider {
  return {
    async getUniverse(): Promise<StockInstrument[]> {
      return [{ code, name: '科力尔', exchange: 'SZ', isSt: false }];
    },
    async getDailyBars() {
      return dailyBars;
    },
    async getHistoricalMinuteBars() {
      return minuteBars;
    },
    ...overrides,
  };
}

test('真实回测先按日线粗筛，再加载分钟线并计算未来收益（显式注入的数据源无需环境配置）', async () => {
  const minuteRequests: string[] = [];
  const provider = baseProvider({
    async getHistoricalMinuteBars(requestedCode, date) {
      minuteRequests.push(`${requestedCode}-${date}`);
      return minuteBars;
    },
  });

  const result = await runLiveYangYongxingBacktest({
    codes: [code],
    startDate: '2026-07-01',
    endDate: '2026-07-02',
    holdingTradingDays: 5,
  }, provider);

  assert.deepEqual(minuteRequests, ['002892-2026-07-01']);
  assert.equal(result.scannedTradingDays, 2);
  assert.equal(result.prefilteredDays, 1);
  assert.equal(result.totalSignals, 1);
  assert.equal(result.completedSignals, 1);
  assert.equal(result.signals[0]?.name, '科力尔');
  assert.equal(result.signals[0]?.signalPrice, 10.63);
  assert.equal(result.signals[0]?.exitPrice, 11);
  assert.equal(result.signals[0]?.returnPct, 3.48);

  // A plain test double is treated as exact/point-in-time by default, since
  // it isn't a recognized approximate provider.
  assert.equal(result.source, 'Tushare Pro');
  assert.equal(result.accuracyMode, 'point-in-time-1m');
  assert.equal(result.isApproximate, false);
  assert.equal(result.maxRangeDays, 90);
});

// ---- Provider auto-selection ----

test('未配置 TUSHARE_TOKEN 时自动选择新浪免费数据源', () => {
  const original = process.env.TUSHARE_TOKEN;
  delete process.env.TUSHARE_TOKEN;
  try {
    assert.ok(selectDefaultProvider() instanceof SinaMarketDataProvider);
  } finally {
    if (original !== undefined) process.env.TUSHARE_TOKEN = original;
  }
});

test('配置 TUSHARE_TOKEN 时自动选择 Tushare 精确数据源', () => {
  const original = process.env.TUSHARE_TOKEN;
  process.env.TUSHARE_TOKEN = 'unit-test-token';
  try {
    assert.ok(selectDefaultProvider() instanceof TushareMarketDataProvider);
  } finally {
    if (original === undefined) delete process.env.TUSHARE_TOKEN;
    else process.env.TUSHARE_TOKEN = original;
  }
});

// ---- Free (Sina) mode metadata and warnings ----

test('注入真实 SinaMarketDataProvider 实例时返回近似口径元数据与全部新浪警告', async () => {
  const sinaProvider = new SinaMarketDataProvider({
    universe: { async getUniverse() { return [{ code, name: '科力尔', exchange: 'SZ', isSt: false }]; } },
    snapshots: { async getSnapshots() { return []; } },
  });
  // Bypass the provider's own network fetch by stubbing its public methods
  // directly; SinaMarketDataProvider's own parsing/estimation logic is
  // covered separately in sina-provider.test.ts.
  sinaProvider.getDailyBars = async () => dailyBars;
  sinaProvider.getHistoricalMinuteBars = async () => minuteBars;

  const result = await runLiveYangYongxingBacktest({
    codes: [code],
    startDate: '2026-07-01',
    endDate: '2026-07-02',
    holdingTradingDays: 5,
  }, sinaProvider);

  assert.equal(result.source, SINA_PROVIDER_METADATA.source);
  assert.equal(result.accuracyMode, 'approximate-5m');
  assert.equal(result.isApproximate, true);
  assert.equal(result.maxRangeDays, 30);
  for (const warning of SINA_PROVIDER_METADATA.warnings) {
    assert.ok(result.warnings.includes(warning), `缺少新浪口径警告：${warning}`);
  }
  assert.ok(result.warnings.some((w) => w.includes('ST')));
});

// ---- Unusable free data detection ----

test('所选股票在区间内均无日线数据时抛出 NO_DAILY_DATA，而不是静默返回 0 信号', async () => {
  const provider = baseProvider({
    async getDailyBars() {
      return [];
    },
  });

  await assert.rejects(
    runLiveYangYongxingBacktest({
      codes: [code],
      startDate: '2026-07-01',
      endDate: '2026-07-02',
      holdingTradingDays: 5,
    }, provider),
    (error: unknown) => {
      assert.ok(error instanceof LiveBacktestDataError);
      assert.equal(error.code, 'NO_DAILY_DATA');
      assert.equal(error.status, 503);
      return true;
    },
  );
});

test('所有粗筛候选日均缺少分钟线时抛出 SIGNAL_MINUTE_DATA_MISSING，而不是静默返回 0 信号', async () => {
  const provider = baseProvider({
    async getHistoricalMinuteBars() {
      return [];
    },
  });

  await assert.rejects(
    runLiveYangYongxingBacktest({
      codes: [code],
      startDate: '2026-07-01',
      endDate: '2026-07-02',
      holdingTradingDays: 5,
    }, provider),
    (error: unknown) => {
      assert.ok(error instanceof LiveBacktestDataError);
      assert.equal(error.code, 'SIGNAL_MINUTE_DATA_MISSING');
      assert.equal(error.status, 503);
      return true;
    },
  );
});

test('部分股票日线缺失时仅产生命名标准化代码的警告，不中断整体回测', async () => {
  const otherCode = '600000';
  const provider = baseProvider({
    async getUniverse(): Promise<StockInstrument[]> {
      return [
        { code, name: '科力尔', exchange: 'SZ', isSt: false },
        { code: otherCode, name: '浦发银行', exchange: 'SH', isSt: false },
      ];
    },
    async getDailyBars() {
      // otherCode intentionally returns no bars, simulating an upstream
      // per-code failure that was swallowed by Promise.allSettled.
      return dailyBars;
    },
  });

  const result = await runLiveYangYongxingBacktest({
    codes: [code, otherCode],
    startDate: '2026-07-01',
    endDate: '2026-07-02',
    holdingTradingDays: 5,
  }, provider);

  const warning = result.warnings.find((w) => w.includes(otherCode));
  assert.ok(warning, '应包含标准化代码的警告');
  assert.ok(!warning?.includes('http'), '警告不应包含上游 URL');
});
