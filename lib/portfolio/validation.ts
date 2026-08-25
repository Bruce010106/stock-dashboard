import { normalizeTicker } from '../data/provider-utils.ts';
import {
  MAX_HOLDING_COST_PRICE,
  MAX_HOLDING_QUANTITY,
  MAX_WATCHLIST_ITEMS,
} from './types.ts';
import type { PortfolioHolding } from './types.ts';

export type PortfolioValidationResult<T> =
  | { ok: true; value: T }
  | { ok: false; error: string };

export type HoldingDraftInput = {
  code: unknown;
  quantity: unknown;
  costPrice: unknown;
};

/**
 * Normalizes the formats accepted by the existing providers while keeping the
 * public portfolio API restricted to six-digit A-share codes.
 */
export function normalizePortfolioCode(input: unknown): PortfolioValidationResult<string> {
  if (typeof input !== 'string' || input.trim().length === 0) {
    return { ok: false, error: '股票代码不能为空' };
  }
  if (input.trim().length > 24) {
    return { ok: false, error: '股票代码格式不正确' };
  }

  try {
    const code = normalizeTicker(input);
    if (!/^\d{6}$/.test(code)) {
      return { ok: false, error: '股票代码必须是 6 位数字' };
    }
    return { ok: true, value: code };
  } catch {
    return { ok: false, error: '股票代码必须是 6 位数字或带交易所前缀的代码' };
  }
}

/** Validates a comma-separated quote query and applies the server-side cap. */
export function validatePortfolioQuoteCodes(
  rawCodes: unknown,
): PortfolioValidationResult<string[]> {
  if (typeof rawCodes !== 'string' || rawCodes.trim().length === 0) {
    return { ok: false, error: 'codes 至少需要一个股票代码' };
  }

  const tokens = rawCodes.split(',').map((code) => code.trim());
  if (tokens.some((code) => code.length === 0)) {
    return { ok: false, error: 'codes 不允许包含空代码' };
  }
  if (tokens.length > MAX_WATCHLIST_ITEMS) {
    return { ok: false, error: `codes 数量不能超过 ${MAX_WATCHLIST_ITEMS}` };
  }

  const codes: string[] = [];
  for (const token of tokens) {
    const normalized = normalizePortfolioCode(token);
    if (!normalized.ok) {
      return { ok: false, error: `codes 包含无效股票代码：${token}` };
    }
    if (!codes.includes(normalized.value)) codes.push(normalized.value);
  }
  return codes.length > 0
    ? { ok: true, value: codes }
    : { ok: false, error: 'codes 至少需要一个股票代码' };
}

function parsePositiveNumber(value: unknown): number | null {
  if (typeof value === 'number') return Number.isFinite(value) ? value : null;
  if (typeof value !== 'string' || value.trim().length === 0) return null;
  const parsed = Number(value);
  return Number.isFinite(parsed) ? parsed : null;
}

export function validateHoldingDraft(
  input: HoldingDraftInput,
): PortfolioValidationResult<Omit<PortfolioHolding, 'id'>> {
  const code = normalizePortfolioCode(input.code);
  if (!code.ok) return code;

  const quantity = parsePositiveNumber(input.quantity);
  if (quantity === null || !Number.isSafeInteger(quantity) || quantity < 1 || quantity > MAX_HOLDING_QUANTITY) {
    return { ok: false, error: `持仓数量必须是 1-${MAX_HOLDING_QUANTITY} 的整数` };
  }

  const costPrice = parsePositiveNumber(input.costPrice);
  if (costPrice === null || costPrice <= 0 || costPrice > MAX_HOLDING_COST_PRICE) {
    return { ok: false, error: `成本价必须大于 0 且不超过 ${MAX_HOLDING_COST_PRICE}` };
  }

  return { ok: true, value: { code: code.value, quantity, costPrice } };
}
