import { describe, expect, it } from 'vitest';
import { createPrivateKey, generateKeyPairSync } from 'node:crypto';
import { KeyFileError, createKeyring, manifestMessage, parseKeyFile, sha256Hex } from '../src/data/signing.js';
import { ApiError, createApi } from '../src/data/api.js';
import { manifestMessage as serverMessage, verifySignature } from '../../server/src/crypto.js';

// A key file exactly as `tools/sign/sign.js keygen` writes it.
function keyFile() {
  const { privateKey } = generateKeyPairSync('ed25519');
  const jwk = privateKey.export({ format: 'jwk' });
  const file = { seed: Buffer.from(jwk.d, 'base64url').toString('base64'), publicKey: Buffer.from(jwk.x, 'base64url').toString('base64') };
  return { file, text: JSON.stringify(file, null, 2), privateKey };
}
const manifest = { appId: 'counter', platform: 'windows', version: '1.1.0', sha256: 'ab'.repeat(32), size: 5000 };

describe('key file', () => {
  it('derives the public key and rejects damaged files', () => {
    const k = keyFile();
    expect(parseKeyFile(k.text).publicKey).toBe(k.file.publicKey);
    expect(() => parseKeyFile('nope')).toThrow(KeyFileError);
    expect(() => parseKeyFile('{}')).toThrow(/key_bad_seed/);
    expect(() => parseKeyFile(JSON.stringify({ seed: 'AAAA' }))).toThrow(/key_bad_seed/);
    expect(() => parseKeyFile(JSON.stringify({ seed: '!!!' }))).toThrow(/key_bad_seed/);
    expect(() => parseKeyFile(JSON.stringify({ ...k.file, publicKey: keyFile().file.publicKey }))).toThrow(/key_mismatch/);
    expect(parseKeyFile(JSON.stringify({ seed: k.file.seed })).publicKey).toBe(k.file.publicKey); // public key line optional
  });
});

describe('signing matches the server and the clients', () => {
  it('uses the exact same message as the server', () => {
    expect(manifestMessage(manifest)).toBe(serverMessage(manifest));
  });

  it('signatures verify with the server code, and break if anything changes', () => {
    const k = keyFile();
    const ring = createKeyring();
    ring.load(k.text);
    const sig = ring.sign(manifestMessage(manifest));
    expect(verifySignature(k.file.publicKey, serverMessage(manifest), sig)).toBe(true);
    expect(verifySignature(k.file.publicKey, serverMessage({ ...manifest, version: '1.1.1' }), sig)).toBe(false);
    expect(verifySignature(keyFile().file.publicKey, serverMessage(manifest), sig)).toBe(false);
    // Ed25519 is deterministic: the browser produces the same signature as Node's own signer
    const nodeSig = require_sign(k.privateKey, serverMessage(manifest));
    expect(sig).toBe(nodeSig);
    ring.forget();
  });

  it('hashes like sha256sum', async () => {
    expect(await sha256Hex(new TextEncoder().encode('abc'))).toBe('ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad');
  });
});

import { sign as nodeSign } from 'node:crypto';
function require_sign(privateKey, message) {
  return nodeSign(null, Buffer.from(message), privateKey).toString('base64');
}

describe('keyring', () => {
  it('holds the key only until forgotten, and never exposes it', () => {
    const k = keyFile();
    const ring = createKeyring();
    let events = 0;
    ring.subscribe(() => events++);
    expect(ring.loaded()).toBe(false);
    expect(() => ring.sign('x')).toThrow(/no_key_loaded/);
    expect(ring.load(k.text)).toBe(k.file.publicKey);
    expect(ring.loaded()).toBe(true);
    expect(ring.publicKey()).toBe(k.file.publicKey);
    const dump = JSON.stringify(ring) + Object.keys(ring).join(',') + String(Object.values(ring).map((v) => typeof v));
    expect(dump).not.toContain(k.file.seed);
    ring.forget();
    expect(ring.loaded()).toBe(false);
    expect(ring.publicKey()).toBe(null);
    expect(() => ring.sign('x')).toThrow(/no_key_loaded/);
    expect(events).toBe(2);
  });

  it('a bad file leaves an already loaded key untouched and loads nothing new', () => {
    const k = keyFile();
    const ring = createKeyring();
    expect(() => ring.load('garbage')).toThrow();
    expect(ring.loaded()).toBe(false);
    ring.load(k.text);
    expect(() => ring.load('garbage')).toThrow();
    expect(ring.publicKey()).toBe(k.file.publicKey);
  });

  it('forgets after the idle time, and using the key restarts the clock', () => {
    const timers = [];
    let cleared = 0;
    const ring = createKeyring({ idleMs: 1000, setTimer: (fn, ms) => { timers.push({ fn, ms }); return timers.length; }, clearTimer: () => { cleared++; } });
    ring.load(keyFile().text);
    expect(timers.length).toBe(1);
    expect(timers[0].ms).toBe(1000);
    ring.sign('hello');
    expect(timers.length).toBe(2);   // re-armed by use
    expect(cleared).toBeGreaterThanOrEqual(1);
    timers[1].fn();                  // idle time elapsed
    expect(ring.loaded()).toBe(false);
  });
});

describe('publishing calls', () => {
  it('builds the key and publish requests', async () => {
    const calls = [];
    const api = createApi({ baseUrl: 'https://x', token: 't', fetchImpl: async (url, init) => { calls.push([init.method, url, init.body]); return { ok: true, json: async () => ({}) }; } });
    await api.listKeys('a b');
    await api.addKey('counter', 'K');
    await api.retireKey('counter', 'K');
    await api.uploadUrl({ appId: 'counter', platform: 'windows', version: '1.1.0', filename: 'x.exe' });
    await api.publishRelease({ appId: 'counter' });
    expect(calls).toEqual([
      ['GET', 'https://x/admin/apps/a%20b/keys', undefined],
      ['POST', 'https://x/admin/apps/counter/keys', '{"publicKey":"K"}'],
      ['POST', 'https://x/admin/apps/counter/keys/retire', '{"publicKey":"K"}'],
      ['POST', 'https://x/admin/upload-url', '{"appId":"counter","platform":"windows","version":"1.1.0","filename":"x.exe"}'],
      ['POST', 'https://x/admin/releases', '{"appId":"counter"}'],
    ]);
  });

  it('uploads to storage without the admin token, and explains failures', async () => {
    let seen;
    const ok = createApi({ baseUrl: 'https://x', token: 'SECRET', fetchImpl: async (url, init) => { seen = { url, init }; return { ok: true }; } });
    await ok.uploadArtifact('https://r2.example/put?sig=1', new Uint8Array([1]));
    expect(seen.url).toBe('https://r2.example/put?sig=1');
    expect(seen.init.method).toBe('PUT');
    expect(JSON.stringify(seen.init)).not.toContain('SECRET');
    expect(seen.init.headers).toBeUndefined();
    const blocked = createApi({ baseUrl: 'https://x', token: 't', fetchImpl: async () => { throw new TypeError('cors'); } });
    await expect(blocked.uploadArtifact('u', 'b')).rejects.toMatchObject({ code: 'upload_blocked', status: 0 });
    const refused = createApi({ baseUrl: 'https://x', token: 't', fetchImpl: async () => ({ ok: false, status: 403 }) });
    await expect(refused.uploadArtifact('u', 'b')).rejects.toBeInstanceOf(ApiError);
    await expect(refused.uploadArtifact('u', 'b')).rejects.toMatchObject({ code: 'upload_failed', status: 403 });
  });
});
