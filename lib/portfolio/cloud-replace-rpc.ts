import { PortfolioCloudError } from './cloud-errors.ts';
import type { CloudWritePayload } from './cloud-validation.ts';

const REPLACE_PORTFOLIO_STATE_RPC = 'replace_portfolio_state';

type PortfolioReplaceRpcResult = {
  data: unknown;
  error: { code?: string; message?: string } | null;
};

/**
 * Minimal structural client contract for the replace RPC: only the `rpc`
 * method is needed, so this stays independent of the full Supabase client
 * type (and its next/headers-dependent factory) used elsewhere.
 */
export type PortfolioReplaceRpcClient = {
  rpc(fn: string, args?: Record<string, unknown>): PromiseLike<PortfolioReplaceRpcResult>;
};

/**
 * Narrow, directly testable wrapper around the atomic replace RPC (see
 * supabase/migrations/20260826000000_portfolio_replace_rpc.sql). The
 * function determines the target user from auth.uid() itself, so no user id
 * is sent in the RPC arguments.
 */
export async function callReplacePortfolioStateRpc(
  client: PortfolioReplaceRpcClient,
  payload: Required<CloudWritePayload>,
): Promise<void> {
  const result = await client.rpc(REPLACE_PORTFOLIO_STATE_RPC, {
    p_watchlist: payload.watchlist.map((stockCode) => ({ stock_code: stockCode })),
    p_holdings: payload.holdings.map((holding) => ({
      source_id: holding.sourceId,
      stock_code: holding.code,
      quantity: holding.quantity,
      cost_price: holding.costPrice,
    })),
  });
  if (result.error) {
    throw new PortfolioCloudError(502, 'STORAGE', '云端组合数据暂时不可用');
  }
}
