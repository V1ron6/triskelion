import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { generateKeyPairSync, sign } from 'node:crypto';
import { PGlite } from '@electric-sql/pglite';
import { createApp } from '../src/app.js';
import { createBroadcaster } from '../src/broadcast.js';
import { manifestMessage, verifySignature } from '../src/crypto.js';

const ADMIN = 'test-admin-token';
const INSTALL_A = 'aaaaaaaa-0000-4000-8000-000000000001';
const INSTALL_B = 'bbbbbbbb-0000-4000-8000-000000000002';

const { privateKey } = generateKeyPairSync('ed25519');
const publicKey = Buffer.from(privateKey.export({ format: 'jwk' }).x, 'base64url').toString('base64');

const release = (version, extra = {}) => {
  const m = { appId: 'locked', platform: 'android', version, sha256: 'c'.repeat(64), size: 4096 };
  return {
    ...m, artifactKey: `locked/android/${version}/locked.apk`, changelog: `notes ${version}`,
    signature: sign(null, Buffer.from(manifestMessage(m)), privateKey).toString('base64'),
    ...extra,
  };
};

async function boot() {
  const db = new PGlite();
  await db.exec(readFileSync(new URL('../schema.sql', import.meta.url), 'utf8'));
  const storage = {
    presignGet: async (key) => `https://r2.test/get/${key}`,
    presignPut: async (key) => `https://r2.test/put/${key}`,
  };
  const app = createApp({ db, storage, broadcaster: createBroadcaster(), adminToken: ADMIN });
  const server = await new Promise((resolve) => { const s = app.listen(0, () => resolve(s)); });
  const base = `http://127.0.0.1:${server.address().port}`;
  const call = async (method, path, body, token = ADMIN) => {
    const res = await fetch(base + path, {
      method,
      headers: { 'content-type': 'application/json', ...(token ? { authorization: `Bearer ${token}` } : {}) },
      body: body ? JSON.stringify(body) : undefined,
    });
    return { status: res.status, body: await res.json().catch(() => null) };
  };
  const check = (install, version, channel = 'stable') =>
    call('GET', `/v1/check?app=locked&platform=android&version=${version}&install=${install}&channel=${channel}`, null, null);
  const register = (install, version, channel = 'stable') =>
    call('POST', '/v1/register', { install, app: 'locked', platform: 'android', version, channel }, null);
  return { base, call, check, register, close: async () => { server.closeAllConnections(); server.close(); await db.close(); } };
}

test('admin routes need the token', async () => {
  const t = await boot();
  try {
    assert.equal((await t.call('GET', '/admin/apps', null, null)).status, 401);
    assert.equal((await t.call('GET', '/admin/apps', null, 'wrong')).status, 401);
    assert.equal((await t.call('GET', '/admin/apps')).status, 200);
  } finally { await t.close(); }
});

