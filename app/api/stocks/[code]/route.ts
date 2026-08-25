import { marketDataProvider } from '../../../../lib/data/composite-provider.ts';
import { normalizeTicker } from '../../../../lib/data/provider-utils.ts';
import { evaluateYangYongxing } from '../../../../lib/strategies/yang-yongxing.ts';

export const runtime = 'nodejs';
export const maxDuration = 60;

function calendarDaysBefore(date: string, days: number): string {
  const value = new Date(`${date}T00:00:00+08:00`);
  value.setUTCDate(value.getUTCDate() - days);
  return value.toISOString().slice(0, 10);
}

export async function GET(
  _request: Request,
  { params }: { params: Promise<{ code: string }> },
) {
  let code: string;
  try {
    code = normalizeTicker((await params).code);
  } catch (error) {
    return Response.json(
      { error: error instanceof Error ? error.message : '股票代码格式错误' },
      { status: 400 },
    );
  }

  const today = new Date().toISOString().slice(0, 10);
  const startDate = calendarDaysBefore(today, 540);
  const warnings: string[] = [];

  const [historyResult, snapshotResult, universeResult] = await Promise.allSettled([
    marketDataProvider.getDailyBarsWithSource([code], startDate, today),
    marketDataProvider.getSnapshots([code]),
    marketDataProvider.getUniverse(today),
  ]);

  if (historyResult.status === 'rejected') {
    console.error(`股票详情日线请求失败：${code}`, historyResult.reason);
    return Response.json(
      { error: historyResult.reason instanceof Error ? historyResult.reason.message : '历史行情读取失败' },
      { status: 502 },
    );
  }

  const history = historyResult.value;
  if (history.warning) warnings.push(history.warning);
  if (snapshotResult.status === 'rejected') {
    warnings.push('实时行情暂不可用，页面仅展示最近可用日线');
  }
  const bars = history.bars
    .filter((bar) => bar.code === code)
    .sort((a, b) => a.date.localeCompare(b.date))
    .slice(-250);
  if (bars.length === 0) {
    return Response.json({ error: '该股票暂无可用日线数据' }, { status: 404 });
  }

  const snapshot = snapshotResult.status === 'fulfilled'
    ? snapshotResult.value.find((item) => item.code === code)
    : undefined;
  if (universeResult.status === 'rejected' && !snapshot?.name) {
    warnings.push('股票名称暂不可用');
  }
  const instrument = universeResult.status === 'fulfilled'
    ? universeResult.value.find((item) => item.code === code)
    : undefined;
  const tradeDate = snapshot?.timestamp.slice(0, 10) ?? bars.at(-1)!.date;

  let evaluation: ReturnType<typeof evaluateYangYongxing> | undefined;
  if (snapshot) {
    let minuteBars: Awaited<ReturnType<typeof marketDataProvider.getMinuteBars>> = [];
    try {
      minuteBars = await marketDataProvider.getMinuteBars([code], tradeDate);
    } catch (error) {
      warnings.push(`分钟线暂不可用：${error instanceof Error ? error.message : '上游异常'}`);
    }
    const recentDailyBars = bars.filter((bar) => bar.date < tradeDate).slice(-30);
    evaluation = evaluateYangYongxing({
      code,
      name: instrument?.name ?? snapshot.name ?? code,
      changePct: (snapshot.lastPrice / snapshot.previousClose - 1) * 100,
      totalMarketCapYuan: snapshot.totalMarketCapYuan,
      volumeRatio: snapshot.volumeRatio,
      turnoverRatePct: snapshot.turnoverRatePct,
      recentDailyBars,
      minuteBars,
    });
  }

  return Response.json({
    code,
    name: instrument?.name ?? snapshot?.name ?? code,
    exchange: instrument?.exchange,
    generatedAt: new Date().toISOString(),
    source: marketDataProvider.name,
    historyMode: history.historyMode,
    isFallback: history.isFallback,
    warnings,
    snapshot,
    bars,
    evaluation,
  }, {
    headers: { 'Cache-Control': 'public, s-maxage=300, stale-while-revalidate=300' },
  });
}
