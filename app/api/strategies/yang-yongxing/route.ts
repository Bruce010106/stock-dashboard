import {
  evaluateYangYongxing,
  screenYangYongxing,
  YANG_YONGXING_RULES,
} from '../../../../lib/strategies/yang-yongxing';
import { validateYangYongxingStrategyPayload } from '../../../../lib/api-validation';

export async function GET() {
  return Response.json({
    id: 'yang-yongxing-tail-1430',
    name: '杨永兴尾盘战法',
    version: 1,
    rules: YANG_YONGXING_RULES,
    intradayDefinition:
      '14:30 后首次突破此前日内最高价；随后发生回踩，但后续分钟最低价不低于突破位，最后一分钟收盘价仍站在突破位之上。',
  });
}

export async function POST(request: Request) {
  let rawPayload: unknown;

  try {
    rawPayload = await request.json();
  } catch {
    return Response.json({ error: '请求体必须是合法 JSON' }, { status: 400 });
  }

  const validated = validateYangYongxingStrategyPayload(rawPayload);
  if (!validated.ok) {
    return Response.json({ error: validated.error }, { status: 400 });
  }
  const { candidates, explain } = validated.value;

  const results = explain
    ? candidates.map(evaluateYangYongxing)
    : screenYangYongxing(candidates);

  return Response.json({
    strategy: 'yang-yongxing-tail-1430',
    scanned: candidates.length,
    matched: results.filter((result) => result.passed).length,
    results,
  });
}
