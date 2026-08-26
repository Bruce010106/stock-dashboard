import assert from 'node:assert/strict';
import test from 'node:test';

import {
  buildDailyMarketBars,
  groupMinuteBarsByDate,
  parseSinaDailyPayload,
  parseSinaMinutePayload,
  SINA_PROVIDER_METADATA,
  SinaMarketDataProvider,
} from '../lib/data/sina-provider.ts';
import type { MarketSnapshot, StockInstrument } from '../lib/data/market-data-provider.ts';

type FetchArgs = Parameters<typeof fetch>;
type FetchHandler = (url: string, init?: FetchArgs[1]) => Promise<Response> | Response;

function textResponse(body: string, opts: { ok?: boolean; status?: number } = {}): Response {
  const ok = opts.ok ?? true;
  const status = opts.status ?? (ok ? 200 : 500);
  return {
    ok,
    status,
    text: async () => body,
  } as unknown as Response;
}

function installFetchMock(handler: FetchHandler): () => void {
  const original = globalThis.fetch;
  globalThis.fetch = (async (input: FetchArgs[0], init?: FetchArgs[1]) => {
    const url = typeof input === 'string' ? input : input.toString();
    return handler(url, init);
  }) as typeof fetch;
  return () => {
    globalThis.fetch = original;
  };
}

type DailyRow = { day: string; open: number; high: number; low: number; close: number; volume: number };

function dailyJsonpBody(ticker: string, rows: DailyRow[]): string {
  const items = rows.map((r) => (
    `{"day":"${r.day}","open":"${r.open}","high":"${r.high}","low":"${r.low}","close":"${r.close}","volume":"${r.volume}"}`
  )).join(',');
  return `var _${ticker}_240_1970=[${items}];`;
}

type MinuteRow = { day: string; open: number; high: number; low: number; close: number; volume: number };

function minuteJsonpBody(ticker: string, rows: MinuteRow[]): string {
  const items = rows.map((r) => (
    `{"day":"${r.day}","open":"${r.open}","high":"${r.high}","low":"${r.low}","close":"${r.close}","volume":"${r.volume}"}`
  )).join(',');
  return `var _${ticker}_5_1970=[${items}];`;
}

function snapshot(overrides: Partial<MarketSnapshot> = {}): MarketSnapshot {
  return {
    code: '600000',
    timestamp: '2024-01-05T15:00:00+08:00',
    lastPrice: 10,
    previousClose: 10,
    volumeRatio: 1,
    turnoverRatePct: 1,
    totalMarketCapYuan: 100_000_000_000,
    circulatingMarketCapYuan: 60_000_000_000,
    ...overrides,
  };
}

// ---- JSONP parsing ----

test('解析新浪日线 JSONP 载荷为结构化行', () => {
  const body = dailyJsonpBody('sh600000', [
    { day: '2024-01-02', open: 10, high: 10.2, low: 9.9, close: 10.1, volume: 1_000_000 },
    { day: '2024-01-03', open: 10.1, high: 10.3, low: 10, close: 10.2, volume: 1_200_000 },
  ]);
  const rows = parseSinaDailyPayload(body);
  assert.equal(rows.length, 2);
  assert.deepEqual(rows[0], { day: '2024-01-02', open: 10, high: 10.2, low: 9.9, close: 10.1, volume: 1_000_000 });
});

test('解析新浪5分钟线 JSONP 载荷，拆分日期与时间', () => {
  const body = minuteJsonpBody('sh600000', [
    { day: '2024-01-02 09:35:00', open: 10, high: 10.05, low: 9.98, close: 10.02, volume: 20_000 },
  ]);
  const rows = parseSinaMinutePayload(body);
  assert.equal(rows.length, 1);
  assert.equal(rows[0].date, '2024-01-02');
  assert.equal(rows[0].time, '09:35');
});

test('无法识别的响应体（如 null）解析时抛出错误', () => {
  assert.throws(() => parseSinaDailyPayload('null'), /响应格式无法识别/);
});

