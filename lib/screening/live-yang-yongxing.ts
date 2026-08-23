import { marketDataProvider } from '../data/composite-provider.ts';
import type { DailyMarketBar, MarketSnapshot } from '../data/market-data-provider.ts';
import {
  evaluateYangYongxing,
  YANG_YONGXING_RULES,
  type RuleCheck,
} from '../strategies/yang-yongxing.ts';

export type LiveScreenMatch = {
  code: string;
  name: string;
  changePct: number;
  totalMarketCapYuan: number;
  volumeRatio: number;
  turnoverRatePct: number;
  breakoutTime?: string;
  breakoutLevel?: number;
  score: number;
  checks: RuleCheck[];
};

export type LiveScreenNearMiss = LiveScreenMatch & {
  failedRuleKey: 'tail_pattern';
  failedRuleLabel: string;
  reason: string;
};

export type LiveScreenResponse = {
  strategy: 'yang-yongxing-tail-1430';
  tradeDate: string;
  generatedAt: string;
  source: string;
  historyMode: 'tushare' | 'tencent-fallback';
  isFallback: boolean;
  scanned: number;
  quoted: number;
  funnel: {
    realtimeRules: number;
    recentLimitUp: number;
    intradayConfirmed: number;
  };
  results: LiveScreenMatch[];
  nearMisses: LiveScreenNearMiss[];
  warnings: string[];
};

let cachedScan: { expiresAt: number; value: LiveScreenResponse } | undefined;
let activeScan: Promise<LiveScreenResponse> | undefined;

function calendarDaysBefore(date: string, days: number): string {
  const value = new Date(`${date}T00:00:00+08:00`);
  value.setUTCDate(value.getUTCDate() - days);
  return value.toISOString().slice(0, 10);
}

function tradeDateOf(snapshots: MarketSnapshot[]): string {
  const counts = new Map<string, number>();
  for (const snapshot of snapshots) {
    const date = snapshot.timestamp.slice(0, 10);
    if (/^\d{4}-\d{2}-\d{2}$/.test(date)) {
      counts.set(date, (counts.get(date) ?? 0) + 1);
    }
  }
  return [...counts.entries()].sort((a, b) => b[1] - a[1])[0]?.[0] ??
    new Date().toISOString().slice(0, 10);
}

function changePct(snapshot: MarketSnapshot): number {
  return (snapshot.lastPrice / snapshot.previousClose - 1) * 100;
}

