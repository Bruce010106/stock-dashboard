import type {
  DailyMarketBar,
  HistoricalBacktestDataProvider,
  MarketSnapshot,
  MinuteMarketBar,
  StockInstrument,
} from './market-data-provider.ts';
import {
  inferLimitRate,
  isApproximateLimitUp,
  normalizeTicker,
  parseNumber,
  tencentTicker,
} from './provider-utils.ts';
import { TencentMarketDataProvider } from './tencent-provider.ts';

const KLINE_JSONP_BASE = 'https://quotes.sina.cn/cn/api/jsonp_v2.php';
const KLINE_API = 'CN_MarketDataService.getKLineData';
const DAILY_SCALE = 240;
const MINUTE_SCALE = 5;
const DAILY_DATALEN = 1_970;
const MINUTE_DATALEN = 1_970;
const MAX_JSONP_BODY_LENGTH = 4_000_000;
const VOLUME_RATIO_LOOKBACK_DAYS = 5;
const FETCH_TIMEOUT_MS = 12_000;

export const SINA_PROVIDER_METADATA = {
  source: '新浪财经 K 线（免费源，无 Token）',
  accuracyMode: 'approximate-5m',
  maxRecommendedRangeDays: 30,
  warnings: [
    '分钟线粒度为 5 分钟（非 1 分钟），仅可近似复现日内走势',
    '历史总市值 / 换手率按当前腾讯快照隐含的总股本 / 流通股本静态外推，不反映历史股本变化',
    '量比按最近 5 个有效交易日成交量均值估算，与交易所公布口径存在差异',
    '成交额由当日 OHLC 均价 × 成交量估算，非真实逐笔成交口径',
  ],
} as const;

type SinaRawDailyBar = {
  day: string;
  open: number;
  high: number;
  low: number;
  close: number;
  volume: number;
};

type SinaRawMinuteBar = {
  date: string;
  time: string;
  open: number;
  high: number;
  low: number;
  close: number;
  volume: number;
};

export type SinaSnapshotSource = {
  getSnapshots(codes: string[]): Promise<MarketSnapshot[]>;
};

export type SinaUniverseSource = {
  getUniverse(asOfDate: string): Promise<StockInstrument[]>;
};

const defaultTencentProvider = new TencentMarketDataProvider();

function sanitizeError(error: unknown, context: string): Error {
  const message = error instanceof Error ? error.message : '未知错误';
  const cleaned = message.replace(/https?:\/\/\S+/gi, '[地址已隐藏]').slice(0, 200);
  return new Error(`${context}失败：${cleaned}`);
}

function roundPrice(value: number): number {
  return Math.round(value * 100) / 100;
}

function buildKlineUrl(ticker: string, scale: number, datalen: number): string {
  const callback = `var%20_${ticker}_${scale}_${datalen}=`;
  const params = new URLSearchParams({
    symbol: ticker,
    scale: String(scale),
    ma: 'no',
    datalen: String(datalen),
  });
  return `${KLINE_JSONP_BASE}/${callback}/${KLINE_API}?${params}`;
}

async function fetchSinaKlineBody(ticker: string, scale: number, datalen: number): Promise<string> {
  const url = buildKlineUrl(ticker, scale, datalen);
  const response = await fetch(url, {
    headers: {
      'User-Agent': 'Mozilla/5.0 (compatible; ZhihengQuant/1.0)',
      Referer: 'https://finance.sina.com.cn/',
    },
    cache: 'no-store',
    signal: AbortSignal.timeout(FETCH_TIMEOUT_MS),
  });
  if (!response.ok) {
    throw new Error(`HTTP ${response.status}`);
  }
  const body = await response.text();
  if (body.length > MAX_JSONP_BODY_LENGTH) {
    throw new Error('响应体积异常偏大');
  }
  return body;
}

/**
 * Extracts the JSONP callback's array payload by locating the outer
 * brackets textually and never evaluating the response as script.
 */
function extractArrayLiteral(body: string): string {
  const start = body.indexOf('[');
  const end = body.lastIndexOf(']');
  if (start === -1 || end === -1 || end < start) {
    throw new Error('响应格式无法识别');
  }
  return body.slice(start, end + 1);
}

function splitRecords(arrayLiteral: string): string[] {
  return arrayLiteral.match(/\{[^{}]*\}/g) ?? [];
}

function extractField(record: string, field: string): string | undefined {
  const pattern = new RegExp(`"?${field}"?\\s*:\\s*(?:"([^"]*)"|([\\d.\\-]+))`);
  const match = pattern.exec(record);
  if (!match) return undefined;
  return match[1] ?? match[2];
}

