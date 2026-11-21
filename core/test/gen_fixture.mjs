// Regenerates core/test/fixtures with the REAL signing tool, so the C++ core is checked against
// exactly what the server and CLI produce.   node core/test/gen_fixture.mjs
// The private key lives in a temp dir and is deleted; only the public key and signature are kept.
import { execFileSync } from 'node:child_process';
import { mkdtempSync, mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = (p) => fileURLToPath(new URL(p, import.meta.url));
const signTool = here('../../tools/sign/sign.js');
const outDir = here('./fixtures');
mkdirSync(outDir, { recursive: true });

const artifact = Buffer.alloc(4096);
for (let i = 0; i < artifact.length; i++) artifact[i] = (i * 31 + 7) & 0xff;
const artifactPath = join(outDir, 'artifact.bin');
writeFileSync(artifactPath, artifact);

const tmp = mkdtempSync(join(tmpdir(), 'triskelion-fixture-'));
try {
  const keyFile = join(tmp, 'fixture.key');
  execFileSync('node', [signTool, 'keygen', keyFile], { stdio: 'ignore' });
  const { publicKey } = JSON.parse(readFileSync(keyFile, 'utf8'));
  const out = execFileSync('node', [
    signTool, 'sign', '--key', keyFile, '--app', 'locked', '--platform', 'android',
    '--version', '1.1.0', '--file', artifactPath,
  ]);
  const r = JSON.parse(out);
  writeFileSync(join(outDir, 'vector.txt'), [
    `publicKey=${publicKey}`, `app=${r.appId}`, `platform=${r.platform}`, `version=${r.version}`,
    `sha256=${r.sha256}`, `size=${r.size}`, `signature=${r.signature}`, '',
  ].join('\n'));
  console.log('fixture written to', outDir);
} finally {
  rmSync(tmp, { recursive: true, force: true });
}
