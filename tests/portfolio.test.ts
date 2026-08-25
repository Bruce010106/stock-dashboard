import assert from 'node:assert/strict';
import test from 'node:test';
import {
  calculateHoldingValuation,
  calculatePortfolioTotals,
} from '../lib/portfolio/calculations.ts';
import { parsePortfolioState } from '../lib/portfolio/storage.ts';
import {
  normalizePortfolioCode,
  validateHoldingDraft,
  validatePortfolioQuoteCodes,
} from '../lib/portfolio/validation.ts';
import type { PortfolioHolding, PortfolioQuote } from '../lib/portfolio/types.ts';

const quote: PortfolioQuote = {
  code: '600519',
  timestamp: '2026-08-25T10:00:00+08:00',
  lastPrice: 1_020,
  previousClose: 1_000,
  changePct: 2,
  volumeRatio: 1.2,
  turnoverRatePct: 0.5,
  totalMarketCapYuan: 1_000_000_000_000,
};

const holding: PortfolioHolding = {
  id: 'h-1',
  code: '600519',
  quantity: 100,
  costPrice: 1_000,
};

test('自选股代码复用 provider 归一化并拒绝超限请求', () => {
  assert.deepEqual(normalizePortfolioCode('SH600519'), { ok: true, value: '600519' });
  assert.deepEqual(normalizePortfolioCode('000001.SZ'), { ok: true, value: '000001' });
  assert.equal(normalizePortfolioCode('6005190').ok, false);

  const valid = validatePortfolioQuoteCodes('600519, SZ000001, 600519');
  assert.deepEqual(valid, { ok: true, value: ['600519', '000001'] });
  const invalid = validatePortfolioQuoteCodes(Array.from({ length: 51 }, () => '600519').join(','));
  assert.equal(invalid.ok, false);
});

test('持仓表单严格限制正整数数量和正成本价', () => {
  const valid = validateHoldingDraft({ code: '600519', quantity: '100', costPrice: '1000' });
  assert.equal(valid.ok, true);
  if (valid.ok) assert.deepEqual(valid.value, { code: '600519', quantity: 100, costPrice: 1000 });

  assert.equal(validateHoldingDraft({ code: '600519', quantity: '10.5', costPrice: '1000' }).ok, false);
  assert.equal(validateHoldingDraft({ code: '600519', quantity: '100', costPrice: '0' }).ok, false);
});

test('本地 schema 只接受当前版本并清理非法记录', () => {
  const parsed = parsePortfolioState({
    schemaVersion: 1,
    watchlist: ['SH600519', '600519', 'bad', '000001.SZ'],
    holdings: [
      holding,
      { ...holding, id: 'h-1', code: '000001' },
      { ...holding, id: 'h-2', code: 'bad' },
    ],
  });
  assert.deepEqual(parsed.watchlist, ['600519', '000001']);
  assert.equal(parsed.holdings.length, 1);
  assert.equal(parsePortfolioState({ schemaVersion: 0, watchlist: ['600519'], holdings: [] }).watchlist.length, 0);
});

test('真实报价存在时计算市值和盈亏，缺报价时不伪造价格', () => {
  const valuation = calculateHoldingValuation(holding, quote);
  assert.equal(valuation.marketValue, 102_000);
  assert.equal(valuation.pnl, 2_000);
  assert.equal(valuation.pnlPct, 2);

  const missing = calculateHoldingValuation(holding);
  assert.equal(missing.marketValue, null);
  assert.equal(missing.pnl, null);

  const other: PortfolioHolding = { id: 'h-2', code: '000001', quantity: 100, costPrice: 10 };
  const totals = calculatePortfolioTotals([holding, other], { '600519': quote });
  assert.equal(totals.pricedCount, 1);
  assert.equal(totals.marketValue, null);
  assert.equal(totals.partialMarketValue, 102_000);
  assert.equal(totals.pnl, null);
});
