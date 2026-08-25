import { getSupabaseServerClient } from '../supabase/server';
import {
  MAX_HOLDING_COST_PRICE,
  MAX_HOLDING_QUANTITY,
  PORTFOLIO_SCHEMA_VERSION,
} from './types.ts';
import type {
  PortfolioHolding,
  PortfolioState,
} from './types.ts';
import {
  parsePortfolioCloudDeletePayload,
  parsePortfolioCloudMergePayload,
  parsePortfolioCloudWritePayload,
} from './cloud-validation.ts';
import type {
  CloudDeletePayload,
  CloudHoldingInput,
  CloudWritePayload,
} from './cloud-validation.ts';
import { normalizePortfolioCode } from './validation.ts';

/**
 * Contract implemented by lib/supabase/server.ts. The concrete Supabase
 * client is intentionally structural here so this module does not expose or
 * depend on a service-role client. The factory must use the user's cookies.
 */
type PortfolioSupabaseQuery = PromiseLike<PortfolioSupabaseQueryResult> & {
  select(columns?: string): PortfolioSupabaseQuery;
  upsert(values: unknown, options?: { onConflict?: string }): PortfolioSupabaseQuery;
  delete(): PortfolioSupabaseQuery;
  eq(column: string, value: unknown): PortfolioSupabaseQuery;
  in(column: string, values: string[]): PortfolioSupabaseQuery;
  order(column: string, options?: { ascending?: boolean }): PortfolioSupabaseQuery;
};

type PortfolioSupabaseQueryResult = {
  data: unknown;
  error: { code?: string; message?: string } | null;
};

type PortfolioSupabaseClient = {
  auth: {
    getUser(): Promise<{
      data: { user: { id: string; email?: string | null } | null };
      error: { message?: string } | null;
    }>;
  };
  from(table: string): PortfolioSupabaseQuery;
};

type SupabaseServerFactory = () => Promise<unknown> | unknown;

export class PortfolioCloudError extends Error {
  constructor(
    public readonly status: 400 | 401 | 502 | 503,
    public readonly code: 'VALIDATION' | 'AUTH_REQUIRED' | 'NOT_CONFIGURED' | 'STORAGE',
    message: string,
  ) {
    super(message);
    this.name = 'PortfolioCloudError';
  }
}

export type PortfolioCloudOperation = {
  state: PortfolioState;
  userId: string;
  email: string | null;
};

function asClient(value: unknown): PortfolioSupabaseClient | null {
  if (!value || typeof value !== 'object') return null;
  const candidate = value as Partial<PortfolioSupabaseClient>;
  return typeof candidate.from === 'function' && candidate.auth
    && typeof candidate.auth.getUser === 'function'
    ? value as PortfolioSupabaseClient
    : null;
}

function throwValidation(message: string): never {
  throw new PortfolioCloudError(400, 'VALIDATION', message);
}

function throwStorage(): never {
  throw new PortfolioCloudError(502, 'STORAGE', '云端组合数据暂时不可用');
}

function isConfigured(): boolean {
  const url = process.env.NEXT_PUBLIC_SUPABASE_URL ?? process.env.SUPABASE_URL;
  const key = process.env.NEXT_PUBLIC_SUPABASE_ANON_KEY
    ?? process.env.NEXT_PUBLIC_SUPABASE_PUBLISHABLE_KEY;
  return Boolean(url?.trim() && key?.trim());
}

async function requireClientAndUser(): Promise<{
  client: PortfolioSupabaseClient;
  userId: string;
  email: string | null;
}> {
  if (!isConfigured()) {
    throw new PortfolioCloudError(503, 'NOT_CONFIGURED', '云端同步尚未配置');
  }

  try {
    const factory = getSupabaseServerClient as unknown as SupabaseServerFactory;
    const client = asClient(await factory());
    if (!client) {
      throw new PortfolioCloudError(503, 'NOT_CONFIGURED', '云端同步尚未配置');
    }
    const auth = await client.auth.getUser();
    const userId = auth.data?.user?.id;
    if (auth.error || !userId) {
      throw new PortfolioCloudError(401, 'AUTH_REQUIRED', '请先登录后同步自选股和持仓');
    }
    const email = typeof auth.data.user?.email === 'string' && auth.data.user.email.trim()
      ? auth.data.user.email.trim()
      : null;
    return { client, userId, email };
  } catch (error) {
    if (error instanceof PortfolioCloudError) throw error;
    throw new PortfolioCloudError(503, 'NOT_CONFIGURED', '云端同步尚未配置');
  }
}

function assertQuerySucceeded(result: PortfolioSupabaseQueryResult): unknown {
  if (result.error) throwStorage();
  return result.data;
}

function toFiniteNumber(value: unknown): number | null {
  const parsed = typeof value === 'number' ? value : Number(value);
  return Number.isFinite(parsed) ? parsed : null;
}

function toHolding(row: unknown): PortfolioHolding | null {
  if (!row || typeof row !== 'object') return null;
  const record = row as Record<string, unknown>;
  const sourceId = typeof record.source_id === 'string'
    ? record.source_id
    : typeof record.id === 'string' ? record.id : null;
  const code = normalizePortfolioCode(record.stock_code);
  const quantity = toFiniteNumber(record.quantity);
  const costPrice = toFiniteNumber(record.cost_price);
  if (!sourceId || !code.ok || quantity === null || costPrice === null) return null;
  if (!Number.isSafeInteger(quantity) || quantity < 1 || quantity > MAX_HOLDING_QUANTITY) return null;
  if (costPrice <= 0 || costPrice > MAX_HOLDING_COST_PRICE) return null;
  return {
    id: sourceId,
    code: code.value,
    quantity,
    costPrice,
  };
}