test('丢弃字段非正的记录', () => {
  const body = 'var _sh600000_240_1970=[{"day":"2024-01-02","open":"-1","high":"10","low":"9","close":"10","volume":"100"}];';
  const rows = parseSinaDailyPayload(body);
  assert.equal(rows.length, 0);
});

// ---- Daily derivations ----

test('日线派生：前收、涨停价、成交额估算', () => {
  const bars = buildDailyMarketBars('600001', [
    { day: '2024-01-02', open: 10, high: 10.1, low: 9.9, close: 10, volume: 1_000_000 },
    { day: '2024-01-03', open: 10, high: 11, low: 10, close: 11, volume: 2_000_000 },
  ], '2024-01-02', '2024-01-03');

  assert.equal(bars.length, 2);
  assert.equal(bars[0].previousClose, 0);
  assert.equal(bars[1].previousClose, 10);
  assert.equal(bars[1].isLimitUp, true);
  assert.equal(bars[1].limitUpPrice, 11);
  assert.ok(bars[0].amountYuan > 0);
  assert.equal(bars[0].amountYuan, 1_000_000 * ((10 + 10.1 + 9.9 + 10) / 4));
});

test('日线按起止日期过滤，但计算仍使用完整历史', () => {
  const bars = buildDailyMarketBars('600001', [
    { day: '2024-01-01', open: 10, high: 10, low: 10, close: 10, volume: 1_000_000 },
    { day: '2024-01-02', open: 10, high: 10.1, low: 9.9, close: 10.2, volume: 1_000_000 },
    { day: '2024-01-03', open: 10.2, high: 10.3, low: 10.1, close: 10.3, volume: 1_000_000 },
  ], '2024-01-02', '2024-01-02');

  assert.equal(bars.length, 1);
  assert.equal(bars[0].date, '2024-01-02');
  assert.equal(bars[0].previousClose, 10);
});

// ---- Market cap / turnover estimation ----

test('结合快照隐含股本估算历史总市值与换手率', () => {
  const bars = buildDailyMarketBars('600000', [
    { day: '2024-01-02', open: 9, high: 9.5, low: 8.9, close: 9, volume: 6_000_000 },
  ], '2024-01-02', '2024-01-02', snapshot());

  const impliedTotalShares = 100_000_000_000 / 10;
  const impliedCirculatingShares = 60_000_000_000 / 10;
  assert.equal(bars[0].totalMarketCapYuan, impliedTotalShares * 9);
  assert.equal(bars[0].turnoverRatePct, (6_000_000 / impliedCirculatingShares) * 100);
});

test('缺少估算所需输入时，市值与换手率保持未定义而非置零', () => {
  const noSnapshot = buildDailyMarketBars('600000', [
    { day: '2024-01-02', open: 9, high: 9.5, low: 8.9, close: 9, volume: 6_000_000 },
  ], '2024-01-02', '2024-01-02');
  assert.equal(noSnapshot[0].totalMarketCapYuan, undefined);
  assert.equal(noSnapshot[0].turnoverRatePct, undefined);

  const zeroPriceSnapshot = buildDailyMarketBars('600000', [
    { day: '2024-01-02', open: 9, high: 9.5, low: 8.9, close: 9, volume: 6_000_000 },
  ], '2024-01-02', '2024-01-02', snapshot({ lastPrice: 0 }));
  assert.equal(zeroPriceSnapshot[0].totalMarketCapYuan, undefined);
  assert.equal(zeroPriceSnapshot[0].turnoverRatePct, undefined);

  const noCirculating = buildDailyMarketBars('600000', [
    { day: '2024-01-02', open: 9, high: 9.5, low: 8.9, close: 9, volume: 6_000_000 },
  ], '2024-01-02', '2024-01-02', snapshot({ circulatingMarketCapYuan: undefined }));
  assert.ok(noCirculating[0].totalMarketCapYuan !== undefined);
  assert.equal(noCirculating[0].turnoverRatePct, undefined);
});

