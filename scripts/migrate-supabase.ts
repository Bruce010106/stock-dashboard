import { createHash } from 'node:crypto';
import { readdir, readFile } from 'node:fs/promises';
import path from 'node:path';
import postgres from 'postgres';

const connectionString = process.env.POSTGRES_URL_NON_POOLING
  ?? process.env.POSTGRES_URL;

if (!connectionString) {
  throw new Error('缺少 POSTGRES_URL_NON_POOLING 或 POSTGRES_URL，无法执行数据库迁移。');
}

const migrationsDirectory = path.join(process.cwd(), 'supabase', 'migrations');
const migrationFiles = (await readdir(migrationsDirectory))
  .filter((file) => file.endsWith('.sql'))
  .sort();

const sql = postgres(connectionString, {
  max: 1,
  prepare: false,
  ssl: 'require',
});

try {
  await sql`create schema if not exists private`;
  await sql`
    create table if not exists private.zhiheng_schema_migrations (
      name text primary key,
      checksum text not null,
      applied_at timestamptz not null default timezone('utc', now())
    )
  `;

  for (const file of migrationFiles) {
    const migration = await readFile(path.join(migrationsDirectory, file), 'utf8');
    const checksum = createHash('sha256').update(migration).digest('hex');
    const applied = await sql<{ checksum: string }[]>`
      select checksum
      from private.zhiheng_schema_migrations
      where name = ${file}
    `;

    if (applied[0]?.checksum === checksum) {
      console.log(`skip ${file}`);
      continue;
    }
    if (applied.length > 0) {
      throw new Error(`迁移 ${file} 已执行但内容发生变化，请创建新的迁移文件。`);
    }

    await sql.begin(async (transaction) => {
      await transaction.unsafe(migration);
      await transaction`
        insert into private.zhiheng_schema_migrations (name, checksum)
        values (${file}, ${checksum})
      `;
    });
    console.log(`applied ${file}`);
  }
} finally {
  await sql.end();
}
