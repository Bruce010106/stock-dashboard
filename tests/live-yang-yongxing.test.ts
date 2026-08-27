import assert from 'node:assert/strict';
import test from 'node:test';
import {
  LiveBacktestDataError,
  providerForTushareHealth,
  runLiveYangYongxingBacktest,
  selectDefaultProvider,
  type HistoricalBacktestDataProvider,
} from '../lib/backtest/live-yang-yongxing.ts';
import { SINA_PROVIDER_METADATA, SinaMarketDataProvider } from '../lib/data/sina-provider.ts';
import {
  checkTushareHealth,
  resetTushareHealthCache,
  TushareMarketDataProvider,
} from '../lib/data/tushare-provider.ts';
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

test('未配置 TUSHARE_TOKEN 时自动选择新浪免费数据源（不发起真实探测请求）', async () => {
  const original = process.env.TUSHARE_TOKEN;
  delete process.env.TUSHARE_TOKEN;
  resetTushareHealthCache();
  try {
    assert.ok((await selectDefaultProvider()) instanceof SinaMarketDataProvider);
  } finally {
    resetTushareHealthCache();
    if (original !== undefined) process.env.TUSHARE_TOKEN = original;
  }
});

test('providerForTushareHealth 依据健康探测结果（而非仅是否配置）选择数据源', () => {
  assert.ok(providerForTushareHealth(true) instanceof TushareMarketDataProvider);
  assert.ok(providerForTushareHealth(false) instanceof SinaMarketDataProvider);
});

test('健康探测结果在 TTL 窗口内被缓存，同一窗口内不会重复探测；坏 token 探测失败后选择新浪且错误信息已脱敏', async () => {
  // checkTushareHealth short-circuits to `{ healthy: false }` without calling
  // the prober when TUSHARE_TOKEN isn't configured, so a temporary token is
  // set here purely to exercise the injected-prober caching path in
  // isolation from real environment configuration.
  const original = process.env.TUSHARE_TOKEN;
  process.env.TUSHARE_TOKEN = 'test-token';
  resetTushareHealthCache();
  try {
    let calls = 0;
    const failingProber = async () => {
      calls += 1;
      throw new Error('Tushare 探测失败：https://api.tushare.pro/ 401 token 无效');
    };

    const first = await checkTushareHealth(failingProber, 60_000);
    const second = await checkTushareHealth(failingProber, 60_000);

    assert.equal(calls, 1, '第二次调用应命中缓存，不应重复触发探测');
    assert.equal(first.healthy, false);
    assert.equal(second.healthy, false);
    assert.ok(first.error && !first.error.includes('http'), '错误信息不应包含上游 URL');

    assert.ok(providerForTushareHealth(first.healthy) instanceof SinaMarketDataProvider);
  } finally {
    resetTushareHealthCache();
    if (original === undefined) delete process.env.TUSHARE_TOKEN;
    else process.env.TUSHARE_TOKEN = original;
  }
});

