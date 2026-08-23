import type {
  DailyMarketBar,
  MarketDataProvider,
  MarketSnapshot,
  MinuteMarketBar,
  StockInstrument,
} from './market-data-provider.ts';
import {
  exchangeOf,
  formatDateCompact,
  formatDateDashed,
  isApproximateLimitUp,
  isStName,
  normalizeTicker,
  parseNumber,
  tushareTicker,
} from './provider-utils.ts';

const DEFAULT_TUSHARE_URL = 'https://api.tushare.pro';

type TushareResponse = {
  code: number;
  msg?: string | null;
  data?: { fields?: string[]; items?: unknown[][] };
};

type TushareRow = Record<string, unknown>;

function getToken(): string {
  const token = process.env.TUSHARE_TOKEN?.trim();
  if (!token) throw new Error('尚未配置 TUSHARE_TOKEN');
  return token;
}

function getApiUrl(): string {
  const configured = process.env.TUSHARE_API_URL?.trim() || DEFAULT_TUSHARE_URL;
  const url = new URL(configured);
  if (url.protocol !== 'https:') {
    throw new Error('TUSHARE_API_URL 必须使用 HTTPS');
  }
  return url.toString();
}

async function query(
  apiName: string,
  params: Record<string, string>,
  fields: string[],
): Promise<TushareRow[]> {
  const response = await fetch(getApiUrl(), {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      api_name: apiName,
      token: getToken(),
      params,
      fields: fields.join(','),
    }),
    cache: 'no-store',
    signal: AbortSignal.timeout(20_000),
  });
  if (!response.ok) throw new Error(`Tushare ${apiName}：HTTP ${response.status}`);
  const payload = (await response.json()) as TushareResponse;
  if (payload.code !== 0) {
    throw new Error(`Tushare ${apiName}：${payload.msg || `错误码 ${payload.code}`}`);
  }
  const returnedFields = payload.data?.fields ?? [];
  return (payload.data?.items ?? []).map((item) => Object.fromEntries(
    returnedFields.map((field, index) => [field, item[index]]),
  ));
}

export function isTushareConfigured(): boolean {
  return Boolean(process.env.TUSHARE_TOKEN?.trim());
}

export async function probeTushareConnection(): Promise<boolean> {
  if (!isTushareConfigured()) return false;
  const rows = await query('stock_basic', {
    ts_code: '000001.SZ',
    list_status: 'L',
  }, ['ts_code', 'symbol', 'name']);
  return rows.some((row) => String(row.symbol) === '000001');
}

export class TushareMarketDataProvider implements MarketDataProvider {
  readonly name = 'Tushare Pro';

  async getUniverse(_asOfDate: string): Promise<StockInstrument[]> {
    void _asOfDate;
    const rows = await query('stock_basic', { list_status: 'L' }, [
      'ts_code', 'symbol', 'name', 'exchange', 'list_date', 'delist_date',
    ]);
    return rows.flatMap((row) => {
      const code = String(row.symbol ?? '').trim();
      const name = String(row.name ?? '').trim();
      if (!/^\d{6}$/.test(code) || !name) return [];
      return [{
        code,
        name,
        exchange: exchangeOf(code),
        listingDate: formatDateDashed(String(row.list_date ?? '')),
        delistingDate: row.delist_date ? formatDateDashed(String(row.delist_date)) : undefined,
        isSt: isStName(name),
      } satisfies StockInstrument];
    });
  }

  async getDailyBars(
    codes: string[],
    startDate: string,
    endDate: string,
  ): Promise<DailyMarketBar[]> {
    const output: DailyMarketBar[] = [];
    for (const rawCode of codes) {
      const code = normalizeTicker(rawCode);
      const params = {
        ts_code: tushareTicker(code),
        start_date: formatDateCompact(startDate),
        end_date: formatDateCompact(endDate),
      };
      const [daily, metrics] = await Promise.all([
        query('daily', params, [
          'ts_code', 'trade_date', 'open', 'high', 'low', 'close', 'pre_close', 'vol', 'amount',
        ]),
        query('daily_basic', params, [
          'ts_code', 'trade_date', 'turnover_rate', 'volume_ratio', 'total_mv', 'limit_status',
        ]),
      ]);
      const metricByDate = new Map(metrics.map((row) => [String(row.trade_date), row]));
      output.push(...daily.map((row) => {
        const tradeDate = String(row.trade_date ?? '');
        const metric = metricByDate.get(tradeDate);
        const close = parseNumber(row.close);
        const previousClose = parseNumber(row.pre_close);
        const limitStatus = parseNumber(metric?.limit_status);
        return {
          code,
          date: formatDateDashed(tradeDate),
          open: parseNumber(row.open),
          high: parseNumber(row.high),
          low: parseNumber(row.low),
          close,
          previousClose,
          volume: parseNumber(row.vol),
          amountYuan: parseNumber(row.amount) * 1_000,
          turnoverRatePct: parseNumber(metric?.turnover_rate),
          totalMarketCapYuan: parseNumber(metric?.total_mv) * 10_000,
          isLimitUp: limitStatus === 2 || limitStatus === 3 ||
            (limitStatus === 0 && isApproximateLimitUp(code, close, previousClose)),
        } satisfies DailyMarketBar;
      }));
    }
    return output;
  }

  async getMinuteBars(_codes: string[], _date: string): Promise<MinuteMarketBar[]> {
    void _codes;
    void _date;
    throw new Error('历史分钟线需开通 Tushare stk_mins 权限；当日分钟线由腾讯行情提供');
  }

  async getSnapshots(_codes: string[]): Promise<MarketSnapshot[]> {
    void _codes;
    throw new Error('实时快照由腾讯行情提供');
  }
}
