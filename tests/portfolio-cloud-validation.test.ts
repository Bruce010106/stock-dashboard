import assert from 'node:assert/strict';
import test from 'node:test';
import {
  parsePortfolioCloudDeletePayload,
  parsePortfolioCloudMergePayload,
  parsePortfolioCloudWritePayload,
} from '../lib/portfolio/cloud-validation.ts';

test('cloud write validation normalizes and deduplicates codes and source ids', () => {
  const parsed = parsePortfolioCloudWritePayload({
    watchlist: ['sh600519', '600519', 'SZ000001'],
    holdings: [
      { id: 'lot-a', code: '600519', quantity: '100', costPrice: '100.5' },
      { id: 'lot-a', code: '600519', quantity: 200, costPrice: 99 },
      { source_id: 'lot-b', stock_code: '000001', quantity: 50, cost_price: 10 },
    ],
  });

  assert.equal(parsed.ok, true);
  if (!parsed.ok) return;
  assert.deepEqual(parsed.value.watchlist, ['600519', '000001']);
  assert.deepEqual(parsed.value.holdings, [
    { sourceId: 'lot-a', code: '600519', quantity: 100, costPrice: 100.5 },
    { sourceId: 'lot-b', code: '000001', quantity: 50, costPrice: 10 },
  ]);
});

test('cloud merge requires both collections and preserves same-stock lots', () => {
  const parsed = parsePortfolioCloudMergePayload({
    state: {
      schemaVersion: 1,
      watchlist: [],
      holdings: [
        { id: 'lot-a', code: '600519', quantity: 100, costPrice: 100 },
        { id: 'lot-b', code: '600519', quantity: 200, costPrice: 110 },
      ],
    },
  });

  assert.equal(parsed.ok, true);
  if (!parsed.ok) return;
  assert.equal(parsed.value.holdings.length, 2);
  assert.equal(parsePortfolioCloudMergePayload({ watchlist: [] }).ok, false);
});

test('cloud delete validation accepts ids and codes', () => {
  const parsed = parsePortfolioCloudDeletePayload({
    watchlist: ['600519', '600519'],
    holdings: [{ id: 'lot-a' }, 'lot-b'],
  });
  assert.equal(parsed.ok, true);
  if (!parsed.ok) return;
  assert.deepEqual(parsed.value, {
    watchlist: ['600519'],
    holdingIds: ['lot-a', 'lot-b'],
  });
});
