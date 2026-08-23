import type {
  DailyMarketBar,
  MarketDataProvider,
  MarketSnapshot,
  MinuteMarketBar,
  StockInstrument,
} from './market-data-provider.ts';
import {
  chunked,
  formatDateDashed,
  isApproximateLimitUp,
  normalizeTicker,
  parseNumber,
  tencentTicker,
} from './provider-utils.ts';
import { getEastmoneyUniverse } from './eastmoney-provider.ts';

const QUOTE_URL = 'https://qt.gtimg.cn/q=';
const KLINE_URL = 'https://web.ifzq.gtimg.cn/appstock/app/kline/kline';
const MINUTE_URL = 'https://ifzq.gtimg.cn/appstock/app/kline/mkline';

type TencentKlineResponse = {
  code?: number;
  data?: Record<string, {
    day?: unknown[][];
    m1?: unknown[][];
  }>;
};

function decodeQuoteTimestamp(value: string): string {
  if (!/^\d{14}$/.test(value)) return new Date().toISOString();
  return `${value.slice(0, 4)}-${value.slice(4, 6)}-${value.slice(6, 8)}T${value.slice(8, 10)}:${value.slice(10, 12)}:${value.slice(12, 14)}+08:00`;
}

async function fetchQuoteChunk(codes: string[]): Promise<MarketSnapshot[]> {
  const response = await fetch(`${QUOTE_URL}${codes.map(tencentTicker).join(',')}`, {
    headers: { 'User-Agent': 'Mozilla/5.0 (compatible; ZhihengQuant/1.0)' },
    cache: 'no-store',
    signal: AbortSignal.timeout(12_000),
  });
  if (!response.ok) {
    throw new Error(`腾讯行情请求失败：HTTP ${response.status}`);
  }
  const bytes = await response.arrayBuffer();
  const body = new TextDecoder('gbk').decode(bytes);
  const requested = new Map(codes.map((code) => [tencentTicker(code), normalizeTicker(code)]));
  const snapshots: MarketSnapshot[] = [];

  for (const line of body.split(';')) {
    const normalizedLine = line.trim();
    const keyMatch = normalizedLine.match(/^v_([^=]+)=/);
    const valueMatch = normalizedLine.match(/"([\s\S]*)"/);
    if (!keyMatch || !valueMatch) continue;
    const fields = valueMatch[1].split('~');
    if (fields.length < 50) continue;
    const code = requested.get(keyMatch[1]) ?? fields[2];
    const lastPrice = parseNumber(fields[3]);
    const previousClose = parseNumber(fields[4]);
    const amountWan = parseNumber(fields[37]);
    const stale = amountWan === 0 && lastPrice === previousClose && lastPrice > 0;
    if (!code || stale || lastPrice <= 0 || previousClose <= 0) continue;
    snapshots.push({
      code,
      timestamp: decodeQuoteTimestamp(fields[30] ?? ''),
      lastPrice,
      previousClose,
      turnoverRatePct: parseNumber(fields[38]),
      totalMarketCapYuan: parseNumber(fields[45]) * 100_000_000,
      volumeRatio: parseNumber(fields[49]),
    });
  }
  return snapshots;
}

export class TencentMarketDataProvider implements MarketDataProvider {
  readonly name = 'a-stock-data / 腾讯财经';

  async getUniverse(_asOfDate: string): Promise<StockInstrument[]> {
    void _asOfDate;
    return getEastmoneyUniverse();
  }

  async getSnapshots(codes: string[]): Promise<MarketSnapshot[]> {
    const groups = chunked([...new Set(codes.map(normalizeTicker))], 80);
    const output: MarketSnapshot[] = [];
    const concurrency = 6;
    for (let index = 0; index < groups.length; index += concurrency) {
      const page = groups.slice(index, index + concurrency);
      const settled = await Promise.allSettled(page.map(fetchQuoteChunk));
      for (const result of settled) {
        if (result.status === 'fulfilled') output.push(...result.value);
      }
    }
    if (codes.length > 0 && output.length === 0) {
      throw new Error('腾讯行情未返回有效报价');
    }
    return output;
  }

  async getDailyBars(
    codes: string[],
    startDate: string,
    endDate: string,
  ): Promise<DailyMarketBar[]> {
    const results = await Promise.allSettled(codes.map(async (rawCode) => {
      const code = normalizeTicker(rawCode);
      const ticker = tencentTicker(code);
      const params = new URLSearchParams({ param: `${ticker},day,,,80` });
      const response = await fetch(`${KLINE_URL}?${params}`, {
        headers: {
          'User-Agent': 'Mozilla/5.0 (compatible; ZhihengQuant/1.0)',
          Referer: 'https://gu.qq.com/',
        },
        next: { revalidate: 900 },
        signal: AbortSignal.timeout(12_000),
      });
      if (!response.ok) throw new Error(`腾讯日线 ${code} 请求失败`);
      const payload = (await response.json()) as TencentKlineResponse;
      const rows = payload.data?.[ticker]?.day ?? [];
      return rows.flatMap((row, index) => {
        const date = String(row[0] ?? '');
        if (date < startDate || date > endDate) return [];
        const previousRow = rows[index - 1];
        const previousClose = parseNumber(previousRow?.[2]);
        const close = parseNumber(row[2]);
        return [{
          code,
          date,
          open: parseNumber(row[1]),
          close,
          high: parseNumber(row[3]),
          low: parseNumber(row[4]),
          previousClose,
          volume: parseNumber(row[5]),
          amountYuan: 0,
          isLimitUp: isApproximateLimitUp(code, close, previousClose),
        } satisfies DailyMarketBar];
      });
    }));
    return results.flatMap((result) => result.status === 'fulfilled' ? result.value : []);
  }

  async getMinuteBars(codes: string[], date: string): Promise<MinuteMarketBar[]> {
    const results = await Promise.allSettled(codes.map(async (rawCode) => {
      const code = normalizeTicker(rawCode);
      const ticker = tencentTicker(code);
      const params = new URLSearchParams({ param: `${ticker},m1,,320` });
      const response = await fetch(`${MINUTE_URL}?${params}`, {
        headers: {
          'User-Agent': 'Mozilla/5.0 (compatible; ZhihengQuant/1.0)',
          Referer: 'https://gu.qq.com/',
        },
        cache: 'no-store',
        signal: AbortSignal.timeout(12_000),
      });
      if (!response.ok) throw new Error(`腾讯分钟线 ${code} 请求失败`);
      const payload = (await response.json()) as TencentKlineResponse;
      const rows = payload.data?.[ticker]?.m1 ?? [];
      return rows.flatMap((row) => {
        const stamp = String(row[0] ?? '');
        if (stamp.length < 12 || formatDateDashed(stamp.slice(0, 8)) !== date) return [];
        const volume = parseNumber(row[5]);
        const open = parseNumber(row[1]);
        const close = parseNumber(row[2]);
        return [{
          code,
          date,
          time: `${stamp.slice(8, 10)}:${stamp.slice(10, 12)}`,
          open,
          close,
          high: parseNumber(row[3]),
          low: parseNumber(row[4]),
          volume,
          amountYuan: volume * 100 * ((open + close) / 2),
        } satisfies MinuteMarketBar];
      });
    }));
    return results.flatMap((result) => result.status === 'fulfilled' ? result.value : []);
  }
}
