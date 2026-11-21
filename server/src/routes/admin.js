import { Router } from 'express';
import { createHash, timingSafeEqual } from 'node:crypto';
import { compareVersions, inRollout, isSemver, manifestMessage, verifySignature } from '../crypto.js';

const PLATFORMS = ['android', 'windows'];
const CHANNELS = ['stable', 'beta'];
const SLUG = /^[a-z0-9][a-z0-9._-]{1,63}$/;
const SHA256 = /^[0-9a-f]{64}$/;

const digest = (s) => createHash('sha256').update(String(s)).digest();

export function adminRoutes({ db, storage, broadcaster, adminToken }) {
  const r = Router();

  r.use((req, res, next) => {
    const given = (req.get('authorization') || '').replace(/^Bearer /, '');
    if (!timingSafeEqual(digest(given), digest(adminToken))) return res.status(401).json({ error: 'unauthorized' });
    next();
  });

  const log = (kind, appId, detail) =>
    db.query('INSERT INTO events (kind, app_id, detail) VALUES ($1, $2, $3)', [kind, appId, detail]);

  // Ping every install of this release's app+platform+channel that is behind it and inside the rollout.
  async function broadcast(rel) {
    const { rows } = await db.query(
      `SELECT id, version, push_token FROM installs
       WHERE app_id = $1 AND platform = $2 AND ($3 = 'beta' OR channel = 'stable')`,
      [rel.app_id, rel.platform, rel.channel],
    );
    const targets = rows.filter(
      (i) => compareVersions(i.version, rel.version) < 0 && inRollout(i.id, rel.app_id, rel.rollout_pct),
    );
    return broadcaster.notify(targets, { appId: rel.app_id, version: rel.version });
  }

  r.post('/apps', async (req, res) => {
    const { id, name, publicKey } = req.body || {};
    if (!SLUG.test(id || '')) return res.status(400).json({ error: 'bad_id' });
    if (!name) return res.status(400).json({ error: 'bad_name' });
    if (Buffer.from(publicKey || '', 'base64').length !== 32) return res.status(400).json({ error: 'bad_public_key' });
    try {
      await db.query('INSERT INTO apps (id, name, public_key) VALUES ($1, $2, $3)', [id, name, publicKey]);
    } catch {
      return res.status(409).json({ error: 'app_exists' });
    }
    await log('app_created', id, name);
    res.status(201).json({ ok: true });
  });

  r.get('/apps', async (_req, res) => {
    const { rows } = await db.query('SELECT id, name, owner, created_at FROM apps ORDER BY created_at');
    res.json(rows);
  });

  // Step 1 of publishing: get a presigned PUT so the artifact goes straight to R2.
  r.post('/upload-url', async (req, res) => {
    const { appId, platform, version, filename } = req.body || {};
    if (!SLUG.test(appId || '') || !PLATFORMS.includes(platform) || !isSemver(version)) {
      return res.status(400).json({ error: 'bad_request' });
    }
    const safe = String(filename || 'artifact').replace(/[^A-Za-z0-9._-]/g, '_').slice(0, 100);
    const key = `${appId}/${platform}/${version}/${safe}`;
    res.json({ key, url: await storage.presignPut(key) });
  });

  // Step 2: register the release. The signature must verify against the app's public key.
  r.post('/releases', async (req, res) => {
    const { appId, platform, version, sha256, size, artifactKey, signature } = req.body || {};
    const channel = req.body?.channel || 'stable';
    const rolloutPct = req.body?.rolloutPct ?? 100;
    const minVersion = req.body?.minVersion || null;
    if (!PLATFORMS.includes(platform) || !CHANNELS.includes(channel)) return res.status(400).json({ error: 'bad_request' });
    if (!isSemver(version) || (minVersion && !isSemver(minVersion))) return res.status(400).json({ error: 'bad_version' });
    if (!SHA256.test(sha256 || '') || !(Number(size) > 0) || !artifactKey || !signature) return res.status(400).json({ error: 'bad_request' });
    if (!Number.isInteger(rolloutPct) || rolloutPct < 0 || rolloutPct > 100) return res.status(400).json({ error: 'bad_rollout' });

    const app = (await db.query('SELECT public_key FROM apps WHERE id = $1', [appId])).rows[0];
    if (!app) return res.status(404).json({ error: 'unknown_app' });
    const msg = manifestMessage({ appId, platform, version, sha256, size: Number(size) });
    if (!verifySignature(app.public_key, msg, signature)) return res.status(400).json({ error: 'bad_signature' });

    let rel;
    try {
      rel = (await db.query(
        `INSERT INTO releases (app_id, platform, channel, version, sha256, size, artifact_key, signature, changelog, min_version, rollout_pct)
         VALUES ($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11) RETURNING *`,
        [appId, platform, channel, version, sha256, Number(size), artifactKey, signature, req.body.changelog || '', minVersion, rolloutPct],
      )).rows[0];
    } catch {
      return res.status(409).json({ error: 'release_exists' });
    }
    await log('release_published', appId, `${platform} ${version} ${channel} ${rolloutPct}%`);
    res.status(201).json({ release: rel, broadcast: await broadcast(rel) });
  });

  r.get('/releases', async (req, res) => {
    const { rows } = await db.query(
      'SELECT id, app_id, platform, channel, version, size, rollout_pct, status, created_at FROM releases WHERE ($1::text IS NULL OR app_id = $1) ORDER BY created_at DESC',
      [req.query.appId || null],
    );
    res.json(rows);
  });

  // Widen a staged rollout (or move channel). Raising the percentage re-broadcasts to newly eligible installs.
  r.patch('/releases/:id', async (req, res) => {
    const pct = req.body?.rolloutPct;
    if (!Number.isInteger(pct) || pct < 0 || pct > 100) return res.status(400).json({ error: 'bad_rollout' });
    const { rows } = await db.query(
      `UPDATE releases SET rollout_pct = $2 WHERE id = $1 AND status = 'live' RETURNING *`,
      [Number(req.params.id), pct],
    );
    if (!rows.length) return res.status(404).json({ error: 'not_found' });
    await log('rollout_changed', rows[0].app_id, `${rows[0].version} -> ${pct}%`);
    res.json({ release: rows[0], broadcast: await broadcast(rows[0]) });
  });

  // Stop offering a bad release. Clients never downgrade, so recovery = publish a higher fixed version.
  r.post('/releases/:id/yank', async (req, res) => {
    const { rows } = await db.query(
      `UPDATE releases SET status = 'yanked' WHERE id = $1 RETURNING id, app_id, version, status`,
      [Number(req.params.id)],
    );
    if (!rows.length) return res.status(404).json({ error: 'not_found' });
    await log('release_yanked', rows[0].app_id, rows[0].version);
    res.json({ release: rows[0] });
  });

  return r;
}
