import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import test from 'node:test';

import { PortfolioCloudError } from '../lib/portfolio/cloud-errors.ts';
import { callReplacePortfolioStateRpc } from '../lib/portfolio/cloud-replace-rpc.ts';
import type { PortfolioReplaceRpcClient } from '../lib/portfolio/cloud-replace-rpc.ts';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(__dirname, '..');

const samplePayload = {
  watchlist: ['600519'],
  holdings: [{ sourceId: 'lot-a', code: '000001', quantity: 100, costPrice: 10.5 }],
};

function makeFakeClient(rpcResult: { data: unknown; error: { message?: string } | null }) {
  const calls: { fn: string; args?: Record<string, unknown> }[] = [];
  const client: PortfolioReplaceRpcClient = {
    async rpc(fn, args) {
      calls.push({ fn, args });
      return rpcResult;
    },
  };
  return { client, calls };
}

test('replace RPC helper calls the atomic function exactly once without a userId argument', async () => {
  const { client, calls } = makeFakeClient({ data: null, error: null });
  await callReplacePortfolioStateRpc(client, samplePayload);

  assert.equal(calls.length, 1);
  assert.equal(calls[0].fn, 'replace_portfolio_state');
  const args = calls[0].args ?? {};
  assert.deepEqual(Object.keys(args).sort(), ['p_holdings', 'p_watchlist']);
  assert.deepEqual(args.p_watchlist, [{ stock_code: '600519' }]);
  assert.deepEqual(args.p_holdings, [
    { source_id: 'lot-a', stock_code: '000001', quantity: 100, cost_price: 10.5 },
  ]);
  const serialized = JSON.stringify(args);
  assert.ok(!serialized.toLowerCase().includes('userid'));
  assert.ok(!serialized.includes('user_id'));
});

test('replace RPC helper raises a STORAGE error when the RPC call fails', async () => {
  const { client } = makeFakeClient({ data: null, error: { message: 'constraint violation' } });

  await assert.rejects(
    () => callReplacePortfolioStateRpc(client, samplePayload),
    (error: unknown) => {
      assert.ok(error instanceof PortfolioCloudError);
      assert.equal(error.code, 'STORAGE');
      return true;
    },
  );
});

test('replacePortfolioCloud no longer performs a delete-delete-write sequence', () => {
  const source = readFileSync(path.join(repoRoot, 'lib/portfolio/cloud-service.ts'), 'utf8');
  const start = source.indexOf('export async function replacePortfolioCloud');
  assert.ok(start >= 0, 'replacePortfolioCloud export not found');
  const end = source.indexOf('\n}', start);
  assert.ok(end > start, 'could not locate end of replacePortfolioCloud');
  const body = source.slice(start, end);

  assert.ok(body.includes('callReplacePortfolioStateRpc'));
  assert.ok(!body.includes('.delete()'));
  assert.ok(!body.includes("from('watchlist_items')"));
  assert.ok(!body.includes("from('holdings')"));
});

test('replace RPC migration is atomic, identity-safe, and permission-locked', () => {
  const migration = readFileSync(
    path.join(repoRoot, 'supabase/migrations/20260826000000_portfolio_replace_rpc.sql'),
    'utf8',
  );

  assert.ok(migration.includes('create or replace function public.replace_portfolio_state'));
  assert.ok(migration.includes('auth.uid()'));
  assert.ok(/security\s+invoker/i.test(migration));
  assert.ok(/set search_path\s*=\s*pg_catalog\s*,\s*pg_temp/i.test(migration));
  assert.ok(!/set search_path\s*=\s*public/i.test(migration), 'search_path must not include public');
  assert.ok(migration.includes('public.watchlist_items'));
  assert.ok(migration.includes('public.holdings'));
  assert.ok(migration.includes('revoke all on function public.replace_portfolio_state(jsonb, jsonb) from public'));
  assert.ok(migration.includes('revoke all on function public.replace_portfolio_state(jsonb, jsonb) from anon'));
  assert.ok(migration.includes('grant execute on function public.replace_portfolio_state(jsonb, jsonb) to authenticated'));

  const original = readFileSync(
    path.join(repoRoot, 'supabase/migrations/20260825000000_portfolio.sql'),
    'utf8',
  );
  assert.ok(!original.includes('replace_portfolio_state'), 'must not modify the already-applied migration');
});
