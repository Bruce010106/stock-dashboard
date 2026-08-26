import type {
  DailyBar,
  MinuteBar,
  YangYongxingCandidate,
} from './strategies/yang-yongxing.ts';
import type {
  FutureClose,
  YangYongxingSignalEvent,
} from './backtest/yang-yongxing-forward.ts';
import { normalizeTicker } from './data/provider-utils.ts';

/**
 * Limits for request payloads.  The strategy is intended to audit a finite
 * batch of already prepared market data, not to be used as an unbounded data
 * ingestion endpoint.
 */
export const MAX_CANDIDATES_PER_REQUEST = 500;
export const MAX_EVENTS_PER_REQUEST = 200;
export const MAX_DAILY_BARS_PER_CANDIDATE = 366;
export const MAX_MINUTE_BARS_PER_CANDIDATE = 500;
export const MAX_FUTURE_CLOSES_PER_EVENT = 366;
export const MAX_HOLDING_TRADING_DAYS = 366;
export const DEFAULT_HOLDING_TRADING_DAYS = 5;
export const MAX_LIVE_BACKTEST_CODES = 5;
export const MAX_LIVE_BACKTEST_CALENDAR_DAYS_TUSHARE = 90;
export const MAX_LIVE_BACKTEST_CALENDAR_DAYS_FREE = 30;
export const LIVE_BACKTEST_HOLDING_DAYS = [1, 3, 5, 10] as const;

const MAX_TEXT_LENGTH = 200;
const MAX_PRICE = 1_000_000_000;
const MAX_MARKET_CAP_YUAN = 1_000_000_000_000_000;
const MAX_CHANGE_PCT = 1_000;
const MAX_VOLUME_RATIO = 10_000;
const MAX_TURNOVER_RATE_PCT = 1_000;

export type ValidationResult<T> =
  | { ok: true; value: T }
  | { ok: false; error: string };

type JsonObject = Record<string, unknown>;

