import { marketDataProvider } from '../../../lib/data/composite-provider.ts';
import {
  isTushareConfigured,
  probeTushareConnection,
} from '../../../lib/data/tushare-provider.ts';

export const runtime = 'nodejs';

export async function GET() {
  const checkedAt = new Date().toISOString();
  try {
    const configured = isTushareConfigured();
    const [snapshots, tushareHealthy] = await Promise.all([
      marketDataProvider.getSnapshots(['000001']),
      configured ? probeTushareConnection().catch(() => false) : Promise.resolve(false),
    ]);
    const snapshot = snapshots[0];
    return Response.json({
      checkedAt,
      healthy: Boolean(snapshot),
      latestQuoteAt: snapshot?.timestamp,
      providers: [
        { id: 'tencent', name: '腾讯实时行情', configured: true, healthy: Boolean(snapshot), role: '实时快照 / 当日分钟线' },
        { id: 'eastmoney', name: '全市场股票池', configured: true, healthy: true, role: '东方财富 / 新浪降级清单' },
        { id: 'tushare', name: 'Tushare Pro', configured, healthy: tushareHealthy, role: '历史日线 / 点时指标' },
      ],
      historyMode: tushareHealthy ? 'tushare' : 'tencent-fallback',
    }, { headers: { 'Cache-Control': 'public, s-maxage=60, stale-while-revalidate=120' } });
  } catch (error) {
    return Response.json({
      checkedAt,
      healthy: false,
      error: error instanceof Error ? error.message : '数据源检查失败',
      providers: [
        { id: 'tencent', name: '腾讯实时行情', configured: true, healthy: false, role: '实时快照 / 当日分钟线' },
        { id: 'eastmoney', name: '全市场股票池', configured: true, healthy: true, role: '东方财富 / 新浪降级清单' },
        { id: 'tushare', name: 'Tushare Pro', configured: isTushareConfigured(), healthy: false, role: '历史日线 / 点时指标' },
      ],
      historyMode: marketDataProvider.historyMode,
    }, { status: 503 });
  }
}