function passesRealtimeRules(snapshot: MarketSnapshot): boolean {
  const change = changePct(snapshot);
  return change >= YANG_YONGXING_RULES.minChangePct &&
    change <= YANG_YONGXING_RULES.maxChangePct &&
    snapshot.totalMarketCapYuan > 0 &&
    snapshot.totalMarketCapYuan < YANG_YONGXING_RULES.maxMarketCapYuan &&
    snapshot.volumeRatio > YANG_YONGXING_RULES.minVolumeRatioExclusive &&
    snapshot.turnoverRatePct >= YANG_YONGXING_RULES.minTurnoverRatePct &&
    snapshot.turnoverRatePct <= YANG_YONGXING_RULES.maxTurnoverRatePct;
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

async function executeScan(requestedCodes?: string[]): Promise<LiveScreenResponse> {
  const today = new Date().toISOString().slice(0, 10);
  const universe = await marketDataProvider.getUniverse(today);
  const eligible = universe.filter((stock) => !stock.isSt && !/退/.test(stock.name));
  const requested = requestedCodes?.length
    ? new Set(requestedCodes)
    : undefined;
  const targetUniverse = requested
    ? eligible.filter((stock) => requested.has(stock.code))
    : eligible;

  if (targetUniverse.length === 0) throw new Error('股票池为空或代码不存在');

  const snapshots = await marketDataProvider.getSnapshots(
    targetUniverse.map((stock) => stock.code),
  );
  const tradeDate = tradeDateOf(snapshots);
  const nameByCode = new Map(targetUniverse.map((stock) => [stock.code, stock.name]));
  const realtimeCandidates = snapshots.filter(passesRealtimeRules);
  const warnings: string[] = [];

  if (snapshots.length < targetUniverse.length * 0.9) {
    warnings.push(`有效报价 ${snapshots.length}/${targetUniverse.length}，停牌或上游空数据股票已跳过`);
  }

  if (realtimeCandidates.length === 0) {
    return {
      strategy: 'yang-yongxing-tail-1430',
      tradeDate,
      generatedAt: new Date().toISOString(),
      source: marketDataProvider.name,
      historyMode: marketDataProvider.historyMode,
      isFallback: marketDataProvider.historyMode !== 'tushare',
      scanned: targetUniverse.length,
      quoted: snapshots.length,
      funnel: { realtimeRules: 0, recentLimitUp: 0, intradayConfirmed: 0 },
      results: [],
      nearMisses: [],
      warnings,
    };
  }

  const dailyBars = await marketDataProvider.getDailyBars(
    realtimeCandidates.map((snapshot) => snapshot.code),
    calendarDaysBefore(tradeDate, 70),
    tradeDate,
  );
  const dailyByCode = groupDailyBars(dailyBars);
  const withLimitUp = realtimeCandidates.filter((snapshot) => {
    const history = (dailyByCode.get(snapshot.code) ?? [])
      .filter((bar) => bar.date < tradeDate)
      .slice(-YANG_YONGXING_RULES.lookbackTradingDays);
    return history.some((bar) => bar.isLimitUp);
  });

  const minuteBars = await marketDataProvider.getMinuteBars(
    withLimitUp.map((snapshot) => snapshot.code),
    tradeDate,
  );
  const minutesByCode = new Map<string, typeof minuteBars>();
  for (const bar of minuteBars) {
    const current = minutesByCode.get(bar.code) ?? [];
    current.push(bar);
    minutesByCode.set(bar.code, current);
  }

  const results: LiveScreenMatch[] = [];
  const nearMisses: LiveScreenNearMiss[] = [];
  for (const snapshot of withLimitUp) {
    const history = (dailyByCode.get(snapshot.code) ?? [])
      .filter((bar) => bar.date < tradeDate)
      .slice(-YANG_YONGXING_RULES.lookbackTradingDays);
    const evaluation = evaluateYangYongxing({
      code: snapshot.code,
      name: nameByCode.get(snapshot.code) ?? snapshot.code,
      changePct: changePct(snapshot),
      totalMarketCapYuan: snapshot.totalMarketCapYuan,
      volumeRatio: snapshot.volumeRatio,
      turnoverRatePct: snapshot.turnoverRatePct,
      recentDailyBars: history,
      minuteBars: minutesByCode.get(snapshot.code) ?? [],
    });
    const row: LiveScreenMatch = {
      code: snapshot.code,
      name: evaluation.name,
      changePct: changePct(snapshot),
      totalMarketCapYuan: snapshot.totalMarketCapYuan,
      volumeRatio: snapshot.volumeRatio,
      turnoverRatePct: snapshot.turnoverRatePct,
      breakoutTime: evaluation.intraday.breakoutTime,
      breakoutLevel: evaluation.intraday.breakoutLevel,
      score: evaluation.score,
      checks: evaluation.checks,
    };
    if (evaluation.passed) {
      results.push(row);
    } else {
      nearMisses.push({
        ...row,
        failedRuleKey: 'tail_pattern',
        failedRuleLabel: '14:30 后分时走势',
        reason: evaluation.intraday.reason,
      });
    }
  }

  results.sort((a, b) => b.score - a.score || b.changePct - a.changePct);
  nearMisses.sort((a, b) => b.score - a.score || b.changePct - a.changePct);
  if (marketDataProvider.historyMode !== 'tushare') {
    warnings.push('未配置 Tushare，近30日涨停使用腾讯不复权日线按板块涨停幅度识别');
  }

  return {
    strategy: 'yang-yongxing-tail-1430',
    tradeDate,
    generatedAt: new Date().toISOString(),
    source: marketDataProvider.name,
    historyMode: marketDataProvider.historyMode,
    isFallback: marketDataProvider.historyMode !== 'tushare',
    scanned: targetUniverse.length,
    quoted: snapshots.length,
    funnel: {
      realtimeRules: realtimeCandidates.length,
      recentLimitUp: withLimitUp.length,
      intradayConfirmed: results.length,
    },
    results,
    nearMisses,
    warnings,
  };
}

export async function screenLiveYangYongxing(
  requestedCodes?: string[],
): Promise<LiveScreenResponse> {
  if (requestedCodes?.length) return executeScan(requestedCodes);
  if (cachedScan && cachedScan.expiresAt > Date.now()) return cachedScan.value;
  if (activeScan) return activeScan;
  activeScan = executeScan().then((value) => {
    cachedScan = { expiresAt: Date.now() + 5 * 60_000, value };
    return value;
  }).finally(() => {
    activeScan = undefined;
  });
  return activeScan;
}

