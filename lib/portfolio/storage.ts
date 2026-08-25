import { normalizePortfolioCode } from './validation.ts';
import {
  MAX_HOLDING_COST_PRICE,
  MAX_HOLDING_QUANTITY,
  MAX_HOLDINGS,
  MAX_WATCHLIST_ITEMS,
  PORTFOLIO_MIGRATION_STATUS_PREFIX,
  PORTFOLIO_SCHEMA_VERSION,
  PORTFOLIO_STORAGE_KEY,
  PORTFOLIO_USER_STORAGE_PREFIX,
} from './types.ts';
import type {
  PortfolioCloudResponse,
  PortfolioHolding,
  PortfolioMigrationStatus,
  PortfolioState,
} from './types.ts';

type PortfolioListener = () => void;
type StorageReader = Pick<Storage, 'getItem'>;
type StorageWriter = Pick<Storage, 'setItem'>;
type StorageAccess = StorageReader & StorageWriter;

const serverSnapshot = createEmptyPortfolioState();
let clientSnapshot: PortfolioState | null = null;
let activePortfolioUserId: string | null = null;
let activePortfolioStorageKey = PORTFOLIO_STORAGE_KEY;
const listeners = new Set<PortfolioListener>();

/** The browser key used for anonymous records is intentionally kept stable. */
export function getPortfolioStorageKey(userId?: string | null): string {
  const normalizedUserId = normalizePortfolioUserId(userId);
  return normalizedUserId
    ? `${PORTFOLIO_USER_STORAGE_PREFIX}${encodeURIComponent(normalizedUserId)}`
    : PORTFOLIO_STORAGE_KEY;
}

function getPortfolioMigrationStorageKey(userId: string): string {
  return `${PORTFOLIO_MIGRATION_STATUS_PREFIX}${encodeURIComponent(userId)}`;
}

function normalizePortfolioUserId(userId: string | null | undefined): string | null {
  if (typeof userId !== 'string') return null;
  const normalized = userId.trim();
  return normalized.length > 0 ? normalized : null;
}

function getBrowserStorage(): StorageAccess | undefined {
  if (typeof window === 'undefined') return undefined;
  try {
    return window.localStorage;
  } catch {
    return undefined;
  }
}

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
    id: value.id.trim().slice(0, 120),
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

export function loadPortfolioState(
  storage?: StorageReader,
  storageKey = PORTFOLIO_STORAGE_KEY,
): PortfolioState {
  if (!storage) return createEmptyPortfolioState();
  try {
    const raw = storage.getItem(storageKey);
    return raw ? parsePortfolioState(JSON.parse(raw) as unknown) : createEmptyPortfolioState();
  } catch {
    return createEmptyPortfolioState();
  }
}

export function savePortfolioState(
  state: PortfolioState,
  storage?: StorageWriter,
  storageKey = PORTFOLIO_STORAGE_KEY,
): void {
  if (!storage) return;
  try {
    storage.setItem(storageKey, JSON.stringify(parsePortfolioState(state)));
  } catch {
    // Private browsing and quota errors should not make quote rendering fail.
  }
}

export function hasPortfolioData(state: PortfolioState): boolean {
  return state.watchlist.length > 0 || state.holdings.length > 0;
}

function holdingMatches(left: PortfolioHolding, right: PortfolioHolding): boolean {
  return left.id === right.id &&
    left.code === right.code &&
    left.quantity === right.quantity &&
    left.costPrice === right.costPrice;
}

function createUniqueHoldingId(baseId: string, usedIds: Set<string>): string {
  const base = baseId.slice(0, 120);
  let candidate = base;
  let suffix = 1;
  while (usedIds.has(candidate)) {
    const marker = `-import-${suffix}`;
    candidate = `${base.slice(0, Math.max(1, 120 - marker.length))}${marker}`;
    suffix += 1;
  }
  return candidate;
}

/**
 * Merges cloud records with browser records without silently dropping a
 * holding. Watchlist codes are set-like; holding IDs are preserved unless the
 * exact same record is encountered twice (which makes retries idempotent).
 */