function parseRawBarFields(record: string): {
  day: string;
  open: number;
  high: number;
  low: number;
  close: number;
  volume: number;
} | undefined {
  const day = extractField(record, 'day');
  if (!day) return undefined;
  const open = parseNumber(extractField(record, 'open'));
  const high = parseNumber(extractField(record, 'high'));
  const low = parseNumber(extractField(record, 'low'));
  const close = parseNumber(extractField(record, 'close'));
  const volume = parseNumber(extractField(record, 'volume'));
  if (open <= 0 || high <= 0 || low <= 0 || close <= 0 || volume < 0) return undefined;
  return { day, open, high, low, close, volume };
}

export function parseSinaDailyPayload(body: string): SinaRawDailyBar[] {
  const records = splitRecords(extractArrayLiteral(body));
  return records.flatMap((record) => {
    const parsed = parseRawBarFields(record);
    if (!parsed || !/^\d{4}-\d{2}-\d{2}$/.test(parsed.day)) return [];
    return [parsed];
  });
}

export function parseSinaMinutePayload(body: string): SinaRawMinuteBar[] {
  const records = splitRecords(extractArrayLiteral(body));
  return records.flatMap((record) => {
    const parsed = parseRawBarFields(record);
    if (!parsed) return [];
    const match = /^(\d{4}-\d{2}-\d{2})[ T](\d{2}):(\d{2})/.exec(parsed.day);
    if (!match) return [];
    return [{
      date: match[1],
      time: `${match[2]}:${match[3]}`,
      open: parsed.open,
      high: parsed.high,
      low: parsed.low,
      close: parsed.close,
      volume: parsed.volume,
    }];
  });
}

async function fetchSinaDaily(code: string): Promise<SinaRawDailyBar[]> {
  try {
    const ticker = tencentTicker(code);
    const body = await fetchSinaKlineBody(ticker, DAILY_SCALE, DAILY_DATALEN);
    const bars = parseSinaDailyPayload(body);
    if (bars.length === 0) throw new Error('返回空数据或格式无法识别');
    return bars;
  } catch (error) {
    throw sanitizeError(error, `新浪日线 ${code} 获取`);
  }
}

async function fetchSinaMinute(code: string): Promise<SinaRawMinuteBar[]> {
  try {
    const ticker = tencentTicker(code);
    const body = await fetchSinaKlineBody(ticker, MINUTE_SCALE, MINUTE_DATALEN);
    const bars = parseSinaMinutePayload(body);
    if (bars.length === 0) throw new Error('返回空数据或格式无法识别');
    return bars;
  } catch (error) {
    throw sanitizeError(error, `新浪 5 分钟线 ${code} 获取`);
  }
}

function estimateAmountYuan(bar: { open: number; high: number; low: number; close: number; volume: number }): number {
  return bar.volume * ((bar.open + bar.high + bar.low + bar.close) / 4);
}

/**
 * Pure builder so the rolling volume-ratio and market-cap/turnover
 * estimates can be unit tested without mocking fetch.
 */
export function buildDailyMarketBars(
  code: string,
  rawBars: SinaRawDailyBar[],
  startDate: string,
  endDate: string,
  snapshot?: MarketSnapshot,
): DailyMarketBar[] {
  const sorted = [...rawBars].sort((a, b) => a.day.localeCompare(b.day));
  const impliedTotalShares = snapshot && snapshot.totalMarketCapYuan > 0 && snapshot.lastPrice > 0
    ? snapshot.totalMarketCapYuan / snapshot.lastPrice
    : undefined;
  const impliedCirculatingShares = snapshot?.circulatingMarketCapYuan
    && snapshot.circulatingMarketCapYuan > 0 && snapshot.lastPrice > 0
    ? snapshot.circulatingMarketCapYuan / snapshot.lastPrice
    : undefined;

  const output: DailyMarketBar[] = [];
  for (let index = 0; index < sorted.length; index += 1) {
    const bar = sorted[index];
    if (bar.day < startDate || bar.day > endDate) continue;

    const previousClose = index > 0 ? sorted[index - 1].close : 0;
    const priorVolumes = sorted
      .slice(Math.max(0, index - VOLUME_RATIO_LOOKBACK_DAYS), index)
      .map((item) => item.volume)
      .filter((volume) => volume > 0);
    const volumeRatio = priorVolumes.length > 0
      ? bar.volume / (priorVolumes.reduce((sum, value) => sum + value, 0) / priorVolumes.length)
      : undefined;
    const totalMarketCapYuan = impliedTotalShares !== undefined && bar.close > 0
      ? impliedTotalShares * bar.close
      : undefined;
    const turnoverRatePct = impliedCirculatingShares !== undefined && bar.volume > 0
      ? (bar.volume / impliedCirculatingShares) * 100
      : undefined;
    const limitUpPrice = previousClose > 0
      ? roundPrice(previousClose * (1 + inferLimitRate(code)))
      : undefined;

    output.push({
      code,
      date: bar.day,
      open: bar.open,
      high: bar.high,
      low: bar.low,
      close: bar.close,
      previousClose,
      volume: bar.volume,
      amountYuan: estimateAmountYuan(bar),
      volumeRatio,
      turnoverRatePct,
      totalMarketCapYuan,
      limitUpPrice,
      isLimitUp: isApproximateLimitUp(code, bar.close, previousClose),
    });
  }
  return output;
}

