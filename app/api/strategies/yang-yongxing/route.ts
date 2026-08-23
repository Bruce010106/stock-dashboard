import {
  evaluateYangYongxing,
  screenYangYongxing,
  YANG_YONGXING_RULES,
  type YangYongxingCandidate,
} from '../../../../lib/strategies/yang-yongxing';

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
  let payload: { candidates?: YangYongxingCandidate[]; explain?: boolean };

  try {
    payload = await request.json();
  } catch {
    return Response.json({ error: '请求体必须是合法 JSON' }, { status: 400 });
  }

  if (!Array.isArray(payload.candidates)) {
    return Response.json(
      { error: 'candidates 必须是候选股票数组' },
      { status: 400 },
    );
  }

  const results = payload.explain
    ? payload.candidates.map(evaluateYangYongxing)
    : screenYangYongxing(payload.candidates);

  return Response.json({
    strategy: 'yang-yongxing-tail-1430',
    scanned: payload.candidates.length,
    matched: results.filter((result) => result.passed).length,
    results,
  });
}
