import assert from 'node:assert/strict';
import test from 'node:test';
import {
  combineScreenerRows,
  filterScreenerRows,
  paginateScreenerRows,
  sortScreenerRows,
} from '../components/screener/screener-utils.ts';
import { DEFAULT_SCREENER_FILTERS, type ScreenResult } from '../components/screener/types.ts';

function row(overrides: Partial<ScreenResult> = {}): ScreenResult {
  return {
    code: '000001',
    name: '平安银行',
    lastPrice: 12.5,
    changePct: 3.5,
    totalMarketCapYuan: 100_000_000_000,
    amountYuan: 2_000_000_000,
    volumeRatio: 1.5,
    turnoverRatePct: 6,
    score: 90,
    ...overrides,
  };
}

test('二次筛选支持代码、价格、涨幅、市值、成交额和 ST 排除', () => {
  const rows = combineScreenerRows([
    row(),
    row({ code: '600002', name: 'ST示例', lastPrice: 8, changePct: 2, amountYuan: 500_000_000 }),
  ], []);
  const filtered = filterScreenerRows(rows, {
    ...DEFAULT_SCREENER_FILTERS,
    query: '000001',
    minPrice: '12',
    maxPrice: '13',
    minChange: '3',
    maxChange: '4',
    minMarketCapYi: '900',
    maxMarketCapYi: '1100',
    minAmountYi: '10',
    maxAmountYi: '30',
  });
  assert.deepEqual(filtered.map((item) => item.code), ['000001']);
});

test('排序把缺失数值放到末尾并保留稳定顺序', () => {
  const rows = combineScreenerRows([
    row({ code: '000003', changePct: 1 }),
    row({ code: '000001', changePct: 5 }),
    row({ code: '000002', lastPrice: undefined, changePct: 3 }),
  ], []);
  const sorted = sortScreenerRows(rows, 'changePct', 'desc');
  assert.deepEqual(sorted.map((item) => item.code), ['000001', '000002', '000003']);
  const priceSorted = sortScreenerRows(rows, 'lastPrice', 'asc');
  assert.equal(priceSorted.at(-1)?.code, '000002');
});

test('分页会裁剪越界页码并返回正确片段', () => {
  const rows = Array.from({ length: 25 }, (_, index) => index);
  assert.deepEqual(paginateScreenerRows(rows, 2, 10), {
    items: rows.slice(10, 20),
    page: 2,
    pageCount: 3,
  });
  assert.deepEqual(paginateScreenerRows(rows, 99, 10), {
    items: rows.slice(20),
    page: 3,
    pageCount: 3,
  });
});