export function groupMinuteBarsByDate(
  code: string,
  rawBars: SinaRawMinuteBar[],
): Map<string, MinuteMarketBar[]> {
  const grouped = new Map<string, MinuteMarketBar[]>();
  for (const bar of rawBars) {
    const entry: MinuteMarketBar = {
      code,
      date: bar.date,
      time: bar.time,
      open: bar.open,
      high: bar.high,
      low: bar.low,
      close: bar.close,
      volume: bar.volume,
      amountYuan: estimateAmountYuan(bar),
    };
    const list = grouped.get(bar.date) ?? [];
    list.push(entry);
    grouped.set(bar.date, list);
  }
  for (const list of grouped.values()) {
    list.sort((a, b) => a.time.localeCompare(b.time));
  }
  return grouped;
}

/**
 * No-token, approximate historical backtest data source. Daily bars come
 * from Sina's free JSONP kline endpoint; historical total market cap,
 * turnover and volume ratio are estimated by extrapolating the current
 * Tencent snapshot's implied share counts backward — they are NOT
 * point-in-time accurate. See SINA_PROVIDER_METADATA.warnings.
 */
export class SinaMarketDataProvider implements HistoricalBacktestDataProvider {
  readonly source = SINA_PROVIDER_METADATA.source;
  readonly accuracyMode = SINA_PROVIDER_METADATA.accuracyMode;
  readonly maxRecommendedRangeDays = SINA_PROVIDER_METADATA.maxRecommendedRangeDays;
  readonly warnings = SINA_PROVIDER_METADATA.warnings;

  private readonly snapshotSource: SinaSnapshotSource;
  private readonly universeSource: SinaUniverseSource;
  private readonly minuteCache = new Map<string, Promise<Map<string, MinuteMarketBar[]>>>();

  constructor(deps: { snapshots?: SinaSnapshotSource; universe?: SinaUniverseSource } = {}) {
    this.snapshotSource = deps.snapshots ?? defaultTencentProvider;
    this.universeSource = deps.universe ?? defaultTencentProvider;
  }

  async getUniverse(asOfDate: string): Promise<StockInstrument[]> {
    return this.universeSource.getUniverse(asOfDate);
  }

  async getDailyBars(codes: string[], startDate: string, endDate: string): Promise<DailyMarketBar[]> {
    const normalizedCodes = [...new Set(codes.map(normalizeTicker))];
    const snapshotByCode = await this.loadSnapshots(normalizedCodes);
    const results = await Promise.allSettled(normalizedCodes.map(async (code) => {
      const raw = await fetchSinaDaily(code);
      return buildDailyMarketBars(code, raw, startDate, endDate, snapshotByCode.get(code));
    }));
    return results.flatMap((result) => (result.status === 'fulfilled' ? result.value : []));
  }

  async getHistoricalMinuteBars(rawCode: string, date: string): Promise<MinuteMarketBar[]> {
    const code = normalizeTicker(rawCode);
    let cached = this.minuteCache.get(code);
    if (!cached) {
      cached = fetchSinaMinute(code).then((raw) => groupMinuteBarsByDate(code, raw));
      this.minuteCache.set(code, cached);
    }
    try {
      const grouped = await cached;
      return grouped.get(date) ?? [];
    } catch (error) {
      this.minuteCache.delete(code);
      throw error;
    }
  }

  private async loadSnapshots(codes: string[]): Promise<Map<string, MarketSnapshot>> {
    try {
      const snapshots = await this.snapshotSource.getSnapshots(codes);
      return new Map(snapshots.map((snapshot) => [normalizeTicker(snapshot.code), snapshot]));
    } catch {
      return new Map();
    }
  }
}
