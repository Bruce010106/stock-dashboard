-- Portfolio cloud storage for Supabase.
-- Run this migration in the Supabase SQL Editor or with `supabase db push`.

create extension if not exists pgcrypto;

create table if not exists public.watchlist_items (
  id uuid primary key default gen_random_uuid(),
  user_id uuid not null references auth.users(id) on delete cascade,
  stock_code text not null,
  created_at timestamptz not null default timezone('utc', now()),
  constraint watchlist_items_stock_code_format
    check (stock_code ~ '^[0-9]{6}$'),
  constraint watchlist_items_user_stock_unique
    unique (user_id, stock_code)
);

create table if not exists public.holdings (
  id uuid primary key default gen_random_uuid(),
  user_id uuid not null references auth.users(id) on delete cascade,
  -- Client-generated source_id is the idempotency key used during first-login
  -- merge. It intentionally does not include stock_code: one stock can have
  -- multiple cost lots as long as each lot has a different source_id.
  source_id text not null,
  stock_code text not null,
  quantity integer not null,
  cost_price numeric(20, 6) not null,
  created_at timestamptz not null default timezone('utc', now()),
  updated_at timestamptz not null default timezone('utc', now()),
  constraint holdings_source_id_format
    check (length(btrim(source_id)) between 1 and 120),
  constraint holdings_stock_code_format
    check (stock_code ~ '^[0-9]{6}$'),
  constraint holdings_quantity_range
    check (quantity between 1 and 100000000),
  constraint holdings_cost_price_range
    check (cost_price > 0 and cost_price <= 1000000),
  constraint holdings_user_source_unique
    unique (user_id, source_id)
);

create index if not exists watchlist_items_user_created_idx
  on public.watchlist_items (user_id, created_at);
create index if not exists holdings_user_stock_idx
  on public.holdings (user_id, stock_code);
create index if not exists holdings_user_updated_idx
  on public.holdings (user_id, updated_at);

create or replace function public.set_holdings_updated_at()
returns trigger
language plpgsql
set search_path = public
as $$
begin
  new.updated_at = timezone('utc', now());
  return new;
end;
$$;

drop trigger if exists holdings_set_updated_at on public.holdings;
create trigger holdings_set_updated_at
before update on public.holdings
for each row execute function public.set_holdings_updated_at();

alter table public.watchlist_items enable row level security;
alter table public.holdings enable row level security;

drop policy if exists watchlist_items_select_own on public.watchlist_items;
create policy watchlist_items_select_own
on public.watchlist_items for select
to authenticated
using (auth.uid() = user_id);

drop policy if exists watchlist_items_insert_own on public.watchlist_items;
create policy watchlist_items_insert_own
on public.watchlist_items for insert
to authenticated
with check (auth.uid() = user_id);

drop policy if exists watchlist_items_update_own on public.watchlist_items;
create policy watchlist_items_update_own
on public.watchlist_items for update
to authenticated
using (auth.uid() = user_id)
with check (auth.uid() = user_id);

drop policy if exists watchlist_items_delete_own on public.watchlist_items;
create policy watchlist_items_delete_own
on public.watchlist_items for delete
to authenticated
using (auth.uid() = user_id);

drop policy if exists holdings_select_own on public.holdings;
create policy holdings_select_own
on public.holdings for select
to authenticated
using (auth.uid() = user_id);

drop policy if exists holdings_insert_own on public.holdings;
create policy holdings_insert_own
on public.holdings for insert
to authenticated
with check (auth.uid() = user_id);

drop policy if exists holdings_update_own on public.holdings;
create policy holdings_update_own
on public.holdings for update
to authenticated
using (auth.uid() = user_id)
with check (auth.uid() = user_id);

drop policy if exists holdings_delete_own on public.holdings;
create policy holdings_delete_own
on public.holdings for delete
to authenticated
using (auth.uid() = user_id);

grant select, insert, update, delete on table public.watchlist_items to authenticated;
grant select, insert, update, delete on table public.holdings to authenticated;
revoke all on table public.watchlist_items from anon;
revoke all on table public.holdings from anon;
