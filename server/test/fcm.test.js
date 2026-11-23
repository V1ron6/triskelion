import test from 'node:test';
import assert from 'node:assert/strict';
import { generateKeyPairSync, createVerify } from 'node:crypto';
import { createFcmNotifier } from '../src/fcm.js';

const { privateKey, publicKey } = generateKeyPairSync('rsa', { modulusLength: 2048 });
const serviceAccount = {
  client_email: 'svc@proj.iam.gserviceaccount.com', project_id: 'proj',
  private_key: privateKey.export({ type: 'pkcs8', format: 'pem' }),
};
const target = { install: { id: 'aaaaaaaa-0000-4000-8000-000000000001', push_token: 'tok1' }, appId: 'locked', version: '1.1.0' };

function harness(sendResponses) {
  const calls = []; const dbCalls = [];
  const fetchImpl = async (url, opts) => {
    calls.push({ url: String(url), opts });
    if (String(url).includes('oauth2')) return new Response(JSON.stringify({ access_token: 'AT' + calls.length, expires_in: 3600 }), { status: 200 });
    const r = sendResponses.shift();
    return new Response(JSON.stringify(r.body || {}), { status: r.status });
  };
  const db = { query: async (...a) => { dbCalls.push(a); return { rows: [] }; } };
  return { calls, dbCalls, notify: createFcmNotifier({ serviceAccount, db, fetchImpl }) };
}

test('sends a data-only high-priority message with a valid signed JWT, and caches the token', async () => {
  const h = harness([{ status: 200 }, { status: 200 }]);
  await h.notify(target);
  await h.notify(target);
  const auth = h.calls.filter((c) => c.url.includes('oauth2'));
  assert.equal(auth.length, 1);
  const assertion = new URLSearchParams(auth[0].opts.body).get('assertion');
  const [head, claims, sig] = assertion.split('.');
  assert.ok(createVerify('RSA-SHA256').update(`${head}.${claims}`).verify(publicKey, Buffer.from(sig, 'base64url')));
  const c = JSON.parse(Buffer.from(claims, 'base64url'));
  assert.equal(c.iss, serviceAccount.client_email);
  assert.match(c.scope, /firebase\.messaging/);
  const sends = h.calls.filter((c2) => c2.url.includes('fcm.googleapis.com'));
  assert.equal(sends.length, 2);
  assert.equal(sends[0].url, 'https://fcm.googleapis.com/v1/projects/proj/messages:send');
  const msg = JSON.parse(sends[0].opts.body).message;
  assert.deepEqual(msg.data, { triskelion: 'update', app: 'locked', version: '1.1.0' });
  assert.equal(msg.token, 'tok1');
  assert.equal(msg.android.priority, 'HIGH');
  assert.equal(msg.notification, undefined);
});

test('installs without a push token are skipped', async () => {
  const h = harness([]);
  await h.notify({ ...target, install: { id: 'x', push_token: null } });
  assert.equal(h.calls.length, 0);
});

test('an UNREGISTERED token is dropped; other failures throw', async () => {
  const h = harness([{ status: 404, body: { error: { details: [{ errorCode: 'UNREGISTERED' }] } } }, { status: 500 }]);
  await h.notify(target);
  assert.equal(h.dbCalls.length, 1);
  assert.match(h.dbCalls[0][0], /push_token = NULL/);
  assert.deepEqual(h.dbCalls[0][1], [target.install.id]);
  await assert.rejects(h.notify(target), /http 500/);
  assert.equal(h.dbCalls.length, 1);
});

test('a 401 refreshes the access token once and retries', async () => {
  const h = harness([{ status: 401 }, { status: 200 }]);
  await h.notify(target);
  assert.equal(h.calls.filter((c) => c.url.includes('oauth2')).length, 2);
});

test('rejects an incomplete service account', () => {
  assert.throws(() => createFcmNotifier({ serviceAccount: { client_email: 'a' }, db: {} }), /needs/);
});
