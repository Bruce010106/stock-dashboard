import assert from 'node:assert/strict';
import test from 'node:test';
import {
  MAX_CANDIDATES_PER_REQUEST,
  MAX_EVENTS_PER_REQUEST,
  validateLiveBacktestQuery,
  validateYangYongxingBacktestPayload,
  validateYangYongxingStrategyPayload,
} from '../lib/api-validation.ts';

const candidate = {
  code: '000001',
  name: '示例股票',
  changePct: 4,
  totalMarketCapYuan: 10_000_000_000,
  volumeRatio: 1.5,
  turnoverRatePct: 7,
  recentDailyBars: [{ date: '2026-07-01', close: 10, isLimitUp: true }],
  minuteBars: [
    { time: '14:30', high: 10.2, low: 10, close: 10.1 },
  ],
};

test('策略请求校验完整候选股结构并保留 explain', () => {
  const result = validateYangYongxingStrategyPayload({ candidates: [candidate], explain: true });

  assert.equal(result.ok, true);
  if (result.ok) {
    assert.equal(result.value.candidates[0]?.code, '000001');
    assert.equal(result.value.explain, true);
  }
});

test('策略请求拒绝缺少嵌套字段和非有限数字', () => {
  const missingBars = validateYangYongxingStrategyPayload({
    candidates: [{ ...candidate, minuteBars: undefined }],
  });
  assert.equal(missingBars.ok, false);

  const nonFinite = validateYangYongxingStrategyPayload({
    candidates: [{ ...candidate, changePct: Infinity }],
  });
  assert.equal(nonFinite.ok, false);
  if (!nonFinite.ok) assert.match(nonFinite.error, /changePct/);
});

test('策略请求拒绝超过批量上限', () => {
  const result = validateYangYongxingStrategyPayload({
    candidates: Array.from({ length: MAX_CANDIDATES_PER_REQUEST + 1 }, () => candidate),
  });
  assert.equal(result.ok, false);
});

test('回测请求校验事件、日期、价格和默认持有天数', () => {
  const result = validateYangYongxingBacktestPayload({
    events: [{
      signalDate: '2026-07-01',
      candidate,
      signalPrice: 10,
      futureCloses: [{ tradingDaysAfter: 5, close: 10.8 }],
    }],
  });

  assert.equal(result.ok, true);
  if (result.ok) assert.equal(result.value.holdingTradingDays, 5);

  const invalidDate = validateYangYongxingBacktestPayload({
    events: [{
      signalDate: '2026-02-30',
      candidate,
      signalPrice: 10,
      futureCloses: [],
    }],
  });
  assert.equal(invalidDate.ok, false);
});

test('回测请求拒绝非法持有天数和超限事件批次', () => {
  const invalidHoldingDays = validateYangYongxingBacktestPayload({
    events: [],
    holdingTradingDays: 0,
  });
  assert.equal(invalidHoldingDays.ok, false);

  const tooManyEvents = validateYangYongxingBacktestPayload({
    events: Array.from({ length: MAX_EVENTS_PER_REQUEST + 1 }, () => ({
      signalDate: '2026-07-01',
      candidate,
      signalPrice: 10,
      futureCloses: [],
    })),
  });
  assert.equal(tooManyEvents.ok, false);
});

test('真实回测查询归一化代码并限制区间和持有期', () => {
  const valid = validateLiveBacktestQuery(new URLSearchParams({
    codes: 'SZ000001,600519.sh,000001',
    startDate: '2026-06-01',
    endDate: '2026-08-24',
    holdingTradingDays: '5',
  }));
  assert.equal(valid.ok, true);
  if (valid.ok) assert.deepEqual(valid.value.codes, ['000001', '600519']);

  const tooLong = validateLiveBacktestQuery(new URLSearchParams({
    codes: '000001',
    startDate: '2026-01-01',
    endDate: '2026-08-24',
    holdingTradingDays: '2',
  }));
  assert.equal(tooLong.ok, false);
});
