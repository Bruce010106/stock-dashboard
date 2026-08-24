import { marketDataProvider } from '../../../lib/data/composite-provider.ts';
import {
  isTushareConfigured,
  probeTushareConnection,
} from '../../../lib/data/tushare-provider.ts';

export const runtime = 'nodejs';

type ProbeError = { message?: string };

function errorMessage(reason: unknown, fallback: string): string {
  return reason && typeof reason === 'object' && 'message' in reason
    ? String((reason as ProbeError).message || fallback)
    : fallback;
}

export async function GET() {
  const checkedAt = new Date().toISOString();
  const configured = isTushareConfigured();

  // getUniverse() exercises the same stock-pool path used by a live scan. Its
  // underlying provider uses a 12-hour revalidated request, so repeated status
  // checks do not repeatedly download the full list.
  const [snapshotProbe, universeProbe, tushareProbe] = await Promise.allSettled([
    marketDataProvider.getSnapshots(['000001']),
    marketDataProvider.getUniverse(checkedAt.slice(0, 10)),
    configured ? probeTushareConnection() : Promise.resolve(false),
  ]);

  const snapshots = snapshotProbe.status === 'fulfilled'
    ? snapshotProbe.value
    : [];
  const universe = universeProbe.status === 'fulfilled'
    ? universeProbe.value
    : [];
  const snapshot = snapshots[0];
  const tencentHealthy = snapshotProbe.status === 'fulfilled' && Boolean(snapshot);
  const universeHealthy = universeProbe.status === 'fulfilled' && universe.length > 0;
  const tushareHealthy = configured && tushareProbe.status === 'fulfilled' && tushareProbe.value;

  const warnings: string[] = [];
  if (!tencentHealthy) warnings.push(
    snapshotProbe.status === 'rejected'
      ? errorMessage(snapshotProbe.reason, '腾讯实时行情探测失败')
      : '腾讯实时行情未返回有效报价',
  );
  if (!universeHealthy) warnings.push(
    universeProbe.status === 'rejected'
      ? errorMessage(universeProbe.reason, '股票池探测失败')
      : '股票池未返回有效股票',
  );
  if (configured && !tushareHealthy) warnings.push(
    tushareProbe.status === 'rejected'
      ? errorMessage(tushareProbe.reason, 'Tushare 探测失败，将使用腾讯历史降级')
      : 'Tushare 未通过探测，将使用腾讯历史降级',
  );
  if (!configured) warnings.push('未配置 Tushare，历史日线使用腾讯降级源');

  // healthy means the core live-scan dependencies are reachable. Tushare is
  // an optional historical enhancement; its failure is represented by
  // degraded/historyMode instead of making the whole service unhealthy.
  const healthy = tencentHealthy && universeHealthy;
  const historyMode = tushareHealthy ? 'tushare' : 'tencent-fallback';

  return Response.json({
    checkedAt,
    healthy,
    degraded: !tushareHealthy,
    healthScope: '实时行情与股票池为核心依赖；Tushare 为可选历史增强源',
    latestQuoteAt: snapshot?.timestamp,
    providers: [
      {
        id: 'tencent',
        name: '腾讯实时行情',
        configured: true,
        healthy: tencentHealthy,
        role: '实时快照 / 当日分钟线',
        ...(snapshotProbe.status === 'rejected'
          ? { error: errorMessage(snapshotProbe.reason, '腾讯实时行情探测失败') }
          : {}),
      },
      {
        id: 'eastmoney',
        name: '全市场股票池',
        configured: true,
        healthy: universeHealthy,
        role: '东方财富 / 新浪降级清单',
        ...(universeProbe.status === 'rejected'
          ? { error: errorMessage(universeProbe.reason, '股票池探测失败') }
          : { count: universe.length }),
      },
      {
        id: 'tushare',
        name: 'Tushare Pro',
        configured,
        healthy: tushareHealthy,
        role: '历史日线 / 点时指标 / 历史分钟回测',
        ...(tushareProbe.status === 'rejected'
          ? { error: errorMessage(tushareProbe.reason, 'Tushare 探测失败') }
          : {}),
      },
    ],
    historyMode,
    warnings,
  }, {
    status: healthy ? 200 : 503,
    headers: { 'Cache-Control': 'public, s-maxage=60, stale-while-revalidate=120' },
  });
}
