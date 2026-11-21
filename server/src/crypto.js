import { createHash, createPublicKey, verify } from 'node:crypto';

// The signed message binds a release to one app, platform, version and exact artifact bytes.
// Clients rebuild this string themselves, so a forged server response fails verification.
export function manifestMessage({ appId, platform, version, sha256, size }) {
  return `triskelion:v1\n${appId}\n${platform}\n${version}\n${sha256}\n${size}`;
}

export function verifySignature(publicKeyB64, message, signatureB64) {
  try {
    const raw = Buffer.from(publicKeyB64, 'base64');
    if (raw.length !== 32) return false;
    const key = createPublicKey({
      key: { kty: 'OKP', crv: 'Ed25519', x: raw.toString('base64url') },
      format: 'jwk',
    });
    const sig = Buffer.from(signatureB64, 'base64');
    if (sig.length !== 64) return false;
    return verify(null, Buffer.from(message), key, sig);
  } catch {
    return false;
  }
}

const SEMVER = /^(\d+)\.(\d+)\.(\d+)$/;

export const isSemver = (v) => typeof v === 'string' && SEMVER.test(v);

export function compareVersions(a, b) {
  const pa = a.match(SEMVER).slice(1).map(Number);
  const pb = b.match(SEMVER).slice(1).map(Number);
  for (let i = 0; i < 3; i++) if (pa[i] !== pb[i]) return pa[i] < pb[i] ? -1 : 1;
  return 0;
}

// Stable 0-99 bucket per install+app, so a staged rollout never flaps.
export function rolloutBucket(installId, appId) {
  const h = createHash('sha256').update(`${installId}:${appId}`).digest();
  return h.readUInt32BE(0) % 100;
}

export const inRollout = (installId, appId, pct) => rolloutBucket(installId, appId) < pct;
