import {
  deletePortfolioCloud,
  getPortfolioCloudOperation,
  PortfolioCloudError,
  replacePortfolioCloud,
  writePortfolioCloud,
} from '../../../../lib/portfolio/cloud-service';
import type { PortfolioState } from '../../../../lib/portfolio/types';
import { PORTFOLIO_SCHEMA_VERSION } from '../../../../lib/portfolio/types';

export const runtime = 'nodejs';
export const dynamic = 'force-dynamic';

function errorResponse(error: unknown): Response {
  if (error instanceof PortfolioCloudError) {
    const body = error.status === 401
      ? {
        authenticated: false,
        userId: null,
        email: null,
        portfolio: {
          schemaVersion: PORTFOLIO_SCHEMA_VERSION,
          watchlist: [],
          holdings: [],
        },
        error: error.message,
        code: error.code,
      }
      : { error: error.message, code: error.code };
    return Response.json(
      body,
      { status: error.status, headers: { 'Cache-Control': 'no-store' } },
    );
  }
  console.error('组合云端接口请求失败');
  return Response.json(
    { error: '云端组合数据暂时不可用', code: 'STORAGE' },
    { status: 502, headers: { 'Cache-Control': 'no-store' } },
  );
}

function stateResponse(result: {
  state: PortfolioState;
  userId: string;
  email: string | null;
}): Response {
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
}

async function readBody(request: Request): Promise<unknown> {
  try {
    return await request.json() as unknown;
  } catch {
    throw new PortfolioCloudError(400, 'VALIDATION', '请求体必须是有效 JSON');
  }
}

export async function GET() {
  try {
    return stateResponse(await getPortfolioCloudOperation());
  } catch (error) {
    return errorResponse(error);
  }
}

export async function POST(request: Request) {
  try {
    return stateResponse(await writePortfolioCloud(await readBody(request)));
  } catch (error) {
    return errorResponse(error);
  }
}

export async function PUT(request: Request) {
  try {
    return stateResponse(await replacePortfolioCloud(await readBody(request)));
  } catch (error) {
    return errorResponse(error);
  }
}

export async function DELETE(request: Request) {
  try {
    return stateResponse(await deletePortfolioCloud(await readBody(request)));
  } catch (error) {
    return errorResponse(error);
  }
}