function isObject(value: unknown): value is JsonObject {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

function isFiniteNumber(value: unknown): value is number {
  return typeof value === 'number' && Number.isFinite(value);
}

function isBoundedNumber(
  value: unknown,
  minimum: number,
  maximum: number,
): value is number {
  return isFiniteNumber(value) && value >= minimum && value <= maximum;
}

function isNonEmptyText(value: unknown, maximum = MAX_TEXT_LENGTH): value is string {
  return typeof value === 'string' && value.trim().length > 0 && value.length <= maximum;
}

function isIsoDate(value: unknown): value is string {
  if (typeof value !== 'string' || !/^\d{4}-\d{2}-\d{2}$/.test(value)) return false;
  const [year, month, day] = value.split('-').map(Number);
  const date = new Date(Date.UTC(year, month - 1, day));
  return date.getUTCFullYear() === year &&
    date.getUTCMonth() === month - 1 &&
    date.getUTCDate() === day;
}

function isClockTime(value: unknown): value is string {
  if (typeof value !== 'string' || !/^\d{2}:\d{2}$/.test(value)) return false;
  const [hour, minute] = value.split(':').map(Number);
  return hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59;
}

function invalid(path: string, message: string): ValidationResult<never> {
  return { ok: false, error: `${path}${message}` };
}

export function validateDailyBar(
  value: unknown,
  path = 'recentDailyBars[]',
): ValidationResult<DailyBar> {
  if (!isObject(value)) return invalid(path, ' 必须是对象');
  if (!isIsoDate(value.date)) return invalid(`${path}.date`, ' 必须是有效的 YYYY-MM-DD 日期');
  if (!isBoundedNumber(value.close, 0, MAX_PRICE)) return invalid(`${path}.close`, ' 必须是有限的非负数字');

  if (value.previousClose !== undefined && !isBoundedNumber(value.previousClose, 0, MAX_PRICE)) {
    return invalid(`${path}.previousClose`, ' 必须是有限的非负数字');
  }
  if (value.limitUpPrice !== undefined && !isBoundedNumber(value.limitUpPrice, 0, MAX_PRICE)) {
    return invalid(`${path}.limitUpPrice`, ' 必须是有限的非负数字');
  }
  if (value.isLimitUp !== undefined && typeof value.isLimitUp !== 'boolean') {
    return invalid(`${path}.isLimitUp`, ' 必须是布尔值');
  }

  const bar: DailyBar = { date: value.date, close: value.close };
  if (value.previousClose !== undefined) bar.previousClose = value.previousClose;
  if (value.limitUpPrice !== undefined) bar.limitUpPrice = value.limitUpPrice;
  if (value.isLimitUp !== undefined) bar.isLimitUp = value.isLimitUp;
  return { ok: true, value: bar };
}

export function validateMinuteBar(
  value: unknown,
  path = 'minuteBars[]',
): ValidationResult<MinuteBar> {
  if (!isObject(value)) return invalid(path, ' 必须是对象');
  if (!isClockTime(value.time)) return invalid(`${path}.time`, ' 必须是有效的 HH:mm 时间');
  if (!isBoundedNumber(value.high, 0, MAX_PRICE)) return invalid(`${path}.high`, ' 必须是有限的非负数字');
  if (!isBoundedNumber(value.low, 0, MAX_PRICE)) return invalid(`${path}.low`, ' 必须是有限的非负数字');
  if (!isBoundedNumber(value.close, 0, MAX_PRICE)) return invalid(`${path}.close`, ' 必须是有限的非负数字');
  if (value.low > value.high) return invalid(path, ' 的 low 不能高于 high');
  if (value.close < value.low || value.close > value.high) {
    return invalid(path, ' 的 close 必须位于 low 和 high 之间');
  }

  return {
    ok: true,
    value: {
      time: value.time,
      high: value.high,
      low: value.low,
      close: value.close,
    },
  };
}

export function validateYangYongxingCandidate(
  value: unknown,
  path = 'candidates[]',
): ValidationResult<YangYongxingCandidate> {
  if (!isObject(value)) return invalid(path, ' 必须是对象');
  if (typeof value.code !== 'string' || !/^\d{6}$/.test(value.code)) {
    return invalid(`${path}.code`, ' 必须是 6 位数字股票代码');
  }
  if (!isNonEmptyText(value.name)) return invalid(`${path}.name`, ' 必须是非空字符串');
  if (!isBoundedNumber(value.changePct, -MAX_CHANGE_PCT, MAX_CHANGE_PCT)) {
    return invalid(`${path}.changePct`, ' 必须是有限且在合理范围内的数字');
  }
  if (!isBoundedNumber(value.totalMarketCapYuan, 0, MAX_MARKET_CAP_YUAN)) {
    return invalid(`${path}.totalMarketCapYuan`, ' 必须是有限的非负数字');
  }
  if (!isBoundedNumber(value.volumeRatio, 0, MAX_VOLUME_RATIO)) {
    return invalid(`${path}.volumeRatio`, ' 必须是有限且在合理范围内的数字');
  }
  if (!isBoundedNumber(value.turnoverRatePct, 0, MAX_TURNOVER_RATE_PCT)) {
    return invalid(`${path}.turnoverRatePct`, ' 必须是有限且在合理范围内的数字');
  }

  if (!Array.isArray(value.recentDailyBars)) {
    return invalid(`${path}.recentDailyBars`, ' 必须是日线数组');
  }
  if (value.recentDailyBars.length > MAX_DAILY_BARS_PER_CANDIDATE) {
    return invalid(`${path}.recentDailyBars`, ` 数量不能超过 ${MAX_DAILY_BARS_PER_CANDIDATE}`);
  }
  const recentDailyBars: DailyBar[] = [];
  for (const [index, bar] of value.recentDailyBars.entries()) {
    const result = validateDailyBar(bar, `${path}.recentDailyBars[${index}]`);
    if (!result.ok) return result;
    recentDailyBars.push(result.value);
  }

  if (!Array.isArray(value.minuteBars)) {
    return invalid(`${path}.minuteBars`, ' 必须是分钟线数组');
  }
  if (value.minuteBars.length > MAX_MINUTE_BARS_PER_CANDIDATE) {
    return invalid(`${path}.minuteBars`, ` 数量不能超过 ${MAX_MINUTE_BARS_PER_CANDIDATE}`);
  }
  const minuteBars: MinuteBar[] = [];
  for (const [index, bar] of value.minuteBars.entries()) {
    const result = validateMinuteBar(bar, `${path}.minuteBars[${index}]`);
    if (!result.ok) return result;
    minuteBars.push(result.value);
  }

  return {
    ok: true,
    value: {
      code: value.code,
      name: value.name,
      changePct: value.changePct,
      totalMarketCapYuan: value.totalMarketCapYuan,
      volumeRatio: value.volumeRatio,
      turnoverRatePct: value.turnoverRatePct,
      recentDailyBars,
      minuteBars,
    },
  };
}

export type YangYongxingStrategyPayload = {
  candidates: YangYongxingCandidate[];
  explain?: boolean;
};

export function validateYangYongxingStrategyPayload(
  value: unknown,
): ValidationResult<YangYongxingStrategyPayload> {
  if (!isObject(value)) return invalid('请求体', ' 必须是 JSON 对象');
  if (!Array.isArray(value.candidates)) return invalid('candidates', ' 必须是候选股票数组');
  if (value.candidates.length > MAX_CANDIDATES_PER_REQUEST) {
    return invalid('candidates', ` 数量不能超过 ${MAX_CANDIDATES_PER_REQUEST}`);
  }
  if (value.explain !== undefined && typeof value.explain !== 'boolean') {
    return invalid('explain', ' 必须是布尔值');
  }

  const candidates: YangYongxingCandidate[] = [];
  for (const [index, candidate] of value.candidates.entries()) {
    const result = validateYangYongxingCandidate(candidate, `candidates[${index}]`);
    if (!result.ok) return result;
    candidates.push(result.value);
  }

  return {
    ok: true,
    value: {
      candidates,
      ...(value.explain === undefined ? {} : { explain: value.explain }),
    },
  };
}

export function validateFutureClose(
  value: unknown,
  path = 'futureCloses[]',
): ValidationResult<FutureClose> {
  if (!isObject(value)) return invalid(path, ' 必须是对象');
  const tradingDaysAfter = value.tradingDaysAfter;
  if (typeof tradingDaysAfter !== 'number' ||
      !Number.isInteger(tradingDaysAfter) ||
      tradingDaysAfter < 1 ||
      tradingDaysAfter > MAX_HOLDING_TRADING_DAYS) {
    return invalid(`${path}.tradingDaysAfter`, ` 必须是 1-${MAX_HOLDING_TRADING_DAYS} 的整数`);
  }
  const close = value.close;
  if (!isBoundedNumber(close, 0, MAX_PRICE) || close === 0) {
    return invalid(`${path}.close`, ' 必须是有限的正数字');
  }
  return { ok: true, value: { tradingDaysAfter, close } };
}

export function validateYangYongxingSignalEvent(
  value: unknown,
  path = 'events[]',
): ValidationResult<YangYongxingSignalEvent> {
  if (!isObject(value)) return invalid(path, ' 必须是对象');
  if (!isIsoDate(value.signalDate)) return invalid(`${path}.signalDate`, ' 必须是有效的 YYYY-MM-DD 日期');

  const candidate = validateYangYongxingCandidate(value.candidate, `${path}.candidate`);
  if (!candidate.ok) return candidate;

  if (!isBoundedNumber(value.signalPrice, 0, MAX_PRICE) || value.signalPrice === 0) {
    return invalid(`${path}.signalPrice`, ' 必须是有限的正数字');
  }
  if (!Array.isArray(value.futureCloses)) return invalid(`${path}.futureCloses`, ' 必须是未来收盘价数组');
  if (value.futureCloses.length > MAX_FUTURE_CLOSES_PER_EVENT) {
    return invalid(`${path}.futureCloses`, ` 数量不能超过 ${MAX_FUTURE_CLOSES_PER_EVENT}`);
  }

  const futureCloses: FutureClose[] = [];
  for (const [index, close] of value.futureCloses.entries()) {
    const result = validateFutureClose(close, `${path}.futureCloses[${index}]`);
    if (!result.ok) return result;
    futureCloses.push(result.value);
  }

  return {
    ok: true,
    value: {
      signalDate: value.signalDate,
      candidate: candidate.value,
      signalPrice: value.signalPrice,
      futureCloses,
    },
  };
}

export type YangYongxingBacktestPayload = {
  events: YangYongxingSignalEvent[];
  holdingTradingDays: number;
};

export function validateYangYongxingBacktestPayload(
  value: unknown,
): ValidationResult<YangYongxingBacktestPayload> {
  if (!isObject(value)) return invalid('请求体', ' 必须是 JSON 对象');
  if (!Array.isArray(value.events)) return invalid('events', ' 必须是信号事件数组');
  if (value.events.length > MAX_EVENTS_PER_REQUEST) {
    return invalid('events', ` 数量不能超过 ${MAX_EVENTS_PER_REQUEST}`);
  }

  const holdingTradingDays: unknown = value.holdingTradingDays === undefined
    ? DEFAULT_HOLDING_TRADING_DAYS
    : value.holdingTradingDays;
  if (typeof holdingTradingDays !== 'number' ||
      !Number.isInteger(holdingTradingDays) ||
      holdingTradingDays < 1 ||
      holdingTradingDays > MAX_HOLDING_TRADING_DAYS) {
    return invalid('holdingTradingDays', ` 必须是 1-${MAX_HOLDING_TRADING_DAYS} 的整数`);
  }

  const events: YangYongxingSignalEvent[] = [];
  for (const [index, event] of value.events.entries()) {
    const result = validateYangYongxingSignalEvent(event, `events[${index}]`);
    if (!result.ok) return result;
    events.push(result.value);
  }

  return { ok: true, value: { events, holdingTradingDays } };
}

export type LiveBacktestQuery = {
  codes: string[];
  startDate: string;
  endDate: string;
  holdingTradingDays: number;
};

export type LiveBacktestQueryOptions = {
  /** Whether Tushare is configured; determines the allowed date-span cap. */
  tushareConfigured: boolean;
};

export function validateLiveBacktestQuery(
  searchParams: URLSearchParams,
  options: LiveBacktestQueryOptions,
): ValidationResult<LiveBacktestQuery> {
  const rawCodes = searchParams.get('codes')?.split(',')
    .map((code) => code.trim())
    .filter(Boolean) ?? [];
  if (rawCodes.length === 0) return invalid('codes', ' 至少需要一个股票代码');

  let codes: string[];
  try {
    codes = [...new Set(rawCodes.map(normalizeTicker))];
  } catch (error) {
    return invalid(
      'codes',
      ` 包含无效股票代码：${error instanceof Error ? error.message : '格式错误'}`,
    );
  }
  if (codes.length > MAX_LIVE_BACKTEST_CODES) {
    return invalid('codes', ` 数量不能超过 ${MAX_LIVE_BACKTEST_CODES}`);
  }

  const startDate = searchParams.get('startDate');
  const endDate = searchParams.get('endDate');
  if (!isIsoDate(startDate)) return invalid('startDate', ' 必须是有效的 YYYY-MM-DD 日期');
  if (!isIsoDate(endDate)) return invalid('endDate', ' 必须是有效的 YYYY-MM-DD 日期');
  const startTime = Date.parse(`${startDate}T00:00:00Z`);
  const endTime = Date.parse(`${endDate}T00:00:00Z`);
  if (startTime > endTime) return invalid('回测区间', ' 的开始日期不能晚于结束日期');
  const calendarDays = Math.floor((endTime - startTime) / 86_400_000) + 1;
  const maxCalendarDays = options.tushareConfigured
    ? MAX_LIVE_BACKTEST_CALENDAR_DAYS_TUSHARE
    : MAX_LIVE_BACKTEST_CALENDAR_DAYS_FREE;
  if (calendarDays > maxCalendarDays) {
    return invalid(
      '回测区间',
      ` 不能超过 ${maxCalendarDays} 个自然日（${options.tushareConfigured ? 'Tushare 精确模式' : '新浪免费近似模式'}）`,
    );
  }

  const holdingRaw = searchParams.get('holdingTradingDays') ?? String(DEFAULT_HOLDING_TRADING_DAYS);
  const holdingTradingDays = Number(holdingRaw);
  if (!LIVE_BACKTEST_HOLDING_DAYS.includes(
    holdingTradingDays as (typeof LIVE_BACKTEST_HOLDING_DAYS)[number],
  )) {
    return invalid('holdingTradingDays', ` 只支持 ${LIVE_BACKTEST_HOLDING_DAYS.join('/')} 个交易日`);
  }

  return {
    ok: true,
    value: { codes, startDate, endDate, holdingTradingDays },
  };
}
