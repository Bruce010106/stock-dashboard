import assert from 'node:assert/strict';
import test from 'node:test';
import {
  backtestSourceMode,
  resultSignalWord,
  signalPriceBasisCopy,
} from '../lib/backtest/result-copy.ts';

test('backtestSourceMode reflects the server response, not client detection, once a result exists', () => {
  // A completed Tushare-configured run that actually returned an
  // approximate (Sina) result must still be reported as free/approximate.
  assert.equal(backtestSourceMode(true, true, true), 'free');
  assert.equal(backtestSourceMode(true, false, false), 'tushare');
});

test('backtestSourceMode falls back to client detection before a result exists', () => {
  assert.equal(backtestSourceMode(false, false, null), 'detecting');
  assert.equal(backtestSourceMode(false, false, true), 'tushare');
  assert.equal(backtestSourceMode(false, false, false), 'free');
});

test('signalPriceBasisCopy never claims a 1-minute close for the free/approximate mode', () => {
  assert.match(signalPriceBasisCopy('tushare'), /1 分钟收盘价/);
  assert.doesNotMatch(signalPriceBasisCopy('free'), /最后 1 分钟收盘价$/);
  assert.match(signalPriceBasisCopy('free'), /5 分钟 K 线收盘价/);
});

test('resultSignalWord labels approximate results as 近似, never 严格', () => {
  assert.equal(resultSignalWord(true), '近似');
  assert.equal(resultSignalWord(false), '严格');
});
