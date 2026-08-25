import { marketDataProvider } from '../../../../lib/data/composite-provider';
import { normalizeTicker } from '../../../../lib/data/provider-utils';
import type { MarketSnapshot } from '../../../../lib/data/market-data-provider';
import {
  validatePortfolioQuoteCodes,
} from '../../../../lib/portfolio/validation';
import type {
  PortfolioQuote,
  PortfolioQuotesResponse,
} from '../../../../lib/portfolio/types';

export const runtime = 'nodejs';
export const maxDuration = 20;

function toPublicQuote(snapshot: MarketSnapshot): PortfolioQuote | null {
  try {
    const code = normalizeTicker(snapshot.code);
    const values = [
      snapshot.lastPrice,
      snapshot.previousClose,
      snapshot.volumeRatio,
      snapshot.turnoverRatePct,
      snapshot.totalMarketCapYuan,
    ];
    if (!/^\d{6}$/.test(code) ||
        snapshot.lastPrice <= 0 ||
        snapshot.previousClose <= 0 ||
        values.some((value) => !Number.isFinite(value))) {
      return null;
    }
    const changePct = snapshot.previousClose > 0
      ? ((snapshot.lastPrice - snapshot.previousClose) / snapshot.previousClose) * 100
      : 0;
    if (!Number.isFinite(changePct)) return null;
    return {
      code,
      timestamp: snapshot.timestamp,
      lastPrice: snapshot.lastPrice,
      previousClose: snapshot.previousClose,
      volumeRatio: snapshot.volumeRatio,
      turnoverRatePct: snapshot.turnoverRatePct,
      totalMarketCapYuan: snapshot.totalMarketCapYuan,
      changePct,
    };
  } catch {
    return null;
  }
}

function degradedResponse(
  codes: string[],
  warning: string,
  status = 502,
): Response {
  const payload: PortfolioQuotesResponse = {
    generatedAt: new Date().toISOString(),
    provider: marketDataProvider.name,
    snapshots: [],
    missingCodes: codes,
    degraded: true,
    warning,
  };
  return Response.json(payload, {
    status,
    headers: { 'Cache-Control': 'no-store' },
  });
}

export async function GET(request: Request) {
  const validated = validatePortfolioQuoteCodes(new URL(request.url).searchParams.get('codes'));
  if (!validated.ok) {
    return Response.json({ error: validated.error }, { status: 400 });
  }

  const codes = validated.value;
  try {
    const rawSnapshots = await marketDataProvider.getSnapshots(codes);
    const requested = new Set(codes);
    const snapshotsByCode = new Map<string, PortfolioQuote>();
    for (const snapshot of rawSnapshots) {
      const quote = toPublicQuote(snapshot);
      if (quote && requested.has(quote.code) && !snapshotsByCode.has(quote.code)) {
        snapshotsByCode.set(quote.code, quote);
      }
    }

    const snapshots = codes.flatMap((code) => {
      const quote = snapshotsByCode.get(code);
      return quote ? [quote] : [];
    });
    const missingCodes = codes.filter((code) => !snapshotsByCode.has(code));
    const degraded = missingCodes.length > 0;
    const payload: PortfolioQuotesResponse = {
      generatedAt: new Date().toISOString(),
      provider: marketDataProvider.name,
      snapshots,
      missingCodes,
      degraded,
      ...(degraded
        ? { warning: `有 ${missingCodes.length} 只股票暂时没有有效实时报价，相关持仓暂不计算市值。` }
        : {}),
    };
    if (snapshots.length === 0) {
      return degradedResponse(
        codes,
        '实时行情暂时不可用，当前仅保留浏览器本地的自选股和持仓成本数据。',
      );
    }
    return Response.json(payload, {
      headers: { 'Cache-Control': 'no-store' },
    });
  } catch {
    // Do not forward upstream URLs, credentials, or provider exception text.
    console.warn('自选股实时行情上游请求失败');
    return degradedResponse(
      codes,
      '实时行情服务暂时不可用，当前仅保留浏览器本地的自选股和持仓成本数据。',
    );
  }
}
