import {
  runYangYongxingForwardBacktest,
} from '../../../../lib/backtest/yang-yongxing-forward';
import {
  LiveBacktestDataError,
  runLiveYangYongxingBacktest,
} from '../../../../lib/backtest/live-yang-yongxing';
import {
  validateLiveBacktestQuery,
  validateYangYongxingBacktestPayload,
} from '../../../../lib/api-validation';
import { isTushareConfigured } from '../../../../lib/data/tushare-provider';

export const runtime = 'nodejs';
export const maxDuration = 60;

export async function GET(request: Request) {
  const validated = validateLiveBacktestQuery(new URL(request.url).searchParams, {
    tushareConfigured: isTushareConfigured(),
  });
  if (!validated.ok) {
    return Response.json({ error: validated.error }, { status: 400 });
  }

  try {
    const result = await runLiveYangYongxingBacktest(validated.value);
    return Response.json(result, {
      headers: { 'Cache-Control': 'private, max-age=0, must-revalidate' },
    });
  } catch (error) {
    if (error instanceof LiveBacktestDataError) {
      return Response.json(
        { error: error.message, code: error.code },
        { status: error.status },
      );
    }
    console.error('真实历史回测失败', error);
    return Response.json(
      { error: error instanceof Error ? error.message : '真实历史回测失败' },
      { status: 502 },
    );
  }
}

export async function POST(request: Request) {
  let rawPayload: unknown;

  try {
    rawPayload = await request.json();
  } catch {
    return Response.json({ error: '请求体必须是合法 JSON' }, { status: 400 });
  }

  const validated = validateYangYongxingBacktestPayload(rawPayload);
  if (!validated.ok) {
    return Response.json({ error: validated.error }, { status: 400 });
  }
  const { events, holdingTradingDays } = validated.value;

  try {
    return Response.json(
      runYangYongxingForwardBacktest(
        events,
        holdingTradingDays,
      ),
    );
  } catch (error) {
    return Response.json(
      { error: error instanceof Error ? error.message : '回测参数错误' },
      { status: 400 },
    );
  }
}
