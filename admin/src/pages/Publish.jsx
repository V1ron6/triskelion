import { useEffect, useState } from 'react';
import { Upload } from 'lucide-react';
import { useAsync } from '../data/useAsync.js';
import { clampPct, formatBytes } from '../data/format.js';
import { keyring, manifestMessage, sha256Hex } from '../data/signing.js';
import KeyLoader, { useLoadedKey } from '../components/KeyLoader.jsx';
import { ErrorBanner, Loading } from '../components/ui.jsx';

const SEMVER = /^\d+\.\d+\.\d+$/;
const MAX_BYTES = 500 * 1024 * 1024; // the file is held in memory while hashing

export const safeName = (name) => String(name).replace(/[^A-Za-z0-9._-]/g, '_').slice(0, 100);

export default function Publish({ api }) {
  const apps = useAsync(() => api.listApps(), [api]);
  const loadedKey = useLoadedKey();
  const [form, setForm] = useState({ appId: '', platform: 'android', channel: 'stable', version: '', minVersion: '', rollout: '100', changelog: '' });
  const [file, setFile] = useState(null);
  const [step, setStep] = useState(null);
  const [error, setError] = useState(null);
  const [done, setDone] = useState(null);
  const [keys, setKeys] = useState(null); // active keys of the chosen app

  const set = (k) => (e) => setForm((f) => ({ ...f, [k]: e.target.value }));
  const appId = form.appId || apps.data?.[0]?.id || '';

  useEffect(() => {
    let live = true;
    setKeys(null);
    if (appId) api.listKeys(appId).then((k) => live && setKeys(k.filter((x) => x.status === 'active').map((x) => x.public_key))).catch(() => {});
    return () => { live = false; };
  }, [api, appId]);

  const keyKnown = !loadedKey || !keys || keys.includes(loadedKey);
  const pct = clampPct(form.rollout);
  const problem =
    !appId ? 'Add an app first.' :
    !loadedKey ? 'Load your private key.' :
    !SEMVER.test(form.version) ? 'Version must look like 1.2.3.' :
    form.minVersion && !SEMVER.test(form.minVersion) ? 'Minimum version must look like 1.2.3.' :
    pct === null ? 'Rollout must be 0 to 100.' :
    !file ? 'Choose the file to release.' :
    file.size > MAX_BYTES ? 'File is over 500 MB.' :
    !keyKnown ? 'The loaded key is not an active key for this app.' : null;
  const busy = step !== null;

  async function publish(e) {
    e.preventDefault();
    if (problem || busy) return;
    const summary = `Sign and publish ${appId} ${form.version} for ${form.platform} (${form.channel}), ${pct}% rollout?\n\nFile: ${file.name} (${formatBytes(file.size)})`;
    if (!window.confirm(summary)) return;
    setError(null);
    setDone(null);
    try {
      setStep('Reading and hashing the file');
      const sha256 = await sha256Hex(await file.arrayBuffer());
      const size = file.size;
      setStep('Signing in your browser');
      const signature = keyring.sign(manifestMessage({ appId, platform: form.platform, version: form.version, sha256, size }));
      setStep('Uploading to storage');
      const { key, url } = await api.uploadUrl({ appId, platform: form.platform, version: form.version, filename: safeName(file.name) });
      await api.uploadArtifact(url, file);
      setStep('Publishing');
      const out = await api.publishRelease({
        appId, platform: form.platform, version: form.version, channel: form.channel,
        rolloutPct: pct, minVersion: form.minVersion || undefined, changelog: form.changelog,
        sha256, size, signature, artifactKey: key,
      });
      setDone(out.broadcast || {});
      setFile(null);
    } catch (err) {
      setError(err);
    } finally {
      setStep(null);
    }
  }

  return (
    <section>
      <header className="page-head"><h2>Sign &amp; publish</h2></header>
      <KeyLoader />
      {apps.loading && !apps.data && <Loading />}
      <ErrorBanner error={apps.error} onRetry={apps.reload} />
      {apps.data && apps.data.length === 0 && <p className="empty">No apps yet. Add one on the Apps page first.</p>}
      {apps.data && apps.data.length > 0 && (
        <form className="card form" onSubmit={publish}>
          <h3>New release</h3>
          <label>App
            <select value={appId} onChange={set('appId')}>
              {apps.data.map((a) => <option key={a.id} value={a.id}>{a.name} ({a.id})</option>)}
            </select>
          </label>
          <label>Platform
            <select value={form.platform} onChange={set('platform')}><option>android</option><option>windows</option></select>
          </label>
          <label>Version <input value={form.version} onChange={set('version')} placeholder="1.1.0" required /></label>
          <label>Channel
            <select value={form.channel} onChange={set('channel')}><option>stable</option><option>beta</option></select>
          </label>
          <label>Rollout % <input type="number" min="0" max="100" value={form.rollout} onChange={set('rollout')} /></label>
          <label>Mandatory below version (optional) <input value={form.minVersion} onChange={set('minVersion')} placeholder="1.0.5" /></label>
          <label>Changelog <textarea value={form.changelog} onChange={set('changelog')} rows={3} /></label>
          <label>File (.apk or .exe) <input type="file" onChange={(e) => setFile(e.target.files?.[0] || null)} /></label>
          {loadedKey && !keyKnown && <div className="banner error" role="alert">The loaded key is not registered for this app. Add it under Apps &gt; Keys first, or load the right key.</div>}
          <ErrorBanner error={error} />
          {done && <div className="banner ok" role="status">Published. Pinged {done.targeted ?? 0} install(s), {done.sse ?? 0} connected live.</div>}
          <button className="primary" disabled={!!problem || busy}><Upload size={16} /> {busy ? `${step}...` : 'Sign and publish'}</button>
          {problem && <p className="hint">{problem}</p>}
        </form>
      )}
    </section>
  );
}
