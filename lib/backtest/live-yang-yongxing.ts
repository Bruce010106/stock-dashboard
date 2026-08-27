import type {
  DailyMarketBar,
  HistoricalBacktestDataProvider,
} from '../data/market-data-provider.ts';
import {
  isTushareAvailable,
  sanitizeTushareError,
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
  calendarDaySpan,
  MAX_LIVE_BACKTEST_CALENDAR_DAYS_FREE,
} from '../api-validation.ts';
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
    | 'REQUIRED_METRICS_MISSING'
    | 'SIGNAL_MINUTE_DATA_MISSING'
    | 'TUSHARE_UPSTREAM_UNAVAILABLE';
  readonly status: number;

  constructor(
    message: string,
    code:
      | 'NO_VALID_CODES'
      | 'HISTORICAL_MINUTE_UNAVAILABLE'
      | 'NO_DAILY_DATA'
      | 'REQUIRED_METRICS_MISSING'
      | 'SIGNAL_MINUTE_DATA_MISSING'
      | 'TUSHARE_UPSTREAM_UNAVAILABLE',
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
 * Maps an already-resolved health result to a provider instance. Split out
 * from selectDefaultProvider so a caller that already health-checked Tushare
 * (e.g. the route handler, to decide the 30/90-day cap) can reuse that same
 * result instead of triggering a second checkTushareHealth call.
 */
export function providerForTushareHealth(tushareHealthy: boolean): HistoricalBacktestDataProvider {
  return tushareHealthy ? new TushareMarketDataProvider() : new SinaMarketDataProvider();
}

/**
 * Picks the live provider automatically: Tushare's exact, point-in-time data
 * when the token is configured AND actually reachable (health-checked, short
 * TTL cache — see checkTushareHealth), otherwise Sina's free, approximate
 * data. A configured-but-bad token (revoked, expired, wrong permissions)
 * therefore goes straight to Sina instead of failing the request. This only
 * runs when the caller does not inject a provider explicitly (e.g. in unit
 * tests), so tests never need environment configuration or network access.
 */
export async function selectDefaultProvider(): Promise<HistoricalBacktestDataProvider> {
  return providerForTushareHealth(await isTushareAvailable());
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

type DailyMarketBarWithRequiredMetrics = DailyMarketBar & {
  totalMarketCapYuan: number;
  volumeRatio: number;
  turnoverRatePct: number;
};

/**
 * Distinguishes "missing" (undefined/NaN — e.g. Sina's Tencent snapshot
 * fetch failed for this day) from "present but zero" (a real reading that
 * happens to fail the strategy's thresholds). Only the former may never be
 * silently substituted with 0, since that would fabricate a value the
 * strategy could act on.
 */
function hasRequiredMetrics(bar: DailyMarketBar): bar is DailyMarketBarWithRequiredMetrics {
  return Number.isFinite(bar.totalMarketCapYuan) &&
    Number.isFinite(bar.volumeRatio) &&
    Number.isFinite(bar.turnoverRatePct);
}

function dailyCandidate(
  code: string,
  name: string,
  bars: DailyMarketBar[],
  index: number,
  current: DailyMarketBarWithRequiredMetrics,
): Omit<YangYongxingCandidate, 'minuteBars'> {
  const recentDailyBars = bars.slice(
    Math.max(0, index - YANG_YONGXING_RULES.lookbackTradingDays),
    index,
  );
  return {
    code,
    name,
    changePct: (current.close / current.previousClose - 1) * 100,
    totalMarketCapYuan: current.totalMarketCapYuan,
    volumeRatio: current.volumeRatio,
    turnoverRatePct: current.turnoverRatePct,
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

/**
 * Runs the backtest against the caller-chosen provider, applying a runtime
 * fallback to Sina when the actual Tushare provider fails mid-run (bad
 * token revoked after the health check, upstream outage, missing stk_mins
 * permission, etc.) — as opposed to selectDefaultProvider's upfront health
 * check, which only catches failures that are already known at request
 * time. Business-logic errors (NO_VALID_CODES, a 4xx) are never retried:
 * they reflect the requested codes/state, not the data source's health, and
 * would fail identically against Sina.
 */
export async function runLiveYangYongxingBacktest(
  request: LiveYangYongxingBacktestRequest,
  provider?: HistoricalBacktestDataProvider,
): Promise<LiveYangYongxingBacktestResponse> {
  const resolvedProvider = provider ?? await selectDefaultProvider();
  try {
    return await executeLiveYangYongxingBacktest(request, resolvedProvider);
  } catch (error) {
    if (!isRetryableTushareFailure(error, resolvedProvider)) throw error;

    const requestedDays = calendarDaySpan(request.startDate, request.endDate);
    if (requestedDays > MAX_LIVE_BACKTEST_CALENDAR_DAYS_FREE) {
      throw new LiveBacktestDataError(
        `Tushare 数据获取失败（${sanitizeTushareError(error)}），且回测区间 ${requestedDays} 天超过免费新浪源上限 ${MAX_LIVE_BACKTEST_CALENDAR_DAYS_FREE} 天，无法自动降级；请将区间缩短至 ${MAX_LIVE_BACKTEST_CALENDAR_DAYS_FREE} 天以内后重试`,
        'TUSHARE_UPSTREAM_UNAVAILABLE',
        503,
      );
    }

    const fallbackResult = await executeLiveYangYongxingBacktest(request, new SinaMarketDataProvider());
    return {
      ...fallbackResult,
      warnings: [
        `Tushare 数据获取失败，已自动切换到新浪财经免费近似源重跑：${sanitizeTushareError(error)}`,
        ...fallbackResult.warnings,
      ],
    };
  }
}

/** Only Tushare's own upstream/permission failures are retried against Sina — not a 4xx business error (e.g. NO_VALID_CODES), and not a failure from an explicitly injected non-Tushare provider. */
function isRetryableTushareFailure(
  error: unknown,
  provider: HistoricalBacktestDataProvider,
): boolean {
  if (!(provider instanceof TushareMarketDataProvider)) return false;
  if (error instanceof LiveBacktestDataError && error.status < 500) return false;
  return true;
}

async function executeLiveYangYongxingBacktest(
  request: LiveYangYongxingBacktestRequest,
  provider: HistoricalBacktestDataProvider,
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
  const missingMetricsDays: { code: string; date: string }[] = [];
  let scannedTradingDays = 0;
  let eligibleTradingDays = 0;
  for (const code of evaluatedCodes) {
    const bars = dailyByCode.get(code) ?? [];
    const name = instrumentByCode.get(code)?.name ?? code;
    for (let index = 0; index < bars.length; index += 1) {
      const bar = bars[index];
      if (!bar || bar.date < request.startDate || bar.date > request.endDate) continue;
      scannedTradingDays += 1;
      if (bar.previousClose <= 0) continue;
      eligibleTradingDays += 1;
      if (!hasRequiredMetrics(bar)) {
        missingMetricsDays.push({ code, date: bar.date });
        continue;
      }
      const candidate = dailyCandidate(code, name, bars, index, bar);
      if (passesFirstFiveRules(candidate)) {
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

  if (eligibleTradingDays > 0 && missingMetricsDays.length === eligibleTradingDays) {
    const codes = [...new Set(missingMetricsDays.map((day) => day.code))];
    throw new LiveBacktestDataError(
      `所选股票在区间内可扫描的交易日均缺少总市值、量比或换手率等必需指标，数据源暂不可用：${codes.join('、')}`,
      'REQUIRED_METRICS_MISSING',
      503,
    );
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
  if (missingMetricsDays.length > 0) {
    const daysByCode = new Map<string, number>();
    for (const day of missingMetricsDays) {
      daysByCode.set(day.code, (daysByCode.get(day.code) ?? 0) + 1);
    }
    const summary = [...daysByCode.entries()]
      .map(([code, count]) => `${code}（${count} 个交易日）`)
      .join('、');
    warnings.push(`以下股票存在缺少总市值、量比或换手率等必需指标的交易日，已跳过：${summary}`);
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
