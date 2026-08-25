export type PerformanceSignal = { signalDate: string; returnPct: number };
export type PerformanceRange = 'all' | '30d' | '60d' | '90d';
export type PerformancePoint = { date: string; equity: number; cumulativeReturnPct: number; drawdownPct: number; signalCount: number };
export type PerformanceSummary = { signalCount: number; winningSignalCount: number; winRatePct: number; averageReturnPct: number; cumulativeReturnPct: number; maxDrawdownPct: number; bestReturnPct: number; worstReturnPct: number; startDate: string | null; endDate: string | null };
export type PerformanceRangeBounds = { startDate: string | null; endDate: string | null };

const DAY_MS = 86_400_000;
function round(value: number, digits = 2): number { const factor = 10 ** digits; return Math.round((value + Number.EPSILON) * factor) / factor; }
function isDateOnly(value: string): boolean {
  if (!/^\d{4}-\d{2}-\d{2}$/.test(value)) return false;
  const [year, month, day] = value.split('-').map(Number);
  const date = new Date(Date.UTC(year, month - 1, day));
  return date.getUTCFullYear() === year && date.getUTCMonth() === month - 1 && date.getUTCDate() === day;
}
function shiftDate(date: string, days: number): string { const [year, month, day] = date.split('-').map(Number); return new Date(Date.UTC(year, month - 1, day) + days * DAY_MS).toISOString().slice(0, 10); }
function validSignals(signals: readonly PerformanceSignal[]): PerformanceSignal[] { return signals.filter((signal) => isDateOnly(signal.signalDate) && Number.isFinite(signal.returnPct)).map((signal) => ({ signalDate: signal.signalDate, returnPct: signal.returnPct })); }
function latestSignalDate(signals: readonly PerformanceSignal[]): string | null { return validSignals(signals).map((signal) => signal.signalDate).sort().at(-1) ?? null; }
function earliestSignalDate(signals: readonly PerformanceSignal[]): string | null { return validSignals(signals).map((signal) => signal.signalDate).sort().at(0) ?? null; }

export function getPerformanceRangeBounds(signals: readonly PerformanceSignal[], range: PerformanceRange, requestedEndDate?: string): PerformanceRangeBounds {
  const effectiveEndDate = requestedEndDate && isDateOnly(requestedEndDate) ? requestedEndDate : latestSignalDate(signals);
  if (!effectiveEndDate) return { startDate: null, endDate: null };
  if (range === 'all') return { startDate: earliestSignalDate(signals), endDate: effectiveEndDate };
  const rangeDays: Record<Exclude<PerformanceRange, 'all'>, number> = { '30d': 30, '60d': 60, '90d': 90 };
  return { startDate: shiftDate(effectiveEndDate, 1 - rangeDays[range]), endDate: effectiveEndDate };
}

export function filterPerformanceSignals(signals: readonly PerformanceSignal[], range: PerformanceRange, requestedEndDate?: string): PerformanceSignal[] {
  const bounds = getPerformanceRangeBounds(signals, range, requestedEndDate);
  if (!bounds.startDate || !bounds.endDate) return [];
  return validSignals(signals).filter((signal) => signal.signalDate >= bounds.startDate! && signal.signalDate <= bounds.endDate!);
}

export function buildPerformanceSeries(signals: readonly PerformanceSignal[]): PerformancePoint[] {
  const grouped = new Map<string, number[]>();
  for (const signal of validSignals(signals)) { const returns = grouped.get(signal.signalDate) ?? []; returns.push(signal.returnPct); grouped.set(signal.signalDate, returns); }
  let equity = 100;
  let peak = equity;
  return [...grouped.entries()].sort(([left], [right]) => left.localeCompare(right)).map(([date, returns]) => {
    const groupFactor = returns.reduce((factor, returnPct) => factor * (1 + returnPct / 100), 1);
    equity *= groupFactor;
    peak = Math.max(peak, equity);
    return { date, equity: round(equity, 4), cumulativeReturnPct: round((equity / 100 - 1) * 100), drawdownPct: round((equity / peak - 1) * 100), signalCount: returns.length };
  });
}

export function summarizePerformance(signals: readonly PerformanceSignal[], series = buildPerformanceSeries(signals)): PerformanceSummary {
  const cleanSignals = validSignals(signals);
  const returns = cleanSignals.map((signal) => signal.returnPct);
  const winningSignalCount = returns.filter((returnPct) => returnPct > 0).length;
  const lastPoint = series.at(-1);
  const maxDrawdownPct = series.length ? Math.min(...series.map((point) => point.drawdownPct)) : 0;
  return {
    signalCount: returns.length,
    winningSignalCount,
    winRatePct: round(returns.length ? winningSignalCount / returns.length * 100 : 0),
    averageReturnPct: round(returns.length ? returns.reduce((sum, returnPct) => sum + returnPct, 0) / returns.length : 0),
    cumulativeReturnPct: lastPoint?.cumulativeReturnPct ?? 0,
    maxDrawdownPct: round(maxDrawdownPct),
    bestReturnPct: returns.length ? round(Math.max(...returns)) : 0,
    worstReturnPct: returns.length ? round(Math.min(...returns)) : 0,
    startDate: earliestSignalDate(cleanSignals),
    endDate: latestSignalDate(cleanSignals),
  };
}
