import { normalizePortfolioCode } from './validation.ts';
import {
  MAX_HOLDING_COST_PRICE,
  MAX_HOLDING_QUANTITY,
  MAX_HOLDINGS,
  MAX_WATCHLIST_ITEMS,
  PORTFOLIO_SCHEMA_VERSION,
  PORTFOLIO_STORAGE_KEY,
} from './types.ts';
import type { PortfolioHolding, PortfolioState } from './types.ts';

type PortfolioListener = () => void;
const serverSnapshot = createEmptyPortfolioState();
let clientSnapshot: PortfolioState | null = null;
const listeners = new Set<PortfolioListener>();

export function createEmptyPortfolioState(): PortfolioState {
  return {
    schemaVersion: PORTFOLIO_SCHEMA_VERSION,
    watchlist: [],
    holdings: [],
  };
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

function parseStoredHolding(value: unknown): PortfolioHolding | null {
  if (!isRecord(value) || typeof value.id !== 'string' || value.id.trim().length === 0) {
    return null;
  }
  const code = normalizePortfolioCode(value.code);
  if (!code.ok) return null;
  if (typeof value.quantity !== 'number' ||
      !Number.isSafeInteger(value.quantity) ||
      value.quantity < 1 ||
      value.quantity > MAX_HOLDING_QUANTITY) {
    return null;
  }
  if (typeof value.costPrice !== 'number' ||
      !Number.isFinite(value.costPrice) ||
      value.costPrice <= 0 ||
      value.costPrice > MAX_HOLDING_COST_PRICE) {
    return null;
  }
  return {
    id: value.id.slice(0, 120),
    code: code.value,
    quantity: value.quantity,
    costPrice: value.costPrice,
  };
}

/**
 * Parses only the current schema. Unknown or malformed versions are discarded
 * rather than being interpreted as holdings with an uncertain meaning.
 */
export function parsePortfolioState(value: unknown): PortfolioState {
  if (!isRecord(value) || value.schemaVersion !== PORTFOLIO_SCHEMA_VERSION) {
    return createEmptyPortfolioState();
  }

  const watchlist: string[] = [];
  if (Array.isArray(value.watchlist)) {
    for (const item of value.watchlist) {
      const code = normalizePortfolioCode(item);
      if (code.ok && !watchlist.includes(code.value)) {
        watchlist.push(code.value);
      }
      if (watchlist.length >= MAX_WATCHLIST_ITEMS) break;
    }
  }

  const holdings: PortfolioHolding[] = [];
  if (Array.isArray(value.holdings)) {
    for (const item of value.holdings) {
      const holding = parseStoredHolding(item);
      if (holding && !holdings.some((existing) => existing.id === holding.id)) {
        holdings.push(holding);
      }
      if (holdings.length >= MAX_HOLDINGS) break;
    }
  }

  return { schemaVersion: PORTFOLIO_SCHEMA_VERSION, watchlist, holdings };
}

export function loadPortfolioState(storage?: Pick<Storage, 'getItem'>): PortfolioState {
  if (!storage) return createEmptyPortfolioState();
  try {
    const raw = storage.getItem(PORTFOLIO_STORAGE_KEY);
    return raw ? parsePortfolioState(JSON.parse(raw) as unknown) : createEmptyPortfolioState();
  } catch {
    return createEmptyPortfolioState();
  }
}

export function savePortfolioState(
  state: PortfolioState,
  storage?: Pick<Storage, 'setItem'>,
): void {
  if (!storage) return;
  try {
    storage.setItem(PORTFOLIO_STORAGE_KEY, JSON.stringify(parsePortfolioState(state)));
  } catch {
    // Private browsing and quota errors should not make quote rendering fail.
  }
}

/** External-store helpers keep localStorage out of the server render path. */
export function getPortfolioSnapshot(): PortfolioState {
  if (typeof window === 'undefined') return serverSnapshot;
  if (!clientSnapshot) clientSnapshot = loadPortfolioState(window.localStorage);
  return clientSnapshot;
}

export function getServerPortfolioSnapshot(): PortfolioState {
  return serverSnapshot;
}

export function subscribeToPortfolio(listener: PortfolioListener): () => void {
  listeners.add(listener);
  return () => listeners.delete(listener);
}

export function setPortfolioSnapshot(nextState: PortfolioState): void {
  const parsed = parsePortfolioState(nextState);
  clientSnapshot = parsed;
  if (typeof window !== 'undefined') savePortfolioState(parsed, window.localStorage);
  for (const listener of listeners) listener();
}