test('publish, broadcast, pull, rollout, yank, channels', async () => {
  const t = await boot();
  try {
    assert.equal((await t.call('POST', '/admin/apps', { id: 'locked', name: 'Locked', publicKey })).status, 201);
    assert.equal((await t.call('POST', '/admin/apps', { id: 'locked', name: 'Locked', publicKey })).status, 409);
    assert.equal((await t.register(INSTALL_A, '1.0.0')).status, 200);
    assert.equal((await t.register(INSTALL_B, '1.0.0')).status, 200);
    assert.equal((await t.register(INSTALL_A, '1.0.0').then(() => t.call('POST', '/v1/register', { install: INSTALL_A, app: 'nope', platform: 'android', version: '1.0.0' }, null))).status, 404);

    // nothing published yet
    assert.deepEqual((await t.check(INSTALL_A, '1.0.0')).body, { update: false });

    // a bad signature is rejected
    const bad = release('1.1.0', { signature: Buffer.alloc(64).toString('base64') });
    assert.equal((await t.call('POST', '/admin/releases', bad)).body.error, 'bad_signature');
    // a signature for different bytes is rejected too
    assert.equal((await t.call('POST', '/admin/releases', release('1.1.0', { size: 5000 }))).body.error, 'bad_signature');

    // listen on SSE as install A, then publish
    const ac = new AbortController();
    const stream = await fetch(`${t.base}/v1/stream?install=${INSTALL_A}`, { signal: ac.signal });
    const reader = stream.body.getReader();
    const decoder = new TextDecoder();
    let seen = '';
    const gotUpdate = (async () => {
      while (!seen.includes('event: update')) {
        const { value, done } = await reader.read();
        if (done) break;
        seen += decoder.decode(value);
      }
      return seen;
    })();

    const pub = await t.call('POST', '/admin/releases', release('1.1.0'));
    assert.equal(pub.status, 201);
    assert.deepEqual(pub.body.broadcast, { targeted: 2, sse: 1 });
    const evt = await Promise.race([gotUpdate, new Promise((_, rej) => setTimeout(() => rej(new Error('no SSE ping')), 3000))]);
    assert.match(evt, /"version":"1.1.0"/);
    ac.abort();

    // pull: update offered, and the client can verify what it was given
    const pull = (await t.check(INSTALL_A, '1.0.0')).body;
    assert.equal(pull.update, true);
    assert.equal(pull.version, '1.1.0');
    assert.equal(pull.url, 'https://r2.test/get/locked/android/1.1.0/locked.apk');
    assert.equal(pull.mandatory, false);
    const msg = manifestMessage({ appId: 'locked', platform: 'android', version: pull.version, sha256: pull.sha256, size: pull.size });
    assert.equal(verifySignature(publicKey, msg, pull.signature), true);
    assert.deepEqual((await t.check(INSTALL_A, '1.1.0')).body, { update: false });

    // duplicate version is rejected
    assert.equal((await t.call('POST', '/admin/releases', release('1.1.0'))).status, 409);

    // staged rollout: 0% offers nothing, widening re-broadcasts and offers it
    const staged = await t.call('POST', '/admin/releases', release('1.2.0', { rolloutPct: 0 }));
    assert.equal(staged.body.broadcast.targeted, 0);
    assert.deepEqual((await t.check(INSTALL_A, '1.1.0')).body, { update: false });
    const widened = await t.call('PATCH', `/admin/releases/${staged.body.release.id}`, { rolloutPct: 100 });
    assert.equal(widened.status, 200);
    assert.equal((await t.check(INSTALL_A, '1.1.0')).body.version, '1.2.0');

    // yank: the bad release stops being offered, older good one still is
    await t.call('POST', `/admin/releases/${staged.body.release.id}/yank`);
    assert.deepEqual((await t.check(INSTALL_A, '1.1.0')).body, { update: false });
    assert.equal((await t.check(INSTALL_B, '1.0.0')).body.version, '1.1.0');

    // beta channel stays away from stable installs
    await t.call('POST', '/admin/releases', release('1.3.0', { channel: 'beta' }));
    assert.equal((await t.check(INSTALL_B, '1.1.0', 'stable')).body.update, false);
    assert.equal((await t.check(INSTALL_B, '1.1.0', 'beta')).body.version, '1.3.0');

    // minVersion makes an update mandatory for installs below it
    await t.call('POST', '/admin/releases', release('1.4.0', { minVersion: '1.2.0' }));
    assert.equal((await t.check(INSTALL_B, '1.1.0')).body.mandatory, true);
    assert.equal((await t.check(INSTALL_B, '1.2.5')).body.mandatory, false);

    // input validation on the public path
    assert.equal((await t.call('GET', '/v1/check?app=locked&platform=ios&version=1.0.0&install=' + INSTALL_A, null, null)).status, 400);
    assert.equal((await t.call('GET', '/v1/check?app=locked&platform=android&version=banana&install=' + INSTALL_A, null, null)).status, 400);
  } finally { await t.close(); }
});
