import type {
  DailyMarketBar,
  HistoricalBacktestDataProvider,
} from '../data/market-data-provider.ts';
import {
  isTushareConfigured,
  TushareMarketDataProvider,
} from '../data/tushare-provider.ts';
import {
  SINA_PROVIDER_METADATA,
  SinaMarketDataProvider,
} from '../data/sina-provider.ts';
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

export type BacktestAccuracyMode = 'point-in-time-1m' | 'approximate-5m';

const TUSHARE_SOURCE = 'Tushare Pro';
const TUSHARE_MAX_RANGE_DAYS = 90;

export type LiveYangYongxingBacktestResponse = ForwardBacktestResult & {
  source: string;
  accuracyMode: BacktestAccuracyMode;
  isApproximate: boolean;
  maxRangeDays: number;
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

export type { HistoricalBacktestDataProvider };

export class LiveBacktestDataError extends Error {
  readonly code:
    | 'NO_VALID_CODES'
    | 'HISTORICAL_MINUTE_UNAVAILABLE'
    | 'NO_DAILY_DATA'
    | 'SIGNAL_MINUTE_DATA_MISSING';
  readonly status: number;

  constructor(
    message: string,
    code:
      | 'NO_VALID_CODES'
      | 'HISTORICAL_MINUTE_UNAVAILABLE'
      | 'NO_DAILY_DATA'
      | 'SIGNAL_MINUTE_DATA_MISSING',
    status = 503,
  ) {
    super(message);
    this.name = 'LiveBacktestDataError';
    this.code = code;
    this.status = status;
  }
}

type ProviderMetadata = {
  source: string;
  accuracyMode: BacktestAccuracyMode;
  isApproximate: boolean;
  maxRangeDays: number;
  extraWarnings: string[];
};

/**
 * Picks the live provider automatically: Tushare's exact, point-in-time data
 * when a token is configured, otherwise Sina's free, approximate data. This
 * only runs when the caller does not inject a provider explicitly (e.g. in
 * unit tests), so tests never need environment configuration.
 */
export function selectDefaultProvider(): HistoricalBacktestDataProvider {
  return isTushareConfigured() ? new TushareMarketDataProvider() : new SinaMarketDataProvider();
}

/**
 * Derives response metadata from the provider actually in use, rather than
 * from environment state, so explicitly injected test doubles behave
 * predictably without needing to fake configuration.
 */
function metadataFor(provider: HistoricalBacktestDataProvider): ProviderMetadata {
  if (provider instanceof SinaMarketDataProvider) {
    return {
      source: SINA_PROVIDER_METADATA.source,
      accuracyMode: SINA_PROVIDER_METADATA.accuracyMode,
      isApproximate: true,
      maxRangeDays: SINA_PROVIDER_METADATA.maxRecommendedRangeDays,
      extraWarnings: [...SINA_PROVIDER_METADATA.warnings],
    };
  }
  return {
    source: TUSHARE_SOURCE,
    accuracyMode: 'point-in-time-1m',
    isApproximate: false,
    maxRangeDays: TUSHARE_MAX_RANGE_DAYS,
    extraWarnings: [],
  };
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
): Promise<{
  events: YangYongxingSignalEvent[];
  loaded: number;
  emptyMinuteDays: { code: string; date: string }[];
}> {
  const events: YangYongxingSignalEvent[] = [];
  const emptyMinuteDays: { code: string; date: string }[] = [];
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
      if (minuteBars.length === 0) {
        emptyMinuteDays.push({ code: item.code, date: item.date });
        continue;
      }
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

  return { events, loaded, emptyMinuteDays };
}

export async function runLiveYangYongxingBacktest(
  request: LiveYangYongxingBacktestRequest,
  provider: HistoricalBacktestDataProvider = selectDefaultProvider(),
): Promise<LiveYangYongxingBacktestResponse> {
  const meta = metadataFor(provider);

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
  const missingDailyCodes = evaluatedCodes.filter(
    (code) => (dailyByCode.get(code)?.length ?? 0) === 0,
  );
  if (missingDailyCodes.length === evaluatedCodes.length) {
    throw new LiveBacktestDataError(
      `所选股票在区间内均未返回日线数据，数据源暂不可用：${missingDailyCodes.join('、')}`,
      'NO_DAILY_DATA',
      503,
    );
  }

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

  const { events, loaded, emptyMinuteDays } = await loadEvents(
    prefiltered,
    provider,
    request.holdingTradingDays,
  );
  if (prefiltered.length > 0 && emptyMinuteDays.length === prefiltered.length) {
    const codes = [...new Set(emptyMinuteDays.map((day) => day.code))];
    throw new LiveBacktestDataError(
      `所有候选信号日均无分钟线数据，无法判断是否形成信号，数据源暂不可用：${codes.join('、')}`,
      'SIGNAL_MINUTE_DATA_MISSING',
      503,
    );
  }

  const result = runYangYongxingForwardBacktest(
    events,
    request.holdingTradingDays,
  );
  const warnings = [
    ...meta.extraWarnings,
    '股票名称及 ST 状态采用当前股票列表；历史 ST 状态尚未单独回放',
  ];
  if (missingDailyCodes.length > 0) {
    warnings.push(`以下股票在区间内无有效日线数据，已跳过：${missingDailyCodes.join('、')}`);
  }
  if (emptyMinuteDays.length > 0) {
    const codes = [...new Set(emptyMinuteDays.map((day) => day.code))];
    warnings.push(`以下股票存在候选信号日缺少分钟线数据，已跳过：${codes.join('、')}`);
  }
  if (result.completedSignals < result.totalSignals) {
    warnings.push('区间末尾部分信号缺少完整未来持有期，未计入收益统计');
  }

  return {
    ...result,
    source: meta.source,
    accuracyMode: meta.accuracyMode,
    isApproximate: meta.isApproximate,
    maxRangeDays: meta.maxRangeDays,
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
