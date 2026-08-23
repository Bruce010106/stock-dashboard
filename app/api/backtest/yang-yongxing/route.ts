import {
  runYangYongxingForwardBacktest,
  type YangYongxingSignalEvent,
} from '../../../../lib/backtest/yang-yongxing-forward';

export async function POST(request: Request) {
  let payload: {
    events?: YangYongxingSignalEvent[];
    holdingTradingDays?: number;
  };

  try {
    payload = await request.json();
  } catch {
    return Response.json({ error: '请求体必须是合法 JSON' }, { status: 400 });
  }

  if (!Array.isArray(payload.events)) {
    return Response.json({ error: 'events 必须是信号事件数组' }, { status: 400 });
  }

  try {
    return Response.json(
      runYangYongxingForwardBacktest(
        payload.events,
        payload.holdingTradingDays ?? 5,
      ),
    );
  } catch (error) {
    return Response.json(
      { error: error instanceof Error ? error.message : '回测参数错误' },
      { status: 400 },
    );
  }
}
