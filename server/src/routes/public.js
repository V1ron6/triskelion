import { Router } from 'express';
import { compareVersions, inRollout, isSemver } from '../crypto.js';

const PLATFORMS = ['android', 'windows'];
const CHANNELS = ['stable', 'beta'];
const UUID = /^[0-9a-f-]{16,64}$/i;

// Tiny in-memory limiter: public endpoints take unauthenticated input.
function limiter({ windowMs, max }) {
  const hits = new Map();
  setInterval(() => hits.clear(), windowMs).unref();
  return (req, res, next) => {
    const n = (hits.get(req.ip) || 0) + 1;
    hits.set(req.ip, n);
    if (n > max) return res.status(429).json({ error: 'rate_limited' });
    next();
  };
}

export function publicRoutes({ db, storage, broadcaster }) {
  const r = Router();
  const limit = limiter({ windowMs: 60_000, max: 120 });

  // Register (or refresh) an install and its push token.
  r.post('/register', limit, async (req, res) => {
    const { install, app, platform, version, channel = 'stable', pushToken = null } = req.body || {};
    if (!UUID.test(install || '')) return res.status(400).json({ error: 'bad_install' });
    if (!PLATFORMS.includes(platform)) return res.status(400).json({ error: 'bad_platform' });
    if (!CHANNELS.includes(channel)) return res.status(400).json({ error: 'bad_channel' });
    if (!isSemver(version)) return res.status(400).json({ error: 'bad_version' });
    const { rows } = await db.query('SELECT 1 FROM apps WHERE id = $1', [app]);
    if (!rows.length) return res.status(404).json({ error: 'unknown_app' });

    await db.query(
      `INSERT INTO installs (id, app_id, platform, channel, version, push_token)
       VALUES ($1, $2, $3, $4, $5, $6)
       ON CONFLICT (id) DO UPDATE
         SET platform = $3, channel = $4, version = $5,
             push_token = COALESCE($6, installs.push_token), last_seen = now()`,
      [install, app, platform, channel, version, pushToken],
    );
    res.json({ ok: true });
  });

  // The pull path. Also what an app runs after a broadcast.
  r.get('/check', limit, async (req, res) => {
    const { app, platform, version, install, channel = 'stable' } = req.query;
    if (!PLATFORMS.includes(platform)) return res.status(400).json({ error: 'bad_platform' });
    if (!CHANNELS.includes(channel)) return res.status(400).json({ error: 'bad_channel' });
    if (!isSemver(version)) return res.status(400).json({ error: 'bad_version' });
    if (!UUID.test(install || '')) return res.status(400).json({ error: 'bad_install' });

    const { rows } = await db.query(
      `SELECT * FROM releases
       WHERE app_id = $1 AND platform = $2 AND status = 'live'
         AND ($3 = 'beta' OR channel = 'stable')`,
      [app, platform, channel],
    );
    const latest = rows
      .filter((x) => compareVersions(x.version, version) > 0 && inRollout(install, app, x.rollout_pct))
      .sort((a, b) => compareVersions(b.version, a.version))[0];

    await db.query('UPDATE installs SET last_seen = now(), version = $2 WHERE id = $1', [install, version]);

    if (!latest) return res.json({ update: false });
    res.json({
      update: true,
      version: latest.version,
      sha256: latest.sha256,
      size: Number(latest.size),
      signature: latest.signature,
      changelog: latest.changelog,
      mandatory: !!latest.min_version && compareVersions(version, latest.min_version) < 0,
      url: await storage.presignGet(latest.artifact_key),
    });
  });

  // SSE stream: the server pings, the app pulls.
  r.get('/stream', limit, (req, res) => {
    const { install } = req.query;
    if (!UUID.test(install || '')) return res.status(400).json({ error: 'bad_install' });
    res.set({ 'Content-Type': 'text/event-stream', 'Cache-Control': 'no-cache', Connection: 'keep-alive' });
    res.flushHeaders();
    res.write('event: ready\ndata: {}\n\n');
    const beat = setInterval(() => res.write(': keepalive\n\n'), 25_000);
    res.on('close', () => clearInterval(beat));
    broadcaster.attach(install, res);
  });

  return r;
}