// ---- Rolling volume ratio ----

test('量比 = 当日成交量 / 最近5个有效交易日均量', () => {
  const rows = [
    { day: '2024-01-01', open: 10, high: 10, low: 10, close: 10, volume: 1_000_000 },
    { day: '2024-01-02', open: 10, high: 10, low: 10, close: 10, volume: 1_200_000 },
    { day: '2024-01-03', open: 10, high: 10, low: 10, close: 10, volume: 900_000 },
    { day: '2024-01-04', open: 10, high: 10, low: 10, close: 10, volume: 1_100_000 },
    { day: '2024-01-05', open: 10, high: 10, low: 10, close: 10, volume: 800_000 },
    { day: '2024-01-08', open: 10, high: 10, low: 10, close: 10, volume: 5_000_000 },
  ];
  const bars = buildDailyMarketBars('600000', rows, '2024-01-08', '2024-01-08');
  const avg = (1_000_000 + 1_200_000 + 900_000 + 1_100_000 + 800_000) / 5;
  assert.equal(bars[0].volumeRatio, 5_000_000 / avg);
});

test('首个交易日缺少历史成交量时，量比保持未定义', () => {
  const bars = buildDailyMarketBars('600000', [
    { day: '2024-01-02', open: 10, high: 10, low: 10, close: 10, volume: 1_000_000 },
  ], '2024-01-02', '2024-01-02');
  assert.equal(bars[0].volumeRatio, undefined);
});

// ---- 5-minute grouping ----

test('5分钟线按日期分组并按时间排序', () => {
  const grouped = groupMinuteBarsByDate('600000', [
    { date: '2024-01-02', time: '10:00', open: 10, high: 10.1, low: 9.9, close: 10, volume: 100 },
    { date: '2024-01-02', time: '09:35', open: 9.9, high: 10, low: 9.8, close: 9.95, volume: 100 },
    { date: '2024-01-03', time: '09:35', open: 10, high: 10.05, low: 9.95, close: 10, volume: 200 },
  ]);
  assert.deepEqual([...grouped.keys()].sort(), ['2024-01-02', '2024-01-03']);
  assert.deepEqual(grouped.get('2024-01-02')?.map((b) => b.time), ['09:35', '10:00']);
  assert.equal(grouped.get('2024-01-03')?.[0].amountYuan, 200 * ((10 + 10.05 + 9.95 + 10) / 4));
});

// ---- Provider-level: fetch mocking, caching, malformed payloads ----

test('getUniverse 委托给注入的股票池数据源', async () => {
  const instruments: StockInstrument[] = [{ code: '600000', name: '浦发银行', exchange: 'SH' }];
  const provider = new SinaMarketDataProvider({
    universe: { async getUniverse() { return instruments; } },
    snapshots: { async getSnapshots() { return []; } },
  });
  const result = await provider.getUniverse('2024-01-02');
  assert.deepEqual(result, instruments);
});

test('getDailyBars 通过 mocked fetch 获取并结合快照估算', async () => {
  const calls: string[] = [];
  const restore = installFetchMock((url) => {
    calls.push(url);
    assert.match(url, /scale=240/);
    return textResponse(dailyJsonpBody('sh600000', [
      { day: '2024-01-02', open: 9, high: 9.5, low: 8.9, close: 9, volume: 6_000_000 },
    ]));
  });

  const provider = new SinaMarketDataProvider({
    snapshots: { async getSnapshots() { return [snapshot()]; } },
  });
  const bars = await provider.getDailyBars(['600000'], '2024-01-02', '2024-01-02');
  restore();

  assert.equal(calls.length, 1);
  assert.equal(bars.length, 1);
  assert.equal(bars[0].code, '600000');
  assert.ok(bars[0].totalMarketCapYuan && bars[0].totalMarketCapYuan > 0);
});