export function mergePortfolioStates(
  cloudState: PortfolioState,
  browserState: PortfolioState,
): PortfolioState {
  const cloud = parsePortfolioState(cloudState);
  const browser = parsePortfolioState(browserState);
  const watchlist: string[] = [];
  for (const code of [...cloud.watchlist, ...browser.watchlist]) {
    if (!watchlist.includes(code) && watchlist.length < MAX_WATCHLIST_ITEMS) {
      watchlist.push(code);
    }
  }

  const holdings: PortfolioHolding[] = [];
  const usedIds = new Set<string>();
  for (const holding of [...cloud.holdings, ...browser.holdings]) {
    if (holdings.length >= MAX_HOLDINGS) break;
    if (usedIds.has(holding.id)) {
      if (holdings.some((existing) => holdingMatches(existing, holding))) continue;
      const id = createUniqueHoldingId(holding.id, usedIds);
      holdings.push({ ...holding, id });
      usedIds.add(id);
      continue;
    }
    holdings.push(holding);
    usedIds.add(holding.id);
  }

  return { schemaVersion: PORTFOLIO_SCHEMA_VERSION, watchlist, holdings };
}

/** External-store helpers keep localStorage out of the server render path. */
export function getPortfolioSnapshot(): PortfolioState {
  if (typeof window === 'undefined') return serverSnapshot;
  if (!clientSnapshot) {
    clientSnapshot = loadPortfolioState(getBrowserStorage(), activePortfolioStorageKey);
  }
  return clientSnapshot;
}

export function getServerPortfolioSnapshot(): PortfolioState {
  return serverSnapshot;
}

export function subscribeToPortfolio(listener: PortfolioListener): () => void {
  listeners.add(listener);
  return () => listeners.delete(listener);
}

function notifyPortfolioListeners(): void {
  for (const listener of listeners) listener();
}

/**
 * Changes the active local cache. A user-scoped cache is selected only after
 * the cloud session identifies the account, so anonymous data is never
 * overwritten by a different user's cache.
 */
export function setPortfolioStorageScope(
  userId: string | null | undefined,
  storage?: StorageAccess,
): PortfolioState {
  activePortfolioUserId = normalizePortfolioUserId(userId);
  activePortfolioStorageKey = getPortfolioStorageKey(activePortfolioUserId);
  clientSnapshot = loadPortfolioState(
    storage ?? getBrowserStorage(),
    activePortfolioStorageKey,
  );
  notifyPortfolioListeners();
  return clientSnapshot;
}

export function getActivePortfolioUserId(): string | null {
  return activePortfolioUserId;
}

export function setPortfolioSnapshot(
  nextState: PortfolioState,
  options?: { storage?: StorageWriter; storageKey?: string },
): void {
  const parsed = parsePortfolioState(nextState);
  clientSnapshot = parsed;
  const storage = options?.storage ?? getBrowserStorage();
  const storageKey = options?.storageKey ?? activePortfolioStorageKey;
  if (storage) savePortfolioState(parsed, storage, storageKey);
  notifyPortfolioListeners();
}

export function getPortfolioMigrationStatus(
  userId: string,
  storage?: StorageReader,
): PortfolioMigrationStatus | null {
  const normalizedUserId = normalizePortfolioUserId(userId);
  if (!normalizedUserId) return null;
  const source = storage ?? getBrowserStorage();
  if (!source) return null;
  try {
    const value = source.getItem(getPortfolioMigrationStorageKey(normalizedUserId));
    return value === 'merged' || value === 'deferred' ? value : null;
  } catch {
    return null;
  }
}

export function markPortfolioMigrationStatus(
  userId: string,
  status: PortfolioMigrationStatus,
  storage?: StorageWriter,
): void {
  const normalizedUserId = normalizePortfolioUserId(userId);
  const target = storage ?? getBrowserStorage();
  if (!normalizedUserId || !target) return;
  try {
    target.setItem(getPortfolioMigrationStorageKey(normalizedUserId), status);
  } catch {
    // The marker is only a UX convenience; a storage failure must not block login.
  }
}

export class PortfolioCloudError extends Error {
  readonly status: number;

  constructor(message: string, status = 0) {
    super(message);
    this.name = 'PortfolioCloudError';
    this.status = status;
  }
}

function cloudMessage(payload: unknown, fallback: string): string {
  if (isRecord(payload)) {
    if (typeof payload.error === 'string' && payload.error.trim()) return payload.error;
    if (typeof payload.message === 'string' && payload.message.trim()) return payload.message;
    if (typeof payload.warning === 'string' && payload.warning.trim()) return payload.warning;
  }
  return fallback;
}

