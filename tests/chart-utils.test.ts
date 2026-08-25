import assert from 'node:assert/strict';
import test from 'node:test';
import {
  buildPerformanceSeries,
  filterPerformanceSignals,
  getPerformanceRangeBounds,
  summarizePerformance,
  type PerformanceSignal,
} from '../lib/backtest/chart-utils.ts';

const signals: PerformanceSignal[] = [
  { signalDate: '2026-01-05', returnPct: 10 },
  { signalDate: '2026-01-05', returnPct: -5 },
  { signalDate: '2026-02-10', returnPct: -20 },
  { signalDate: '2026-03-20', returnPct: 8 },
];

test('buildPerformanceSeries compounds same-day signals and tracks drawdown', () => {
  const series = buildPerformanceSeries(signals);

  assert.equal(series.length, 3);
  assert.equal(series[0].date, '2026-01-05');
  assert.equal(series[0].signalCount, 2);
  assert.equal(series[0].equity, 104.5);
  assert.equal(series[0].cumulativeReturnPct, 4.5);
  assert.equal(series[0].drawdownPct, 0);
  assert.equal(series[1].cumulativeReturnPct, -16.4);
  assert.equal(series[1].drawdownPct, -20);
  assert.equal(series[2].drawdownPct, -13.6);
});

test('range tabs use inclusive calendar bounds and never add signals', () => {
  const bounds = getPerformanceRangeBounds(signals, '30d', '2026-03-20');
  assert.deepEqual(bounds, { startDate: '2026-02-19', endDate: '2026-03-20' });
  assert.deepEqual(filterPerformanceSignals(signals, '30d', '2026-03-20'), [
    signals[3],
  ]);
  assert.deepEqual(filterPerformanceSignals(signals, 'all', '2026-03-20'), signals);
});

test('invalid signals are ignored and empty summary remains safe', () => {
  const invalid = [
    { signalDate: 'not-a-date', returnPct: 12 },
    { signalDate: '2026-02-31', returnPct: 5 },
    { signalDate: '2026-02-01', returnPct: Number.NaN },
  ];

  assert.deepEqual(buildPerformanceSeries(invalid), []);
  assert.deepEqual(summarizePerformance(invalid), {
    signalCount: 0,
    winningSignalCount: 0,
    winRatePct: 0,
    averageReturnPct: 0,
    cumulativeReturnPct: 0,
    maxDrawdownPct: 0,
    bestReturnPct: 0,
    worstReturnPct: 0,
    startDate: null,
    endDate: null,
  });
});
