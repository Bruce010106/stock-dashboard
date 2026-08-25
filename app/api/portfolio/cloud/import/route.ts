import {
  mergePortfolioCloud,
  PortfolioCloudError,
} from '../../../../../lib/portfolio/cloud-service';

export const runtime = 'nodejs';
export const dynamic = 'force-dynamic';

function errorResponse(error: unknown): Response {
  if (error instanceof PortfolioCloudError) {
    return Response.json(
      { error: error.message, code: error.code },
      { status: error.status, headers: { 'Cache-Control': 'no-store' } },
    );
  }
  console.error('组合云端导入接口请求失败');
  return Response.json(
    { error: '云端组合数据暂时不可用', code: 'STORAGE' },
    { status: 502, headers: { 'Cache-Control': 'no-store' } },
  );
}

export async function POST(request: Request) {
  try {
    let body: unknown;
    try {
      body = await request.json() as unknown;
    } catch {
      throw new PortfolioCloudError(400, 'VALIDATION', '请求体必须是有效 JSON');
    }
    const result = await mergePortfolioCloud(body);
    return Response.json(
      {
        authenticated: true,
        userId: result.userId,
        email: result.email,
        portfolio: result.state,
        generatedAt: new Date().toISOString(),
      },
      { headers: { 'Cache-Control': 'no-store' } },
    );
  } catch (error) {
    return errorResponse(error);
  }
}