async function readJson(response: Response): Promise<unknown> {
  try {
    return await response.json() as unknown;
  } catch {
    return null;
  }
}

/** Parses the small, stable envelope shared by auth/cloud API implementations. */
export function parsePortfolioCloudResponse(value: unknown): PortfolioCloudResponse | null {
  if (!isRecord(value)) return null;
  // The initial adapter contract wraps the state in an auth envelope. The
  // current server route returns the PortfolioState directly after it has
  // authenticated the request; accept both shapes here so fetch details stay
  // centralized in this module.
  if (value.schemaVersion === PORTFOLIO_SCHEMA_VERSION &&
      Array.isArray(value.watchlist) &&
      Array.isArray(value.holdings)) {
    return {
      authenticated: true,
      userId: null,
      email: null,
      portfolio: parsePortfolioState(value),
    };
  }
  if (typeof value.authenticated !== 'boolean') return null;
  const userId = typeof value.userId === 'string' && value.userId.trim().length > 0
    ? value.userId.trim()
    : null;
  const email = typeof value.email === 'string' && value.email.trim().length > 0
    ? value.email.trim()
    : null;
  return {
    authenticated: value.authenticated,
    userId,
    email,
    portfolio: parsePortfolioState(value.portfolio),
  };
}

async function requestCloud(
  path: string,
  init: RequestInit,
): Promise<PortfolioCloudResponse | null> {
  if (typeof fetch !== 'function') {
    throw new PortfolioCloudError('云端同步在当前环境不可用');
  }
  let response: Response;
  try {
    response = await fetch(path, {
      ...init,
      credentials: 'include',
      cache: 'no-store',
    });
  } catch (error) {
    if (error instanceof Error && error.name === 'AbortError') throw error;
    throw new PortfolioCloudError('网络不可用，当前使用离线缓存');
  }
  const payload = await readJson(response);
  if (!response.ok) {
    throw new PortfolioCloudError(
      cloudMessage(payload, response.status === 404 ? '云端同步服务尚未配置' : '云端同步失败'),
      response.status,
    );
  }
  if (payload === null) return null;
  return parsePortfolioCloudResponse(payload);
}

export async function fetchPortfolioCloud(options?: {
  signal?: AbortSignal;
}): Promise<PortfolioCloudResponse> {
  let payload: PortfolioCloudResponse | null;
  try {
    payload = await requestCloud('/api/portfolio/cloud', {
      method: 'GET',
      signal: options?.signal,
    });
  } catch (error) {
    // A 401 is the expected response for an anonymous visitor. Treat it as a
    // local-only session rather than rendering a cloud-sync error.
    if (error instanceof PortfolioCloudError && error.status === 401) {
      return {
        authenticated: false,
        userId: null,
        email: null,
        portfolio: createEmptyPortfolioState(),
      };
    }
    throw error;
  }
  if (!payload) throw new PortfolioCloudError('云端同步返回了无法识别的数据');
  if (payload.authenticated && !payload.userId) {
    const identity = await fetchPortfolioIdentity(options);
    if (identity.userId) {
      return {
        ...payload,
        userId: identity.userId,
        email: payload.email ?? identity.email,
      };
    }
  }
  return payload;
}

export async function putPortfolioCloud(
  state: PortfolioState,
  options?: { previousState?: PortfolioState; signal?: AbortSignal },
): Promise<PortfolioCloudResponse | null> {
  return syncPortfolioCloud(options?.previousState ?? createEmptyPortfolioState(), state, options);
}

/**
 * Imports anonymous records through the idempotent server endpoint. Some
 * adapters return the merged envelope immediately; older adapters return an
 * acknowledgement only, so a follow-up GET is intentionally centralized here.
 */
export async function importPortfolioCloud(
  state: PortfolioState,
  options?: { signal?: AbortSignal },
): Promise<PortfolioCloudResponse> {
  const response = await requestCloud('/api/portfolio/cloud/merge', {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify(parsePortfolioState(state)),
    signal: options?.signal,
  });
  if (response?.authenticated && response.userId) return response;
  if (response?.authenticated) {
    const identity = await fetchPortfolioIdentity(options);
    return { ...response, ...identity, email: response.email ?? identity.email };
  }
  return fetchPortfolioCloud(options);
}

type PortfolioIdentity = {
  authenticated: boolean;
  userId: string | null;
  email: string | null;
};

