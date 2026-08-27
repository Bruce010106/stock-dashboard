import { marketDataProvider } from '../../../lib/data/composite-provider.ts';
import {
  checkTushareHealth,
  isTushareConfigured,
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
  // checks do not repeatedly download the full list. checkTushareHealth is
  // itself short-TTL cached, so this and the live-backtest route share one
  // upstream probe instead of each firing their own.
  const [snapshotProbe, universeProbe, tushareHealth] = await Promise.all([
    marketDataProvider.getSnapshots(['000001']).then(
      (value) => ({ status: 'fulfilled' as const, value }),
      (reason: unknown) => ({ status: 'rejected' as const, reason }),
    ),
    marketDataProvider.getUniverse(checkedAt.slice(0, 10)).then(
      (value) => ({ status: 'fulfilled' as const, value }),
      (reason: unknown) => ({ status: 'rejected' as const, reason }),
    ),
    // Never rejects — checkTushareHealth catches probe failures internally
    // and reports them via .error, with a short TTL cache shared across
    // every /api/data-status and live-backtest call.
    checkTushareHealth(),
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
  const tushareHealthy = tushareHealth.healthy;

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
    tushareHealth.error
      ? `Tushare 探测失败，将使用新浪免费数据源：${tushareHealth.error}`
      : 'Tushare 未通过探测，将使用新浪免费数据源',
  );
  if (!configured) warnings.push(
    '未配置 Tushare：今日选股使用腾讯不复权日线降级；策略回测仍可用，自动切换到新浪财经免费数据源（5 分钟近似口径，单次区间最长 30 天）',
  );

  // healthy means the core live-scan dependencies are reachable. Tushare is
  // an optional historical enhancement — even fully absent, backtesting
  // still works via the free Sina fallback — so its absence never makes the
  // whole service unhealthy, only degraded/historyMode.
  const healthy = tencentHealthy && universeHealthy;
  const historyMode = tushareHealthy ? 'tushare' : 'tencent-fallback';
  const backtestMode = tushareHealthy ? 'tushare-exact' : 'sina-free-approximate';

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
        role: configured
          ? '历史日线 / 点时指标 / 历史分钟回测（1 分钟精确口径）'
          : '可选精确增强源；未配置时回测自动使用新浪财经免费近似源',
        ...(tushareHealth.error ? { error: tushareHealth.error } : {}),
      },
    ],
    historyMode,
    backtestMode,
    warnings,
  }, {
    status: healthy ? 200 : 503,
    headers: { 'Cache-Control': 'public, s-maxage=60, stale-while-revalidate=120' },
  });
}
