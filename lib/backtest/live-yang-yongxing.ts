import type {
  DailyMarketBar,
  MinuteMarketBar,
  StockInstrument,
} from '../data/market-data-provider.ts';
import {
  isTushareConfigured,
  TushareMarketDataProvider,
} from '../data/tushare-provider.ts';
import {
  evaluateYangYongxing,
  YANG_YONGXING_RULES,
  type YangYongxingCandidate,
} from '../strategies/yang-yongxing.ts';
import {
  runYangYongxingForwardBacktest,
  type ForwardBacktestResult,
  type FutureClose,
  type YangYongxingSignalEvent,
} from './yang-yongxing-forward.ts';

export type LiveYangYongxingBacktestRequest = {
  codes: string[];
  startDate: string;
  endDate: string;
  holdingTradingDays: number;
};

export type LiveYangYongxingBacktestResponse = ForwardBacktestResult & {
  source: 'Tushare Pro';
  generatedAt: string;
  startDate: string;
  endDate: string;
  requestedCodes: string[];
  evaluatedCodes: string[];
  scannedTradingDays: number;
  prefilteredDays: number;
  minuteDaysLoaded: number;
  warnings: string[];
};

export interface HistoricalBacktestDataProvider {
  getUniverse(asOfDate: string): Promise<StockInstrument[]>;
  getDailyBars(codes: string[], startDate: string, endDate: string): Promise<DailyMarketBar[]>;
  getHistoricalMinuteBars(code: string, date: string): Promise<MinuteMarketBar[]>;
}

export class LiveBacktestDataError extends Error {
  readonly code: 'TUSHARE_NOT_CONFIGURED' | 'NO_VALID_CODES' | 'HISTORICAL_MINUTE_UNAVAILABLE';
  readonly status: number;

  constructor(
    message: string,
    code: 'TUSHARE_NOT_CONFIGURED' | 'NO_VALID_CODES' | 'HISTORICAL_MINUTE_UNAVAILABLE',
    status = 503,
  ) {
    super(message);
    this.name = 'LiveBacktestDataError';
    this.code = code;
    this.status = status;
  }
}

type PrefilteredDay = {
  code: string;
  date: string;
  name: string;
  dailyBars: DailyMarketBar[];
  dailyIndex: number;
  candidate: Omit<YangYongxingCandidate, 'minuteBars'>;
};

function shiftCalendarDays(date: string, days: number): string {
  const value = new Date(`${date}T00:00:00Z`);
  value.setUTCDate(value.getUTCDate() + days);
  return value.toISOString().slice(0, 10);
}

function groupDailyBars(bars: DailyMarketBar[]): Map<string, DailyMarketBar[]> {
  const grouped = new Map<string, DailyMarketBar[]>();
  for (const bar of bars) {
    const current = grouped.get(bar.code) ?? [];
    current.push(bar);
    grouped.set(bar.code, current);
  }
  for (const current of grouped.values()) {
    current.sort((a, b) => a.date.localeCompare(b.date));
  }
  return grouped;
}

function dailyCandidate(
  code: string,
  name: string,
  bars: DailyMarketBar[],
  index: number,
): Omit<YangYongxingCandidate, 'minuteBars'> | undefined {
  const current = bars[index];
  if (!current || current.previousClose <= 0) return undefined;
  const recentDailyBars = bars.slice(
    Math.max(0, index - YANG_YONGXING_RULES.lookbackTradingDays),
    index,
  );
  return {
    code,
    name,
    changePct: (current.close / current.previousClose - 1) * 100,
    totalMarketCapYuan: current.totalMarketCapYuan ?? 0,
    volumeRatio: current.volumeRatio ?? 0,
    turnoverRatePct: current.turnoverRatePct ?? 0,
    recentDailyBars: recentDailyBars.map((bar) => ({
      date: bar.date,
      close: bar.close,
      previousClose: bar.previousClose,
      limitUpPrice: bar.limitUpPrice,
      isLimitUp: bar.isLimitUp,
    })),
  };
}

function passesFirstFiveRules(candidate: Omit<YangYongxingCandidate, 'minuteBars'>): boolean {
  const evaluation = evaluateYangYongxing({ ...candidate, minuteBars: [] });
  return evaluation.checks
    .filter((check) => check.key !== 'tail_pattern')
    .every((check) => check.passed);
}

function futureCloses(
  bars: DailyMarketBar[],
  signalIndex: number,
  maximumHoldingDays: number,
): FutureClose[] {
  const output: FutureClose[] = [];
  for (let days = 1; days <= maximumHoldingDays; days += 1) {
    const bar = bars[signalIndex + days];
    if (!bar) break;
    output.push({ tradingDaysAfter: days, close: bar.close });
  }
  return output;
}

