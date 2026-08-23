import { normalizeTicker } from '../../../../lib/data/provider-utils.ts';
import { screenLiveYangYongxing } from '../../../../lib/screening/live-yang-yongxing.ts';

export const runtime = 'nodejs';
export const maxDuration = 60;

export async function GET(request: Request) {
  const url = new URL(request.url);
  const rawCodes = url.searchParams.get('codes');
  let codes: string[] | undefined;
  if (rawCodes) {
    try {
      codes = [...new Set(rawCodes.split(',').filter(Boolean).map(normalizeTicker))];
    } catch (error) {
      return Response.json(
        { error: error instanceof Error ? error.message : '股票代码格式错误' },
        { status: 400 },
      );
    }
    if (codes.length > 200) {
      return Response.json({ error: '单次指定股票不能超过 200 只' }, { status: 400 });
    }
  }

  try {
    const result = await screenLiveYangYongxing(codes);
    return Response.json(result, {
      headers: codes?.length
        ? { 'Cache-Control': 'no-store' }
        : { 'Cache-Control': 'public, s-maxage=300, stale-while-revalidate=300' },
    });
  } catch (error) {
    console.error('真实选股失败', error);
    return Response.json(
      { error: error instanceof Error ? error.message : '真实行情选股失败' },
      { status: 502 },
    );
  }
}

