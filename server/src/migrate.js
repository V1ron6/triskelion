import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { createDb } from './db.js';

const url = process.env.DATABASE_URL;
if (!url) throw new Error('Missing env var DATABASE_URL');

const db = createDb(url);
await db.query(readFileSync(fileURLToPath(new URL('../config/schema.sql', import.meta.url)), 'utf8'));
await db.end();
console.log('schema applied');