async function loadEvents(
  candidates: PrefilteredDay[],
  provider: HistoricalBacktestDataProvider,
  holdingTradingDays: number,
): Promise<{ events: YangYongxingSignalEvent[]; loaded: number }> {
  const events: YangYongxingSignalEvent[] = [];
  let loaded = 0;

  // Keep concurrency deliberately small because historical minute data is a
  // permissioned, rate-limited upstream API.
  for (let index = 0; index < candidates.length; index += 3) {
    const batch = candidates.slice(index, index + 3);
    const settled = await Promise.allSettled(batch.map(async (item) => ({
      item,
      minuteBars: await provider.getHistoricalMinuteBars(item.code, item.date),
    })));
    for (const result of settled) {
      if (result.status === 'rejected') {
        const reason = result.reason instanceof Error ? result.reason.message : '未知错误';
        throw new LiveBacktestDataError(
          `历史分钟线读取失败：${reason}`,
          'HISTORICAL_MINUTE_UNAVAILABLE',
        );
      }
      loaded += 1;
      const { item, minuteBars } = result.value;
      const candidate: YangYongxingCandidate = { ...item.candidate, minuteBars };
      const evaluation = evaluateYangYongxing(candidate);
      if (!evaluation.passed) continue;
      const signalPrice = [...minuteBars]
        .sort((a, b) => a.time.localeCompare(b.time))
        .at(-1)?.close ?? 0;
      events.push({
        signalDate: item.date,
        candidate,
        signalPrice,
        futureCloses: futureCloses(
          item.dailyBars,
          item.dailyIndex,
          holdingTradingDays,
        ),
      });
    }
  }

  return { events, loaded };
}

export async function runLiveYangYongxingBacktest(
  request: LiveYangYongxingBacktestRequest,
  provider: HistoricalBacktestDataProvider = new TushareMarketDataProvider(),
  options: { skipConfigurationCheck?: boolean } = {},
): Promise<LiveYangYongxingBacktestResponse> {
  if (!options.skipConfigurationCheck && !isTushareConfigured()) {
    throw new LiveBacktestDataError(
      '真实历史回测需要配置 TUSHARE_TOKEN，并开通 daily、daily_basic 与 stk_mins 权限',
      'TUSHARE_NOT_CONFIGURED',
    );
  }

  const historyStart = shiftCalendarDays(request.startDate, -70);
  const futureEnd = shiftCalendarDays(
    request.endDate,
    Math.max(20, request.holdingTradingDays * 3),
  );
  const [universe, dailyBars] = await Promise.all([
    provider.getUniverse(request.endDate),
    provider.getDailyBars(request.codes, historyStart, futureEnd),
  ]);
  const instrumentByCode = new Map(universe.map((item) => [item.code, item]));
  const evaluatedCodes = request.codes.filter((code) => {
    const instrument = instrumentByCode.get(code);
    return instrument && !instrument.isSt && !/退/.test(instrument.name);
  });
  if (evaluatedCodes.length === 0) {
    throw new LiveBacktestDataError(
      '指定股票不存在，或当前处于 ST / 退市状态',
      'NO_VALID_CODES',
      422,
    );
  }

  const dailyByCode = groupDailyBars(dailyBars);
  const prefiltered: PrefilteredDay[] = [];
  let scannedTradingDays = 0;
  for (const code of evaluatedCodes) {
    const bars = dailyByCode.get(code) ?? [];
    const name = instrumentByCode.get(code)?.name ?? code;
    for (let index = 0; index < bars.length; index += 1) {
      const bar = bars[index];
      if (!bar || bar.date < request.startDate || bar.date > request.endDate) continue;
      scannedTradingDays += 1;
      const candidate = dailyCandidate(code, name, bars, index);
      if (candidate && passesFirstFiveRules(candidate)) {
        prefiltered.push({
          code,
          date: bar.date,
          name,
          dailyBars: bars,
          dailyIndex: index,
          candidate,
        });
      }
    }
  }

  const { events, loaded } = await loadEvents(
    prefiltered,
    provider,
    request.holdingTradingDays,
  );
  const result = runYangYongxingForwardBacktest(
    events,
    request.holdingTradingDays,
  );
  const warnings = [
    '股票名称及 ST 状态采用当前股票列表；历史 ST 状态尚未单独回放',
  ];
  if (result.completedSignals < result.totalSignals) {
    warnings.push('区间末尾部分信号缺少完整未来持有期，未计入收益统计');
  }

  return {
    ...result,
    source: 'Tushare Pro',
    generatedAt: new Date().toISOString(),
    startDate: request.startDate,
    endDate: request.endDate,
    requestedCodes: request.codes,
    evaluatedCodes,
    scannedTradingDays,
    prefilteredDays: prefiltered.length,
    minuteDaysLoaded: loaded,
    warnings,
  };
}
