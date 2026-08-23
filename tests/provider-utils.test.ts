import assert from 'node:assert/strict';
import test from 'node:test';
import {
  exchangeOf,
  isApproximateLimitUp,
  normalizeTicker,
  tencentTicker,
  tushareTicker,
} from '../lib/data/provider-utils.ts';

test('归一化沪深京股票代码并生成上游代码', () => {
  assert.equal(normalizeTicker('SH600519'), '600519');
  assert.equal(normalizeTicker('000001.SZ'), '000001');
  assert.equal(exchangeOf('920982'), 'BJ');
  assert.equal(tencentTicker('600519'), 'sh600519');
  assert.equal(tushareTicker('300750'), '300750.SZ');
});

test('拒绝模糊或错误格式', () => {
  assert.throws(() => normalizeTicker('foo600519bar'));
  assert.throws(() => normalizeTicker('6005190'));
});

test('按主板、创业板和北交所涨停幅度识别涨停', () => {
  assert.equal(isApproximateLimitUp('600001', 11, 10), true);
  assert.equal(isApproximateLimitUp('300001', 12, 10), true);
  assert.equal(isApproximateLimitUp('920982', 13, 10), true);
  assert.equal(isApproximateLimitUp('300001', 11, 10), false);
});

