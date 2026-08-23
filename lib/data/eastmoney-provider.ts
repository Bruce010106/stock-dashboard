import type { StockInstrument } from './market-data-provider.ts';
import { chunked, exchangeOf, isStName } from './provider-utils.ts';

const UNIVERSE_URL = 'https://push2.eastmoney.com/api/qt/clist/get';

type EastmoneyUniverseResponse = {
  data?: {
    total?: number;
    diff?: Array<{ f12?: string; f14?: string }>;
  };
};

type SinaUniverseRow = {
  code?: string;
  name?: string;
};

const SINA_LIST_URL = 'https://vip.stock.finance.sina.com.cn/quotes_service/api/json_v2.php/Market_Center.getHQNodeData';
const SINA_COUNT_URL = 'https://vip.stock.finance.sina.com.cn/quotes_service/api/json_v2.php/Market_Center.getHQNodeStockCount?node=hs_a';

function toInstruments(rows: Array<{ code?: string; name?: string }>): StockInstrument[] {
  return rows.flatMap((row) => {
    const code = row.code?.trim();
    const name = row.name?.trim();
    if (!code || !name || !/^\d{6}$/.test(code)) return [];
    return [{
      code,
      name,
      exchange: exchangeOf(code),
      isSt: isStName(name),
    } satisfies StockInstrument];
  });
}

async function getSinaUniverse(): Promise<StockInstrument[]> {
  const headers = {
    'User-Agent': 'Mozilla/5.0 (compatible; ZhihengQuant/1.0)',
    Referer: 'https://vip.stock.finance.sina.com.cn/',
  };
  const countResponse = await fetch(SINA_COUNT_URL, {
    headers,
    next: { revalidate: 43_200 },
    signal: AbortSignal.timeout(12_000),
  });
  if (!countResponse.ok) throw new Error(`新浪股票池计数失败：HTTP ${countResponse.status}`);
  const total = Number((await countResponse.text()).replaceAll('"', '').trim());
  if (!Number.isFinite(total) || total < 4_000) throw new Error('新浪股票池计数异常');

  const pages = Array.from({ length: Math.ceil(total / 100) }, (_, index) => index + 1);
  const rows: SinaUniverseRow[] = [];
  for (const batch of chunked(pages, 8)) {
    const settled = await Promise.allSettled(batch.map(async (page) => {
      const params = new URLSearchParams({
        page: String(page),
        num: '100',
        sort: 'symbol',
        asc: '1',
        node: 'hs_a',
        symbol: '',
      });
      const response = await fetch(`${SINA_LIST_URL}?${params}`, {
        headers,
        next: { revalidate: 43_200 },
        signal: AbortSignal.timeout(12_000),
      });
      if (!response.ok) throw new Error(`新浪股票池第 ${page} 页失败`);
      return await response.json() as SinaUniverseRow[];
    }));
    for (const result of settled) {
      if (result.status === 'fulfilled') rows.push(...result.value);
    }
  }
  const instruments = toInstruments(rows);
  if (instruments.length < total * 0.9) {
    throw new Error(`新浪股票池不完整：${instruments.length}/${total}`);
  }
  return instruments;
}

export async function getEastmoneyUniverse(): Promise<StockInstrument[]> {
  const params = new URLSearchParams({
    pn: '1',
    pz: '6000',
    po: '1',
    np: '1',
    fltt: '2',
    invt: '2',
    fid: 'f12',
    fs: 'm:0+t:6,m:0+t:80,m:1+t:2,m:1+t:23,m:0+t:81+s:2048',
    fields: 'f12,f14',
  });
  const response = await fetch(`${UNIVERSE_URL}?${params}`, {
    headers: {
      'User-Agent': 'Mozilla/5.0 (compatible; ZhihengQuant/1.0)',
      Referer: 'https://quote.eastmoney.com/',
    },
    next: { revalidate: 43_200 },
    signal: AbortSignal.timeout(15_000),
  });
  if (!response.ok) {
    throw new Error(`东财股票池请求失败：HTTP ${response.status}`);
  }

  const payload = (await response.json()) as EastmoneyUniverseResponse;
  const rows = payload.data?.diff ?? [];
  if (rows.length === 0) {
    throw new Error('东财股票池返回空数据');
  }
  if (rows.length < (payload.data?.total ?? rows.length)) {
    return getSinaUniverse();
  }
  return toInstruments(rows.map((row) => ({ code: row.f12, name: row.f14 })));
}
