// Local demo stack: the real Express app on in-memory Postgres, with "R2" faked by a local HTTPS blob store.
// Usage: node demo/server.mjs <apiPort> <blobPort> <certDir> <adminToken>
import { readFileSync } from 'node:fs';
import https from 'node:https';
import { PGlite } from '@electric-sql/pglite';
import { createApp } from '../src/app.js';
import { createBroadcaster } from '../src/broadcast.js';

const [apiPort, blobPort, certDir, adminToken] = process.argv.slice(2);
const db = new PGlite();
await db.exec(readFileSync(new URL('../config/schema.sql', import.meta.url), 'utf8'));

const blobs = new Map();
const base = `https://127.0.0.1:${blobPort}`;
const storage = {
  presignGet: async (key) => `${base}/blob/${key}`,
  presignPut: async (key) => `${base}/blob/${key}`,
};
https.createServer({ key: readFileSync(`${certDir}/key.pem`), cert: readFileSync(`${certDir}/cert.pem`) }, (req, res) => {
  const key = decodeURIComponent(req.url.replace('/blob/', '').split('?')[0]);
  // Like the CORS rule you set on the real R2 bucket: lets the admin page in a browser PUT the file.
  res.setHeader('access-control-allow-origin', '*');
  res.setHeader('access-control-allow-methods', 'PUT, GET');
  res.setHeader('access-control-allow-headers', '*');
  if (req.method === 'OPTIONS') { res.statusCode = 204; return res.end(); }
  if (req.method === 'PUT') {
    const chunks = [];
    req.on('data', (c) => chunks.push(c));
    req.on('end', () => { blobs.set(key, Buffer.concat(chunks)); res.end('ok'); });
  } else if (req.method === 'GET' && blobs.has(key)) {
    res.setHeader('content-length', blobs.get(key).length);
    res.end(blobs.get(key));
  } else { res.statusCode = 404; res.end(); }
}).listen(Number(blobPort), '127.0.0.1');

// Demo-only control hook: lets the demo script swap a stored file, like a compromised bucket would.
import http from 'node:http';
http.createServer((req, res) => {
  const key = decodeURIComponent(req.url.slice(1));
  const chunks = [];
  req.on('data', (c) => chunks.push(c));
  req.on('end', () => { blobs.set(key, Buffer.concat(chunks)); res.end('tampered'); });
}).listen(Number(blobPort) + 1, '127.0.0.1');

const app = createApp({ db, storage, broadcaster: createBroadcaster(), adminToken });
app.listen(Number(apiPort), '127.0.0.1', () => console.log('READY'));