test('健康探测缓存过期后会重新探测', async () => {
  const original = process.env.TUSHARE_TOKEN;
  process.env.TUSHARE_TOKEN = 'test-token';
  resetTushareHealthCache();
  try {
    let calls = 0;
    const flakyProber = async () => {
      calls += 1;
      return calls > 1;
    };

    const first = await checkTushareHealth(flakyProber, 1);
    await new Promise((resolve) => setTimeout(resolve, 5));
    const second = await checkTushareHealth(flakyProber, 60_000);

    assert.equal(first.healthy, false);
    assert.equal(second.healthy, true);
    assert.equal(calls, 2, '缓存过期后应重新探测一次');
  } finally {
    resetTushareHealthCache();
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

// ---- Required-metrics-missing detection (undefined/NaN vs. real 0) ----

test('可扫描交易日均缺少总市值/量比/换手率时抛出 REQUIRED_METRICS_MISSING，而不是用 0 顶替', async () => {
  const barsWithoutMetrics: DailyMarketBar[] = [
    {
      code, date: '2026-06-30', open: 10, high: 10, low: 9.9, close: 10,
      previousClose: 10, volume: 1_000, amountYuan: 1_000_000,
    },
    {
      code, date: '2026-07-01', open: 10, high: 10.7, low: 9.95, close: 10.4,
      previousClose: 10, volume: 2_000, amountYuan: 2_000_000,
    },
  ];
  const provider = baseProvider({
    async getDailyBars() {
      return barsWithoutMetrics;
    },
  });

  await assert.rejects(
    runLiveYangYongxingBacktest({
      codes: [code],
      startDate: '2026-07-01',
      endDate: '2026-07-01',
      holdingTradingDays: 5,
    }, provider),
    (error: unknown) => {
      assert.ok(error instanceof LiveBacktestDataError);
      assert.equal(error.code, 'REQUIRED_METRICS_MISSING');
      assert.equal(error.status, 503);
      assert.ok(error.message.includes(code), '错误信息应包含规范化股票代码');
      assert.ok(!error.message.includes('http'), '错误信息不应泄露上游 URL');
      return true;
    },
  );
});

test('部分股票/日期缺少必需指标时仅跳过受影响交易日，并在 warnings 中列出代码与缺失天数，其余正常回测', async () => {
  const otherCode = '600000';
  const otherBars: DailyMarketBar[] = [
    {
      code: otherCode, date: '2026-06-30', open: 10, high: 10, low: 9.9, close: 10,
      previousClose: 10, volume: 1_000, amountYuan: 1_000_000,
    },
    {
      code: otherCode, date: '2026-07-01', open: 10, high: 10.7, low: 9.95, close: 10.4,
      previousClose: 10, volume: 2_000, amountYuan: 2_000_000,
    },
  ];
  const provider = baseProvider({
    async getUniverse(): Promise<StockInstrument[]> {
      return [
        { code, name: '科力尔', exchange: 'SZ', isSt: false },
        { code: otherCode, name: '浦发银行', exchange: 'SH', isSt: false },
      ];
    },
    async getDailyBars() {
      return [...dailyBars, ...otherBars];
    },
  });

  const result = await runLiveYangYongxingBacktest({
    codes: [code, otherCode],
    startDate: '2026-07-01',
    endDate: '2026-07-01',
    holdingTradingDays: 5,
  }, provider);

  assert.equal(result.totalSignals, 1, '未受影响的股票应照常产生信号');
  const warning = result.warnings.find((w) => w.includes('必需指标'));
  assert.ok(warning, '应包含缺失必需指标的警告');
  assert.ok(warning?.includes(otherCode), '警告应包含受影响的规范化股票代码');
  assert.ok(warning?.includes('1 个交易日'), '警告应包含缺失日期数量');
  assert.ok(!warning?.includes('http'), '警告不应泄露上游 URL');
});

test('总市值/量比/换手率真实取值为 0 时视为指标已获取（非缺失），按策略阈值正常判定为无信号', async () => {
  const zeroMetricBars: DailyMarketBar[] = [
    {
      code, date: '2026-06-30', open: 10, high: 10, low: 9.9, close: 10,
      previousClose: 10, volume: 1_000, amountYuan: 1_000_000,
    },
    {
      code, date: '2026-07-01', open: 10, high: 10.7, low: 9.95, close: 10.4,
      previousClose: 10, volume: 2_000, amountYuan: 2_000_000,
      volumeRatio: 0, turnoverRatePct: 0, totalMarketCapYuan: 0,
    },
  ];
  const provider = baseProvider({
    async getDailyBars() {
      return zeroMetricBars;
    },
  });

  const result = await runLiveYangYongxingBacktest({
    codes: [code],
    startDate: '2026-07-01',
    endDate: '2026-07-01',
    holdingTradingDays: 5,
  }, provider);

  assert.equal(result.scannedTradingDays, 1);
  assert.equal(result.prefilteredDays, 0, '量比为 0 未超过策略阈值，属于正常未通过粗筛，而非数据缺失');
  assert.equal(result.totalSignals, 0);
  assert.ok(!result.warnings.some((w) => w.includes('必需指标')), '真实 0 值不应被当作缺失指标告警');
});

// ---- Runtime fallback classification (Tushare upstream/5xx only) ----

test('Tushare 运行期失败且区间不超过 30 天时，自动切换新浪重跑并在 warnings 中脱敏说明', async () => {
  const originalGetUniverse = SinaMarketDataProvider.prototype.getUniverse;
  const originalGetDailyBars = SinaMarketDataProvider.prototype.getDailyBars;
  const originalGetMinuteBars = SinaMarketDataProvider.prototype.getHistoricalMinuteBars;
  // The runtime fallback constructs `new SinaMarketDataProvider()` internally
  // with no injection point, so the prototype is patched for the duration of
  // this test to avoid a real network call, then restored in `finally`.
  SinaMarketDataProvider.prototype.getUniverse = async function () {
    return [{ code, name: '科力尔', exchange: 'SZ', isSt: false }];
  };
  SinaMarketDataProvider.prototype.getDailyBars = async function () {
    return dailyBars;
  };
  SinaMarketDataProvider.prototype.getHistoricalMinuteBars = async function () {
    return minuteBars;
  };

  try {
    const failingTushareProvider = new TushareMarketDataProvider();
    failingTushareProvider.getUniverse = async () => {
      throw new Error('Tushare stock_basic：https://api.tushare.pro/ 权限不足');
    };
    failingTushareProvider.getDailyBars = async () => {
      throw new Error('Tushare daily：https://api.tushare.pro/ 权限不足');
    };

    const result = await runLiveYangYongxingBacktest({
      codes: [code],
      startDate: '2026-07-01',
      endDate: '2026-07-02',
      holdingTradingDays: 5,
    }, failingTushareProvider);

    assert.equal(result.source, SINA_PROVIDER_METADATA.source);
    assert.equal(result.isApproximate, true);
    const fallbackWarning = result.warnings.find((w) => w.includes('已自动切换到新浪财经免费近似源重跑'));
    assert.ok(fallbackWarning, '应包含自动降级说明');
    assert.ok(!fallbackWarning?.includes('http'), 'warning 不应包含上游 URL');
  } finally {
    SinaMarketDataProvider.prototype.getUniverse = originalGetUniverse;
    SinaMarketDataProvider.prototype.getDailyBars = originalGetDailyBars;
    SinaMarketDataProvider.prototype.getHistoricalMinuteBars = originalGetMinuteBars;
  }
});

test('Tushare 运行期失败但区间超过 30 天（新浪上限）时，直接返回 TUSHARE_UPSTREAM_UNAVAILABLE，不做静默降级', async () => {
  const failingTushareProvider = new TushareMarketDataProvider();
  failingTushareProvider.getUniverse = async () => {
    throw new Error('Tushare stock_basic：https://api.tushare.pro/ 服务异常');
  };
  failingTushareProvider.getDailyBars = async () => {
    throw new Error('Tushare daily：https://api.tushare.pro/ 服务异常');
  };

  await assert.rejects(
    runLiveYangYongxingBacktest({
      codes: [code],
      startDate: '2026-06-01',
      endDate: '2026-07-15',
      holdingTradingDays: 5,
    }, failingTushareProvider),
    (error: unknown) => {
      assert.ok(error instanceof LiveBacktestDataError);
      assert.equal(error.code, 'TUSHARE_UPSTREAM_UNAVAILABLE');
      assert.equal(error.status, 503);
      assert.ok(!error.message.includes('http'), '错误信息不应包含上游 URL');
      return true;
    },
  );
});

test('NO_VALID_CODES 是业务错误（4xx），即使数据源是 Tushare 也不会静默降级到新浪重跑', async () => {
  const provider = new TushareMarketDataProvider();
  provider.getUniverse = async () => [];
  provider.getDailyBars = async () => [];

  await assert.rejects(
    runLiveYangYongxingBacktest({
      codes: [code],
      startDate: '2026-07-01',
      endDate: '2026-07-02',
      holdingTradingDays: 5,
    }, provider),
    (error: unknown) => {
      assert.ok(error instanceof LiveBacktestDataError);
      assert.equal(error.code, 'NO_VALID_CODES');
      assert.equal(error.status, 422);
      // A retried run would report the Sina source instead of surfacing
      // this validation error directly.
      return true;
    },
  );
});
