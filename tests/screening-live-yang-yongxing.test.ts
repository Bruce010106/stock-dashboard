import assert from 'node:assert/strict';
import test from 'node:test';
import { describeLiveScreenSource } from '../lib/screening/live-yang-yongxing.ts';

test('describeLiveScreenSource never claims Tushare when history fell back to Tencent', () => {
  const source = describeLiveScreenSource('tencent-fallback');
  assert.doesNotMatch(source, /Tushare/);
  assert.match(source, /腾讯历史日线降级/);
});

test('describeLiveScreenSource credits Tushare only when it was actually used', () => {
  const source = describeLiveScreenSource('tushare');
  assert.match(source, /Tushare 历史日线/);
});
