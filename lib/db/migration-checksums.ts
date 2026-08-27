/**
 * Known-legacy checksums for migrations whose recorded database checksum no
 * longer matches the checksum of the file currently tracked in git.
 *
 * Root cause (2026-08-27 incident): `supabase/migrations/20260825000000_portfolio.sql`
 * was applied to the production database at 2026-08-25T10:04:34.576Z, but the
 * matching commit (799008e) was made at 2026-08-25T10:24:53+08:00
 * (2026-08-25T10:24:53Z), ~20 minutes later. The file was evidently edited
 * locally between the `db:migrate` run and the `git commit`, so the checksum
 * git has always tracked never matches what production recorded. `git log`
 * confirms the file has never changed since that single commit, so this is
 * not drift introduced afterwards — it is a pre-commit edit that predates
 * version control entirely and cannot be recovered from git history.
 *
 * Each entry below was confirmed by a read-only query against
 * `private.zhiheng_schema_migrations` (name, checksum, applied_at only — no
 * credentials or row contents beyond those three columns were inspected or
 * recorded here). Because every statement in that migration is idempotent
 * (`create table/index if not exists`, `create or replace function`,
 * `drop policy if exists` + `create policy`), tolerating this checksum is
 * safe: the runner skips re-executing the file rather than guessing at, or
 * silently rewriting, what the original pre-commit contents were.
 *
 * Do NOT add entries here to silence a checksum mismatch you don't fully
 * understand — that defeats the point of checksum verification. Only add an
 * entry once you have independently confirmed, from the database's own
 * migration history table, that the recorded checksum corresponds to a
 * legitimate historical deployment of this exact migration name.
 */
const KNOWN_LEGACY_CHECKSUMS: Readonly<Record<string, readonly string[]>> = {
  '20260825000000_portfolio.sql': [
    '4fcd5407bcd8b7f09324a8dd72216dfe4ae2ed44da66127ae4ada3734e7d2e5e',
  ],
};

export type MigrationChecksumDecision =
  | 'apply'
  | 'match'
  | 'legacy-match'
  | 'conflict';

/**
 * Decides what the migration runner should do for a single file, given the
 * checksum of the file as it exists on disk today and the checksum (if any)
 * already recorded in the database for that migration name.
 */
export function resolveMigrationChecksum(
  file: string,
  fileChecksum: string,
  appliedChecksum: string | undefined,
): MigrationChecksumDecision {
  if (appliedChecksum === undefined) {
    return 'apply';
  }
  if (appliedChecksum === fileChecksum) {
    return 'match';
  }
  const legacyChecksums = KNOWN_LEGACY_CHECKSUMS[file] ?? [];
  if (legacyChecksums.includes(appliedChecksum)) {
    return 'legacy-match';
  }
  return 'conflict';
}
