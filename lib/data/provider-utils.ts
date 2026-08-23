import type { StockInstrument } from './market-data-provider.ts';

const TICKER_PATTERN = /^(?:(sh|sz|bj)(\d{6})|(\d{6})(?:\.(sh|sz|bj))?)$/i;

export function normalizeTicker(input: string): string {
  const match = TICKER_PATTERN.exec(String(input).trim());
  if (!match) {
    throw new Error(`无法识别股票代码：${input}`);
  }
  return match[2] ?? match[3];
}

export function exchangeOf(code: string): StockInstrument['exchange'] {
  const normalized = normalizeTicker(code);
  if (normalized.startsWith('92') || /^(43|83|87)/.test(normalized)) {
    return 'BJ';
  }
  if (/^[569]/.test(normalized)) return 'SH';
  return 'SZ';
}

export function tencentTicker(code: string): string {
  const normalized = normalizeTicker(code);
  return `${exchangeOf(normalized).toLowerCase()}${normalized}`;
}

export function tushareTicker(code: string): string {
  const normalized = normalizeTicker(code);
  return `${normalized}.${exchangeOf(normalized)}`;
}

export function formatDateCompact(date: string): string {
  return date.replaceAll('-', '');
}

export function formatDateDashed(date: string): string {
  return date.length === 8
    ? `${date.slice(0, 4)}-${date.slice(4, 6)}-${date.slice(6, 8)}`
    : date;
}

export function chunked<T>(items: T[], size: number): T[][] {
  const chunks: T[][] = [];
  for (let index = 0; index < items.length; index += size) {
    chunks.push(items.slice(index, index + size));
  }
  return chunks;
}

export function parseNumber(value: unknown): number {
  const parsed = Number(value);
  return Number.isFinite(parsed) ? parsed : 0;
}

export function isStName(name: string): boolean {
  return /^(?:\*?ST)|退$/i.test(name.trim());
}

export function inferLimitRate(code: string, isSt = false): number {
  if (isSt) return 0.05;
  const normalized = normalizeTicker(code);
  if (/^(300|301|688|689)/.test(normalized)) return 0.2;
  if (normalized.startsWith('92')) return 0.3;
  return 0.1;
}

export function isApproximateLimitUp(
  code: string,
  close: number,
  previousClose: number,
  isSt = false,
): boolean {
  if (close <= 0 || previousClose <= 0) return false;
  const expected = previousClose * (1 + inferLimitRate(code, isSt));
  return Math.abs(close - expected) <= 0.011;
}

