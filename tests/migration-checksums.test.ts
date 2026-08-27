import assert from 'node:assert/strict';
import test from 'node:test';
import { resolveMigrationChecksum } from '../lib/db/migration-checksums.ts';

const KNOWN_LEGACY_FILE = '20260825000000_portfolio.sql';
const KNOWN_LEGACY_CHECKSUM =
  '4fcd5407bcd8b7f09324a8dd72216dfe4ae2ed44da66127ae4ada3734e7d2e5e';

test('resolveMigrationChecksum applies a migration with no recorded checksum', () => {
  assert.equal(
    resolveMigrationChecksum('any.sql', 'abc', undefined),
    'apply',
  );
});

test('resolveMigrationChecksum matches when checksums are equal', () => {
  assert.equal(
    resolveMigrationChecksum('any.sql', 'abc', 'abc'),
    'match',
  );
});

test('resolveMigrationChecksum tolerates the known legacy checksum for the portfolio migration', () => {
  assert.equal(
    resolveMigrationChecksum(KNOWN_LEGACY_FILE, 'current-file-checksum', KNOWN_LEGACY_CHECKSUM),
    'legacy-match',
  );
});

test('resolveMigrationChecksum rejects an unknown checksum for the whitelisted file', () => {
  assert.equal(
    resolveMigrationChecksum(KNOWN_LEGACY_FILE, 'current-file-checksum', 'some-other-checksum'),
    'conflict',
  );
});

test('resolveMigrationChecksum rejects a mismatched checksum for a file with no whitelist entry', () => {
  assert.equal(
    resolveMigrationChecksum('20260826000000_portfolio_replace_rpc.sql', 'current', 'stale'),
    'conflict',
  );
});

test('resolveMigrationChecksum does not tolerate the legacy checksum for a different file name', () => {
  assert.equal(
    resolveMigrationChecksum('some-other-file.sql', 'current', KNOWN_LEGACY_CHECKSUM),
    'conflict',
  );
});
