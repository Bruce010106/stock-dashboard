import assert from 'node:assert/strict';
import test from 'node:test';
import {
  evaluateTailPattern,
  evaluateYangYongxing,
  type DailyBar,
  type MinuteBar,
} from '../lib/strategies/yang-yongxing.ts';

const history: DailyBar[] = Array.from({ length: 30 }, (_, index) => ({
  date: `2026-07-${String(index + 1).padStart(2, '0')}`,
  close: 10 + index / 10,
  isLimitUp: index === 12,
}));

const passingMinutes: MinuteBar[] = [
  { time: '14:29', high: 10.5, low: 10.4, close: 10.45 },
  { time: '14:30', high: 10.48, low: 10.42, close: 10.46 },
  { time: '14:36', high: 10.62, low: 10.51, close: 10.6 },
  { time: '14:40', high: 10.68, low: 10.56, close: 10.61 },
  { time: '14:48', high: 10.64, low: 10.52, close: 10.58 },
  { time: '15:00', high: 10.66, low: 10.55, close: 10.63 },
];

test('通过全部六项筛选条件', () => {
  const result = evaluateYangYongxing({
    code: '002892',
    name: '示例股票',
    changePct: 4.2,
    totalMarketCapYuan: 8_600_000_000,
    volumeRatio: 1.7,
    turnoverRatePct: 7.2,
    recentDailyBars: history,
    minuteBars: passingMinutes,
  });

  assert.equal(result.passed, true);
  assert.equal(result.checks.every((check) => check.passed), true);
  assert.equal(result.intraday.breakoutTime, '14:36');
});

test('总市值等于 200 亿元时不通过严格小于条件', () => {
  const result = evaluateYangYongxing({
    code: '000001',
    name: '边界示例',
    changePct: 3,
    totalMarketCapYuan: 20_000_000_000,
    volumeRatio: 1.01,
    turnoverRatePct: 10,
    recentDailyBars: history,
    minuteBars: passingMinutes,
  });

  assert.equal(result.passed, false);
  assert.equal(
    result.checks.find((check) => check.key === 'market_cap')?.passed,
    false,
  );
});

test('创新高后跌破突破位时分时形态不通过', () => {
  const result = evaluateTailPattern([
    { time: '14:29', high: 10.5, low: 10.4, close: 10.45 },
    { time: '14:35', high: 10.7, low: 10.52, close: 10.65 },
    { time: '14:46', high: 10.66, low: 10.43, close: 10.48 },
    { time: '15:00', high: 10.55, low: 10.46, close: 10.49 },
  ]);

  assert.equal(result.passed, false);
  assert.match(result.reason, /跌破突破位/);
});