async function readWithClient(
  client: PortfolioSupabaseClient,
  userId: string,
): Promise<PortfolioState> {
  const watchlistResult = await client
    .from('watchlist_items')
    .select('stock_code, created_at')
    .eq('user_id', userId)
    .order('created_at', { ascending: true });
  const holdingsResult = await client
    .from('holdings')
    .select('id, source_id, stock_code, quantity, cost_price, created_at, updated_at')
    .eq('user_id', userId)
    .order('created_at', { ascending: true });

  const watchlistRows = assertQuerySucceeded(watchlistResult);
  const holdingRows = assertQuerySucceeded(holdingsResult);
  if (!Array.isArray(watchlistRows) || !Array.isArray(holdingRows)) throwStorage();

  const watchlist: string[] = [];
  for (const row of watchlistRows) {
    if (!row || typeof row !== 'object') continue;
    const code = normalizePortfolioCode((row as Record<string, unknown>).stock_code);
    if (code.ok && !watchlist.includes(code.value)) watchlist.push(code.value);
  }
  const holdings: PortfolioHolding[] = [];
  const sourceIds = new Set<string>();
  for (const row of holdingRows) {
    const holding = toHolding(row);
    if (!holding || sourceIds.has(holding.id)) continue;
    sourceIds.add(holding.id);
    holdings.push(holding);
  }
  return {
    schemaVersion: PORTFOLIO_SCHEMA_VERSION,
    watchlist,
    holdings,
  };
}

async function writeRows(
  client: PortfolioSupabaseClient,
  userId: string,
  input: CloudWritePayload,
): Promise<void> {
  if (input.watchlist && input.watchlist.length > 0) {
    const result = await client.from('watchlist_items').upsert(
      input.watchlist.map((stockCode) => ({ user_id: userId, stock_code: stockCode })),
      { onConflict: 'user_id,stock_code' },
    );
    assertQuerySucceeded(result);
  }
  if (input.holdings && input.holdings.length > 0) {
    const result = await client.from('holdings').upsert(
      input.holdings.map((holding) => ({
        user_id: userId,
        source_id: holding.sourceId,
        stock_code: holding.code,
        quantity: holding.quantity,
        cost_price: holding.costPrice,
      })),
      { onConflict: 'user_id,source_id' },
    );
    assertQuerySucceeded(result);
  }
}

export async function getPortfolioCloud(): Promise<PortfolioState> {
  const { client, userId } = await requireClientAndUser();
  return readWithClient(client, userId);
}

export async function getPortfolioCloudOperation(): Promise<PortfolioCloudOperation> {
  const { client, userId, email } = await requireClientAndUser();
  return { state: await readWithClient(client, userId), userId, email };
}

export async function writePortfolioCloud(input: unknown): Promise<PortfolioCloudOperation> {
  const parsed = parsePortfolioCloudWritePayload(input);
  if (!parsed.ok) throwValidation(parsed.error);
  const { client, userId, email } = await requireClientAndUser();
  await writeRows(client, userId, parsed.value);
  return { state: await readWithClient(client, userId), userId, email };
}

export async function deletePortfolioCloud(input: unknown): Promise<PortfolioCloudOperation> {
  const parsed = parsePortfolioCloudDeletePayload(input);
  if (!parsed.ok) throwValidation(parsed.error);
  const { client, userId, email } = await requireClientAndUser();
  const payload: CloudDeletePayload = parsed.value;
  if (payload.watchlist.length > 0) {
    const result = await client
      .from('watchlist_items')
      .delete()
      .eq('user_id', userId)
      .in('stock_code', payload.watchlist);
    assertQuerySucceeded(result);
  }
  if (payload.holdingIds.length > 0) {
    const result = await client
      .from('holdings')
      .delete()
      .eq('user_id', userId)
      .in('source_id', payload.holdingIds);
    assertQuerySucceeded(result);
  }
  return { state: await readWithClient(client, userId), userId, email };
}

/**
 * First-login merge is additive and idempotent: watchlist rows are naturally
 * deduplicated by their unique key, while holdings use client source_id as
 * their per-user idempotency key and can still contain the same stock code in
 * multiple rows.
 */
export async function mergePortfolioCloud(input: unknown): Promise<PortfolioCloudOperation> {
  const parsed = parsePortfolioCloudMergePayload(input);
  if (!parsed.ok) throwValidation(parsed.error);
  const { client, userId, email } = await requireClientAndUser();
  await writeRows(client, userId, parsed.value);
  return { state: await readWithClient(client, userId), userId, email };
}

/**
 * PUT is the authenticated browser cache's authoritative save. It removes
 * records absent from the submitted state before upserting the desired rows;
 * this prevents a deleted local record from being resurrected on refresh.
 */
export async function replacePortfolioCloud(input: unknown): Promise<PortfolioCloudOperation> {
  const parsed = parsePortfolioCloudMergePayload(input);
  if (!parsed.ok) throwValidation(parsed.error);
  const { client, userId, email } = await requireClientAndUser();

  // Full replacement is intentionally scoped by user_id. RLS independently
  // enforces the same boundary even if a caller tampers with the request.
  const deleteWatchlist = await client.from('watchlist_items').delete().eq('user_id', userId);
  assertQuerySucceeded(deleteWatchlist);
  const deleteHoldings = await client.from('holdings').delete().eq('user_id', userId);
  assertQuerySucceeded(deleteHoldings);
  await writeRows(client, userId, parsed.value);
  return { state: await readWithClient(client, userId), userId, email };
}

// Keep this import contract visible to the authentication agent without
// exposing a service-role key in the portfolio module.
export type { CloudHoldingInput };
