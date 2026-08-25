import type {
  ScreenerFilters,
  ScreenerRow,
  ScreenResult,
} from './types';

export type SortKey =
  | 'name'
  | 'code'
  | 'lastPrice'
  | 'changePct'
  | 'totalMarketCapYuan'
  | 'amountYuan'
  | 'volumeRatio'
  | 'turnoverRatePct'
  | 'score';

export type SortDirection = 'asc' | 'desc';

const CAP_YUAN_PER_YI = 100_000_000;

function parsedBound(value: string): number | undefined {
  if (value.trim() === '') return undefined;
  const parsed = Number(value);
  return Number.isFinite(parsed) ? parsed : undefined;
}

function matchesRange(
  value: number | undefined,
  minimum: number | undefined,
  maximum: number | undefined,
): boolean {
  if (minimum === undefined && maximum === undefined) return true;
  if (value === undefined || !Number.isFinite(value)) return false;
  return (minimum === undefined || value >= minimum) &&
    (maximum === undefined || value <= maximum);
}

export function isStLike(row: Pick<ScreenResult, 'name' | 'isSt'>): boolean {
  return Boolean(row.isSt) || /^(?:\*?ST)|退/i.test(row.name.trim());
}

function toScreenerRows(
  strictResults: ScreenResult[],
  nearMisses: ScreenResult[],
): ScreenerRow[] {
  return [
    ...strictResults.map((row) => ({ ...row, conclusion: '严格命中' as const, reason: '' })),
    ...nearMisses.map((row) => ({
      ...row,
      conclusion: '近似候选' as const,
      reason: 'reason' in row && typeof row.reason === 'string' ? row.reason : '',
    })),
  ];
}

export function filterScreenerRows(
  rows: ScreenerRow[],
  filters: ScreenerFilters,
): ScreenerRow[] {
  const query = filters.query.trim().toLocaleLowerCase('zh-CN');
  const minPrice = parsedBound(filters.minPrice);
  const maxPrice = parsedBound(filters.maxPrice);
  const minChange = parsedBound(filters.minChange);
  const maxChange = parsedBound(filters.maxChange);
  const minMarketCap = parsedBound(filters.minMarketCapYi);
  const maxMarketCap = parsedBound(filters.maxMarketCapYi);
  const minAmount = parsedBound(filters.minAmountYi);
  const maxAmount = parsedBound(filters.maxAmountYi);

  return rows.filter((row) => {
    if (query && !`${row.code} ${row.name}`.toLocaleLowerCase('zh-CN').includes(query)) {
      return false;
    }
    if (filters.excludeSt && isStLike(row)) return false;
    if (!matchesRange(row.lastPrice, minPrice, maxPrice)) return false;
    if (!matchesRange(row.changePct, minChange, maxChange)) return false;
    if (!matchesRange(row.totalMarketCapYuan / CAP_YUAN_PER_YI, minMarketCap, maxMarketCap)) {
      return false;
    }
    if (!matchesRange(
      row.amountYuan === undefined ? undefined : row.amountYuan / CAP_YUAN_PER_YI,
      minAmount,
      maxAmount,
    )) return false;
    return true;
  });
}

export function combineScreenerRows(
  strictResults: ScreenResult[],
  nearMisses: ScreenResult[],
): ScreenerRow[] {
  return toScreenerRows(strictResults, nearMisses);
}

function valueForSort(row: ScreenerRow, key: SortKey): string | number | undefined {
  if (key === 'name' || key === 'code') return row[key];
  return row[key];
}

export function sortScreenerRows(
  rows: ScreenerRow[],
  key: SortKey,
  direction: SortDirection,
): ScreenerRow[] {
  const multiplier = direction === 'asc' ? 1 : -1;
  return rows
    .map((row, index) => ({ row, index }))
    .sort((a, b) => {
      const left = valueForSort(a.row, key);
      const right = valueForSort(b.row, key);
      const leftMissing = left === undefined || left === null || left === '' ||
        (typeof left === 'number' && !Number.isFinite(left));
      const rightMissing = right === undefined || right === null || right === '' ||
        (typeof right === 'number' && !Number.isFinite(right));
      if (leftMissing || rightMissing) {
        if (leftMissing && rightMissing) return a.index - b.index;
        return leftMissing ? 1 : -1;
      }
      const compared = typeof left === 'number' && typeof right === 'number'
        ? left - right
        : String(left).localeCompare(String(right), 'zh-CN');
      return compared * multiplier || a.row.code.localeCompare(b.row.code);
    })
    .map(({ row }) => row);
}

export function paginateScreenerRows<T>(
  rows: T[],
  requestedPage: number,
  pageSize: number,
): { items: T[]; page: number; pageCount: number } {
  const safePageSize = Number.isInteger(pageSize) && pageSize > 0 ? pageSize : 10;
  const pageCount = Math.max(1, Math.ceil(rows.length / safePageSize));
  const page = Math.min(Math.max(1, Math.trunc(requestedPage)), pageCount);
  const start = (page - 1) * safePageSize;
  return { items: rows.slice(start, start + safePageSize), page, pageCount };
}
