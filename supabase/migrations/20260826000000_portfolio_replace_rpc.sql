-- Atomic full-state replace for the authenticated user's portfolio.
-- Run this migration in the Supabase SQL Editor or with `supabase db push`.
-- Does not modify 20260825000000_portfolio.sql.

create or replace function public.replace_portfolio_state(
  p_watchlist jsonb default '[]'::jsonb,
  p_holdings jsonb default '[]'::jsonb
)
returns void
language plpgsql
security invoker
set search_path = pg_catalog, pg_temp
as $$
declare
  v_user_id uuid;
begin
  -- Never trust a client-supplied user id: the only identity this function
  -- acts on is the caller's own authenticated session.
  v_user_id := auth.uid();
  if v_user_id is null then
    raise exception 'replace_portfolio_state requires an authenticated user';
  end if;

  if jsonb_typeof(p_watchlist) <> 'array' then
    raise exception 'p_watchlist must be a JSON array';
  end if;
  if jsonb_typeof(p_holdings) <> 'array' then
    raise exception 'p_holdings must be a JSON array';
  end if;

  -- A single function call runs inside the calling statement's transaction,
  -- so any parse, constraint, or RLS failure below rolls back every delete
  -- and insert made here rather than leaving a partial replace behind.
  delete from public.watchlist_items where user_id = v_user_id;
  delete from public.holdings where user_id = v_user_id;

  insert into public.watchlist_items (user_id, stock_code)
  select v_user_id, elem ->> 'stock_code'
  from jsonb_array_elements(p_watchlist) as elem;

  insert into public.holdings (user_id, source_id, stock_code, quantity, cost_price)
  select
    v_user_id,
    elem ->> 'source_id',
    elem ->> 'stock_code',
    (elem ->> 'quantity')::integer,
    (elem ->> 'cost_price')::numeric
  from jsonb_array_elements(p_holdings) as elem;
end;
$$;

revoke all on function public.replace_portfolio_state(jsonb, jsonb) from public;
revoke all on function public.replace_portfolio_state(jsonb, jsonb) from anon;
grant execute on function public.replace_portfolio_state(jsonb, jsonb) to authenticated;
