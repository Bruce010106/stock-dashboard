import {
  MAX_HOLDINGS,
  MAX_WATCHLIST_ITEMS,
} from './types.ts';
import {
  normalizePortfolioCode,
  validateHoldingDraft,
} from './validation.ts';

export type CloudHoldingInput = {
  sourceId: string;
  code: string;
  quantity: number;
  costPrice: number;
};

export type CloudWritePayload = {
  watchlist?: string[];
  holdings?: CloudHoldingInput[];
};

export type CloudDeletePayload = {
  watchlist: string[];
  holdingIds: string[];
};

export type CloudValidationResult<T> =
  | { ok: true; value: T }
  | { ok: false; error: string };

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

function getString(value: unknown): string | null {
  if (typeof value !== 'string') return null;
  const normalized = value.trim();
  return normalized.length > 0 ? normalized : null;
}

function parseSourceId(value: unknown): string | null {
  const sourceId = getString(value);
  return sourceId && sourceId.length <= 120 ? sourceId : null;
}

function parseWatchlist(value: unknown): CloudValidationResult<string[]> {
  if (!Array.isArray(value)) {
    return { ok: false, error: 'watchlist 必须是股票代码数组' };
  }
  if (value.length > MAX_WATCHLIST_ITEMS) {
    return { ok: false, error: `watchlist 数量不能超过 ${MAX_WATCHLIST_ITEMS}` };
  }

  const watchlist: string[] = [];
  for (const item of value) {
    const normalized = normalizePortfolioCode(item);
    if (!normalized.ok) return { ok: false, error: normalized.error };
    if (!watchlist.includes(normalized.value)) watchlist.push(normalized.value);
  }
  return { ok: true, value: watchlist };
}

function parseHolding(value: unknown): CloudValidationResult<CloudHoldingInput> {
  if (!isRecord(value)) {
    return { ok: false, error: 'holdings 包含无效持仓记录' };
  }

  const sourceId = parseSourceId(value.sourceId ?? value.source_id ?? value.id);
  if (!sourceId) {
    return { ok: false, error: '每条持仓都必须包含有效的 sourceId' };
  }

  const validated = validateHoldingDraft({
    code: value.code ?? value.stockCode ?? value.stock_code,
    quantity: value.quantity,
    costPrice: value.costPrice ?? value.cost_price,
  });
  if (!validated.ok) return { ok: false, error: validated.error };

  return {
    ok: true,
    value: {
      sourceId,
      ...validated.value,
    },
  };
}

function parseHoldings(value: unknown): CloudValidationResult<CloudHoldingInput[]> {
  if (!Array.isArray(value)) {
    return { ok: false, error: 'holdings 必须是数组' };
  }
  if (value.length > MAX_HOLDINGS) {
    return { ok: false, error: `holdings 数量不能超过 ${MAX_HOLDINGS}` };
  }

  const holdings: CloudHoldingInput[] = [];
  const sourceIds = new Set<string>();
  for (const item of value) {
    const parsed = parseHolding(item);
    if (!parsed.ok) return parsed;
    // The first copy wins. This makes a repeated client source_id idempotent
    // without allowing a duplicate entry to silently overwrite another value
    // within the same request.
    if (sourceIds.has(parsed.value.sourceId)) continue;
    sourceIds.add(parsed.value.sourceId);
    holdings.push(parsed.value);
  }
  return { ok: true, value: holdings };
}

/**
 * Validates an incremental cloud write. Both arrays are optional, but at
 * least one must be present so an accidental empty request is rejected.
 */
export function parsePortfolioCloudWritePayload(
  input: unknown,
): CloudValidationResult<CloudWritePayload> {
  if (!isRecord(input)) return { ok: false, error: '请求体必须是 JSON 对象' };

  const hasWatchlist = Object.hasOwn(input, 'watchlist');
  const hasHoldings = Object.hasOwn(input, 'holdings');
  if (!hasWatchlist && !hasHoldings) {
    return { ok: false, error: '请求体至少需要 watchlist 或 holdings' };
  }

  const result: CloudWritePayload = {};
  if (hasWatchlist) {
    const watchlist = parseWatchlist(input.watchlist);
    if (!watchlist.ok) return watchlist;
    result.watchlist = watchlist.value;
  }
  if (hasHoldings) {
    const holdings = parseHoldings(input.holdings);
    if (!holdings.ok) return holdings;
    result.holdings = holdings.value;
  }
  return { ok: true, value: result };
}

/**
 * Validates the full local payload used by the first-login merge endpoint.
 * A nested `state` object is accepted to mirror PortfolioState callers.
 */
export function parsePortfolioCloudMergePayload(
  input: unknown,
): CloudValidationResult<Required<CloudWritePayload>> {
  if (!isRecord(input)) return { ok: false, error: '请求体必须是 JSON 对象' };
  const source = isRecord(input.state) ? input.state : input;
  if (!Object.hasOwn(source, 'watchlist') || !Object.hasOwn(source, 'holdings')) {
    return { ok: false, error: '合并请求必须同时包含 watchlist 和 holdings' };
  }

  const parsed = parsePortfolioCloudWritePayload(source);
  if (!parsed.ok) return parsed;
  if (!parsed.value.watchlist || !parsed.value.holdings) {
    return { ok: false, error: '合并请求必须同时包含 watchlist 和 holdings' };
  }
  return {
    ok: true,
    value: {
      watchlist: parsed.value.watchlist,
      holdings: parsed.value.holdings,
    },
  };
}

function parseHoldingIds(value: unknown): CloudValidationResult<string[]> {
  if (!Array.isArray(value)) return { ok: false, error: 'holdingIds 必须是数组' };
  if (value.length > MAX_HOLDINGS) return { ok: false, error: 'holdingIds 数量过多' };
  const values: string[] = [];
  for (const item of value) {
    const raw = isRecord(item)
      ? item.sourceId ?? item.source_id ?? item.id
      : item;
    const sourceId = parseSourceId(raw);
    if (!sourceId) return { ok: false, error: 'holdingIds 包含无效标识' };
    if (!values.includes(sourceId)) values.push(sourceId);
  }
  return { ok: true, value: values };
}

/** Validates deletion by client source IDs and normalized stock codes. */
export function parsePortfolioCloudDeletePayload(
  input: unknown,
): CloudValidationResult<CloudDeletePayload> {
  if (!isRecord(input)) return { ok: false, error: '请求体必须是 JSON 对象' };
  const watchlistInput = input.watchlist ?? input.watchlistCodes ?? input.codes;
  const holdingInput = input.holdingIds ?? input.holdings;
  if (watchlistInput === undefined && holdingInput === undefined) {
    return { ok: false, error: '请求体至少需要 watchlist 或 holdingIds' };
  }

  const result: CloudDeletePayload = { watchlist: [], holdingIds: [] };
  if (watchlistInput !== undefined) {
    const parsed = parseWatchlist(watchlistInput);
    if (!parsed.ok) return parsed;
    result.watchlist = parsed.value;
  }
  if (holdingInput !== undefined) {
    const parsed = parseHoldingIds(holdingInput);
    if (!parsed.ok) return parsed;
    result.holdingIds = parsed.value;
  }
  return { ok: true, value: result };
}