/**
 * Obtains the account identity for the per-user browser cache. Deployments may
 * expose a lightweight session route; Supabase's browser client is the
 * fallback used by the current auth UI.
 */
export async function fetchPortfolioIdentity(options?: {
  signal?: AbortSignal;
}): Promise<PortfolioIdentity> {
  if (typeof fetch === 'function') {
    try {
      const response = await fetch('/api/auth/session', {
        method: 'GET',
        credentials: 'include',
        cache: 'no-store',
        signal: options?.signal,
      });
      if (response.ok) {
        const payload = await readJson(response);
        if (isRecord(payload) && typeof payload.authenticated === 'boolean') {
          return {
            authenticated: payload.authenticated,
            userId: typeof payload.userId === 'string' ? payload.userId.trim() || null : null,
            email: typeof payload.email === 'string' ? payload.email.trim() || null : null,
          };
        }
      }
    } catch (error) {
      if (error instanceof Error && error.name === 'AbortError') throw error;
      // The browser Supabase client below is intentionally the fallback.
    }
  }

  try {
    const browserModule = await import('../supabase/browser.ts');
    const client = browserModule.getSupabaseBrowserClient();
    if (!client) return { authenticated: false, userId: null, email: null };
    const result = await client.auth.getUser();
    const user = result.data.user;
    return {
      authenticated: Boolean(user),
      userId: user?.id ?? null,
      email: user?.email ?? null,
    };
  } catch {
    return { authenticated: false, userId: null, email: null };
  }
}

function cloudWriteState(state: PortfolioState): {
  watchlist: string[];
  holdings: Array<{
    sourceId: string;
    code: string;
    quantity: number;
    costPrice: number;
  }>;
} {
  const parsed = parsePortfolioState(state);
  return {
    watchlist: parsed.watchlist,
    holdings: parsed.holdings.map((holding) => ({
      sourceId: holding.id,
      code: holding.code,
      quantity: holding.quantity,
      costPrice: holding.costPrice,
    })),
  };
}

/**
 * The server adapter is incremental: delete rows absent from the next state,
 * then upsert the complete current arrays. Keeping this sequence here avoids
 * resurrecting deleted records after a refresh or on another device.
 */
export async function syncPortfolioCloud(
  previousState: PortfolioState,
  nextState: PortfolioState,
  options?: { signal?: AbortSignal },
): Promise<PortfolioCloudResponse | null> {
  const previous = parsePortfolioState(previousState);
  const next = parsePortfolioState(nextState);
  const removedWatchlist = previous.watchlist.filter((code) => !next.watchlist.includes(code));
  const nextHoldingIds = new Set(next.holdings.map((holding) => holding.id));
  const removedHoldingIds = previous.holdings
    .map((holding) => holding.id)
    .filter((id) => !nextHoldingIds.has(id));

  let result: PortfolioCloudResponse | null = null;
  if (removedWatchlist.length > 0 || removedHoldingIds.length > 0) {
    result = await requestCloud('/api/portfolio/cloud', {
      method: 'DELETE',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify({ watchlist: removedWatchlist, holdingIds: removedHoldingIds }),
      signal: options?.signal,
    });
  }
  if (next.watchlist.length > 0 || next.holdings.length > 0) {
    result = await requestCloud('/api/portfolio/cloud', {
      method: 'POST',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify(cloudWriteState(next)),
      signal: options?.signal,
    });
  }
  return result;
}

let cloudSaveChain: Promise<unknown> = Promise.resolve();

/**
 * Serializes cloud writes and skips stale intermediate edits. This prevents
 * rapid form changes from arriving out of order at the database proxy.
 */
export function savePortfolioCloud(
  state: PortfolioState,
  options?: {
    userId?: string | null;
    previousState?: PortfolioState;
    signal?: AbortSignal;
  },
): Promise<PortfolioCloudResponse | null> {
  const expectedUserId = normalizePortfolioUserId(options?.userId ?? activePortfolioUserId);
  if (!expectedUserId) return Promise.resolve(null);
  const run = cloudSaveChain.then(async () => {
    if (activePortfolioUserId && activePortfolioUserId !== expectedUserId) return null;
    return putPortfolioCloud(state, {
      previousState: options?.previousState,
      signal: options?.signal,
    });
  });
  cloudSaveChain = run.then(() => undefined, () => undefined);
  return run;
}
