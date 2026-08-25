import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import test from 'node:test';

test('认证 Server Action 文件只导出异步函数', () => {
  const source = readFileSync(new URL('../app/auth/actions.ts', import.meta.url), 'utf8');

  assert.match(source, /^['"]use server['"];?/m);
  assert.doesNotMatch(source, /^export\s+(?:const|let|var|class|default)\b/m);
  assert.doesNotMatch(source, /^export\s*\{/m);
});
