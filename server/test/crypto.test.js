import test from 'node:test';
import assert from 'node:assert/strict';
import { generateKeyPairSync, sign } from 'node:crypto';
import {
  compareVersions, inRollout, isSemver, manifestMessage, rolloutBucket, verifySignature,
} from '../src/crypto.js';

const keypair = () => {
  const { privateKey } = generateKeyPairSync('ed25519');
  const jwk = privateKey.export({ format: 'jwk' });
  return { privateKey, pub: Buffer.from(jwk.x, 'base64url').toString('base64') };
};
const manifest = { appId: 'locked', platform: 'android', version: '1.2.0', sha256: 'a'.repeat(64), size: 1234 };

test('valid signature verifies', () => {
  const { privateKey, pub } = keypair();
  const msg = manifestMessage(manifest);
  const sig = sign(null, Buffer.from(msg), privateKey).toString('base64');
  assert.equal(verifySignature(pub, msg, sig), true);
});

test('tampered manifest fails verification', () => {
  const { privateKey, pub } = keypair();
  const sig = sign(null, Buffer.from(manifestMessage(manifest)), privateKey).toString('base64');
  for (const patch of [{ version: '1.2.1' }, { sha256: 'b'.repeat(64) }, { size: 1235 }, { platform: 'windows' }, { appId: 'hollow' }]) {
    assert.equal(verifySignature(pub, manifestMessage({ ...manifest, ...patch }), sig), false, JSON.stringify(patch));
  }
});

test('wrong key and garbage input fail without throwing', () => {
  const a = keypair();
  const b = keypair();
  const msg = manifestMessage(manifest);
  const sig = sign(null, Buffer.from(msg), a.privateKey).toString('base64');
  assert.equal(verifySignature(b.pub, msg, sig), false);
  assert.equal(verifySignature('not-a-key', msg, sig), false);
  assert.equal(verifySignature(a.pub, msg, 'AAAA'), false);
});

test('semver compare and validation', () => {
  assert.equal(compareVersions('1.2.3', '1.2.3'), 0);
  assert.equal(compareVersions('1.2.3', '1.10.0'), -1);
  assert.equal(compareVersions('2.0.0', '1.99.99'), 1);
  assert.equal(isSemver('1.2'), false);
  assert.equal(isSemver('1.2.3-beta'), false);
});

test('rollout bucket is deterministic and roughly uniform', () => {
  assert.equal(rolloutBucket('abc', 'locked'), rolloutBucket('abc', 'locked'));
  assert.equal(inRollout('abc', 'locked', 0), false);
  assert.equal(inRollout('abc', 'locked', 100), true);
  let inside = 0;
  for (let i = 0; i < 5000; i++) if (inRollout(`install-${i}`, 'locked', 20)) inside++;
  assert.ok(inside > 800 && inside < 1200, `20% rollout hit ${inside}/5000`);
});