test('部分股票返回畸形载荷时被丢弃，其余股票正常返回', async () => {
  const restore = installFetchMock((url) => {
    if (url.includes('symbol=sh600000')) {
      return textResponse('var _sh600000_240_1970=null;');
    }
    return textResponse(dailyJsonpBody('sz000001', [
      { day: '2024-01-02', open: 10, high: 10.1, low: 9.9, close: 10, volume: 500_000 },
    ]));
  });

  const provider = new SinaMarketDataProvider({
    snapshots: { async getSnapshots() { return []; } },
  });
  const bars = await provider.getDailyBars(['600000', '000001'], '2024-01-02', '2024-01-02');
  restore();

  assert.equal(bars.length, 1);
  assert.equal(bars[0].code, '000001');
});

test('快照获取失败时日线仍正常返回，仅估算字段缺失', async () => {
  const restore = installFetchMock(() => textResponse(dailyJsonpBody('sh600000', [
    { day: '2024-01-02', open: 9, high: 9.5, low: 8.9, close: 9, volume: 6_000_000 },
  ])));

  const provider = new SinaMarketDataProvider({
    snapshots: { async getSnapshots() { throw new Error('腾讯快照超时'); } },
  });
  const bars = await provider.getDailyBars(['600000'], '2024-01-02', '2024-01-02');
  restore();

  assert.equal(bars.length, 1);
  assert.equal(bars[0].totalMarketCapYuan, undefined);
});

test('5分钟线每个代码仅请求一次并按日期缓存', async () => {
  let requestCount = 0;
  const restore = installFetchMock((url) => {
    assert.match(url, /scale=5/);
    requestCount += 1;
    return textResponse(minuteJsonpBody('sh600000', [
      { day: '2024-01-02 09:35:00', open: 10, high: 10.1, low: 9.9, close: 10, volume: 100 },
      { day: '2024-01-03 09:35:00', open: 10.1, high: 10.2, low: 10, close: 10.1, volume: 150 },
    ]));
  });

  const provider = new SinaMarketDataProvider({
    snapshots: { async getSnapshots() { return []; } },
  });
  const day1 = await provider.getHistoricalMinuteBars('600000', '2024-01-02');
  const day2 = await provider.getHistoricalMinuteBars('600000', '2024-01-03');
  restore();

  assert.equal(requestCount, 1);
  assert.equal(day1.length, 1);
  assert.equal(day1[0].time, '09:35');
  assert.equal(day2.length, 1);
  assert.equal(day2[0].close, 10.1);
});

test('5分钟线请求失败后不会永久缓存，允许下次重试', async () => {
  let attempt = 0;
  const restore = installFetchMock(() => {
    attempt += 1;
    if (attempt === 1) return textResponse('', { ok: false, status: 502 });
    return textResponse(minuteJsonpBody('sh600000', [
      { day: '2024-01-02 09:35:00', open: 10, high: 10.1, low: 9.9, close: 10, volume: 100 },
    ]));
  });

  const provider = new SinaMarketDataProvider({
    snapshots: { async getSnapshots() { return []; } },
  });
  await assert.rejects(provider.getHistoricalMinuteBars('600000', '2024-01-02'));
  const bars = await provider.getHistoricalMinuteBars('600000', '2024-01-02');
  restore();

  assert.equal(attempt, 2);
  assert.equal(bars.length, 1);
});

// ---- Metadata ----

test('元数据标注近似口径、5分钟粒度与推荐区间', () => {
  assert.equal(SINA_PROVIDER_METADATA.accuracyMode, 'approximate-5m');
  assert.equal(SINA_PROVIDER_METADATA.maxRecommendedRangeDays, 30);
  assert.ok(SINA_PROVIDER_METADATA.warnings.some((w) => w.includes('5 分钟')));
  assert.ok(SINA_PROVIDER_METADATA.warnings.some((w) => w.includes('市值') || w.includes('换手率')));
});
