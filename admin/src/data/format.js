const MESSAGES = {
  network: 'Cannot reach the server. Check the URL and that it is running.',
  unauthorized: 'Wrong admin token.',
  bad_rollout: 'Rollout must be a whole number from 0 to 100.',
  bad_id: 'App id must be lowercase letters, digits, dots, dashes or underscores (2 to 64 characters).',
  bad_name: 'Give the app a name.',
  bad_public_key: 'Public key must be the base64 32-byte key printed by the keygen command.',
  app_exists: 'An app with that id already exists.',
  not_found: 'That release no longer exists or is not live.',
  bad_signature: 'The server rejected the signature: the loaded key is not an active key for this app, or the file changed.',
  release_exists: 'That version is already published for this app and platform.',
  unknown_app: 'That app is not registered on the server.',
  bad_version: 'Versions must look like 1.2.3.',
  key_exists: 'That key is already registered for this app.',
  last_key: 'That is the only active key. Add the next key before retiring this one.',
  too_many_keys: 'An app can trust at most 8 keys. Retire an old one first.',
  no_such_active_key: 'That key is not an active key for this app.',
  upload_blocked: 'The browser could not upload to storage. The R2 bucket needs a CORS rule allowing PUT from this site (see the usage guide).',
  upload_failed: 'Storage refused the upload.',
  key_not_json: 'That file is not a Triskelion key file.',
  key_bad_seed: 'That key file has no valid private key in it.',
  key_mismatch: 'That key file is damaged: its public key does not match its private key.',
  no_key_loaded: 'Load your private key first.',
  internal: 'The server hit an error. Check its logs.',
};

export const errorText = (err) => MESSAGES[err?.code] || `Request failed (${err?.code || 'unknown'}).`;

export function formatBytes(n) {
  const v = Number(n);
  if (!Number.isFinite(v) || v < 0) return '-';
  if (v < 1024) return `${v} B`;
  const units = ['KB', 'MB', 'GB'];
  let x = v / 1024;
  let i = 0;
  while (x >= 1024 && i < units.length - 1) { x /= 1024; i++; }
  return `${x.toFixed(x < 10 ? 1 : 0)} ${units[i]}`;
}

export function formatDate(iso) {
  const d = new Date(iso);
  if (Number.isNaN(d.getTime())) return '-';
  return d.toISOString().replace('T', ' ').slice(0, 16) + ' UTC';
}

export function clampPct(value) {
  const n = Math.round(Number(value));
  if (!Number.isFinite(n)) return null;
  return Math.min(100, Math.max(0, n));
}

// installs rows -> [{ app, total, versions: [{ key, platform, channel, version, count, share }] }]
export function groupInstalls(rows) {
  const byApp = new Map();
  for (const r of rows || []) {
    if (!byApp.has(r.app_id)) byApp.set(r.app_id, { app: r.app_id, total: 0, versions: [] });
    const g = byApp.get(r.app_id);
    g.total += r.count;
    g.versions.push({ key: `${r.platform}/${r.channel}/${r.version}`, platform: r.platform, channel: r.channel, version: r.version, count: r.count });
  }
  const groups = [...byApp.values()];
  for (const g of groups) for (const v of g.versions) v.share = g.total ? v.count / g.total : 0;
  return groups.sort((a, b) => a.app.localeCompare(b.app));
}
