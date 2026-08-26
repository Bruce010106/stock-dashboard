import assert from 'node:assert/strict';
import test from 'node:test';

import { getEastmoneyUniverse } from '../lib/data/eastmoney-provider.ts';

type FetchArgs = Parameters<typeof fetch>;
type FetchHandler = (url: string, init?: FetchArgs[1]) => Promise<Response> | Response;

function code(index: number): string {
  return String(index).padStart(6, '0');
}

function jsonResponse(body: unknown, opts: { ok?: boolean; status?: number } = {}): Response {
  const ok = opts.ok ?? true;
  const status = opts.status ?? (ok ? 200 : 500);
  return {
    ok,
    status,
    json: async () => body,
    text: async () => JSON.stringify(body),
  } as unknown as Response;
}

function brokenJsonResponse(): Response {
  return {
    ok: true,
    status: 200,
    json: async () => {
      throw new SyntaxError('Unexpected token in JSON');
    },
    text: async () => 'not json',
  } as unknown as Response;
}

function eastmoneySuccessBody(count: number, total = count) {
  return {
    data: {
      total,
      diff: Array.from({ length: count }, (_, index) => ({ f12: code(index), f14: `股票${index}` })),
    },
  };
}

function sinaHandler(total: number, opts: { failFromPage?: number } = {}): FetchHandler {
  return (url) => {
    if (url.includes('getHQNodeStockCount')) {
      return {
        ok: true,
        status: 200,
        json: async () => null,
        text: async () => `"${total}"`,
      } as unknown as Response;
    }
    if (url.includes('getHQNodeData')) {
      const params = new URL(url).searchParams;
      const page = Number(params.get('page'));
      const num = Number(params.get('num'));
      if (opts.failFromPage != null && page >= opts.failFromPage) {
        throw new Error(`sina page ${page} failed`);
      }
      const start = (page - 1) * num;
      const rows = Array.from({ length: Math.max(0, Math.min(num, total - start)) }, (_, i) => {
        const idx = start + i;
        return { code: code(idx), name: `股票${idx}` };
      });
      return jsonResponse(rows);
    }
    throw new Error(`unexpected sina url in test: ${url.split('?')[0]}`);
  };
}

function installFetchMock(handler: FetchHandler): () => void {
  const original = globalThis.fetch;
  globalThis.fetch = (async (input: FetchArgs[0], init?: FetchArgs[1]) => {
    const url = typeof input === 'string' ? input : input.toString();
    return handler(url, init);
  }) as typeof fetch;
  return () => {
    globalThis.fetch = original;
  };
}

function dispatch(eastmoney: FetchHandler, sina: FetchHandler): FetchHandler {
  return (url, init) => {
    if (url.includes('push2.eastmoney.com')) return eastmoney(url, init);
    return sina(url, init);
  };
}

test('东财返回完整股票池时直接使用，不回退新浪', async (t) => {
  let eastmoneyCalls = 0;
  let sinaCalls = 0;
  const restore = installFetchMock(dispatch(
    () => {
      eastmoneyCalls += 1;
      return jsonResponse(eastmoneySuccessBody(4_200));
    },
    () => {
      sinaCalls += 1;
      throw new Error('新浪不应被调用');
    },
  ));
  t.after(restore);

  const result = await getEastmoneyUniverse();
  assert.equal(result.length, 4_200);
  assert.equal(eastmoneyCalls, 1);
  assert.equal(sinaCalls, 0);
});

const eastmoneyFailureScenarios: Array<[string, FetchHandler]> = [
  ['网络异常/超时抛出', () => {
    throw new TypeError('fetch failed');
  }],
  ['非 2xx 响应', () => jsonResponse({}, { ok: false, status: 503 })],
  ['JSON 解析失败', () => brokenJsonResponse()],
  ['空数据负载', () => jsonResponse({ data: { total: 0, diff: [] } })],
  ['结果被截断', () => jsonResponse(eastmoneySuccessBody(100, 5_000))],
  ['归一化后数量异常偏少', () => jsonResponse(eastmoneySuccessBody(50, 50))],
];

for (const [label, eastmoneyHandler] of eastmoneyFailureScenarios) {
  test(`东财${label}时回退新浪并成功返回完整股票池`, async (t) => {
    const restore = installFetchMock(dispatch(eastmoneyHandler, sinaHandler(4_500)));
    t.after(restore);

    const result = await getEastmoneyUniverse();
    assert.equal(result.length, 4_500);
  });
}

test('东财失败且新浪股票池不完整时拒绝返回残缺数据', async (t) => {
  const restore = installFetchMock(dispatch(
    () => jsonResponse({}, { ok: false, status: 500 }),
    sinaHandler(4_500, { failFromPage: 6 }),
  ));
  t.after(restore);

  await assert.rejects(getEastmoneyUniverse(), (error: unknown) => {
    assert.ok(error instanceof Error);
    assert.match(error.message, /东方财富源/);
    assert.match(error.message, /新浪财经源/);
    assert.doesNotMatch(error.message, /https?:\/\//);
    return true;
  });
});

test('东财与新浪均失败时抛出脱敏聚合错误', async (t) => {
  const restore = installFetchMock(dispatch(
    () => {
      throw new TypeError('fetch failed');
    },
    () => jsonResponse({}, { ok: false, status: 502 }),
  ));
  t.after(restore);

  await assert.rejects(getEastmoneyUniverse(), (error: unknown) => {
    assert.ok(error instanceof Error);
    assert.match(error.message, /东方财富源/);
    assert.match(error.message, /新浪财经源/);
    assert.doesNotMatch(error.message, /https?:\/\//);
    assert.doesNotMatch(error.message, /at fetchEastmoneyUniverse/);
    return true;
  });
});
