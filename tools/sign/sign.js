#!/usr/bin/env node
// Offline release signing + one-command publishing.
//   node sign.js keygen <keyfile>
//   node sign.js sign    --key <keyfile> --app <id> --platform <android|windows> --version <x.y.z> --file <artifact>
//   node sign.js publish --key <keyfile> --app ... --platform ... --version ... --file ... \
//                        [--channel stable|beta] [--rollout 100] [--min-version x.y.z] [--changelog "..."]
// publish needs TRISKELION_URL and ADMIN_TOKEN in the environment.
import { createHash, createPrivateKey, generateKeyPairSync, sign } from 'node:crypto';
import { readFileSync, writeFileSync, existsSync } from 'node:fs';
import { basename } from 'node:path';
import { manifestMessage } from '../../server/src/crypto.js';

const [cmd, ...rest] = process.argv.slice(2);
const flags = Object.fromEntries(
  rest.flatMap((a, i) => (a.startsWith('--') ? [[a.slice(2), rest[i + 1]]] : [])),
);
const die = (msg) => { console.error(msg); process.exit(1); };

if (cmd === 'keygen') {
  const file = rest[0] || die('usage: keygen <keyfile>');
  if (existsSync(file)) die(`${file} already exists, refusing to overwrite`);
  const { privateKey } = generateKeyPairSync('ed25519');
  const jwk = privateKey.export({ format: 'jwk' });
  const keyfile = {
    seed: Buffer.from(jwk.d, 'base64url').toString('base64'),
    publicKey: Buffer.from(jwk.x, 'base64url').toString('base64'),
  };
  writeFileSync(file, JSON.stringify(keyfile, null, 2), { mode: 0o600 });
  console.log(`private key written to ${file} (keep it offline, never commit it)`);
  console.log(`public key (register with the app, embed in the client):\n${keyfile.publicKey}`);
} else if (cmd === 'sign' || cmd === 'publish') {
  for (const k of ['key', 'app', 'platform', 'version', 'file']) if (!flags[k]) die(`missing --${k}`);
  const kf = JSON.parse(readFileSync(flags.key, 'utf8'));
  const privateKey = createPrivateKey({
    key: {
      kty: 'OKP', crv: 'Ed25519',
      d: Buffer.from(kf.seed, 'base64').toString('base64url'),
      x: Buffer.from(kf.publicKey, 'base64').toString('base64url'),
    },
    format: 'jwk',
  });

  const bytes = readFileSync(flags.file);
  const sha256 = createHash('sha256').update(bytes).digest('hex');
  const size = bytes.length;
  const msg = manifestMessage({ appId: flags.app, platform: flags.platform, version: flags.version, sha256, size });
  const signature = sign(null, Buffer.from(msg), privateKey).toString('base64');
  const safeName = basename(flags.file).replace(/[^A-Za-z0-9._-]/g, '_').slice(0, 100);
  const body = {
    appId: flags.app, platform: flags.platform, version: flags.version,
    channel: flags.channel || 'stable',
    rolloutPct: flags.rollout ? Number(flags.rollout) : 100,
    minVersion: flags['min-version'] || undefined,
    changelog: flags.changelog || '',
    sha256, size, signature,
    artifactKey: `${flags.app}/${flags.platform}/${flags.version}/${safeName}`,
  };

  if (cmd === 'sign') {
    console.log(JSON.stringify(body, null, 2));
  } else {
    const base = process.env.TRISKELION_URL || die('set TRISKELION_URL');
    const auth = { authorization: `Bearer ${process.env.ADMIN_TOKEN || die('set ADMIN_TOKEN')}` };
    const json = { ...auth, 'content-type': 'application/json' };

    const up = await fetch(`${base}/admin/upload-url`, {
      method: 'POST', headers: json,
      body: JSON.stringify({ appId: flags.app, platform: flags.platform, version: flags.version, filename: safeName }),
    });
    if (!up.ok) die(`upload-url failed: ${up.status} ${await up.text()}`);
    const { key, url } = await up.json();
    body.artifactKey = key;

    const put = await fetch(url, { method: 'PUT', body: bytes });
    if (!put.ok) die(`artifact upload failed: ${put.status}`);

    const rel = await fetch(`${base}/admin/releases`, { method: 'POST', headers: json, body: JSON.stringify(body) });
    if (!rel.ok) die(`publish failed: ${rel.status} ${await rel.text()}`);
    const out = await rel.json();
    console.log(`published ${flags.app} ${flags.version} (${flags.platform}), broadcast: ${JSON.stringify(out.broadcast)}`);
  }
} else {
  die('commands: keygen | sign | publish');
}
