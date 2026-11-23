import { useState } from 'react';
import { Ban, Check, RefreshCw } from 'lucide-react';
import { useAsync } from '../data/useAsync.js';
import { clampPct, formatBytes, formatDate } from '../data/format.js';
import { Empty, ErrorBanner, Loading } from '../components/ui.jsx';

function ReleaseRow({ release, api, onChanged, onError }) {
  const [pct, setPct] = useState(String(release.rollout_pct));
  const [busy, setBusy] = useState(false);
  const live = release.status === 'live';
  const next = clampPct(pct);
  const dirty = live && next !== null && next !== release.rollout_pct;

  async function apply() {
    setBusy(true);
    try {
      await api.setRollout(release.id, next);
      onChanged();
    } catch (err) {
      onError(err);
    } finally {
      setBusy(false);
    }
  }

  async function yank() {
    const ok = window.confirm(
      `Stop offering ${release.app_id} ${release.version} (${release.platform})?\n\nInstalls already on it stay on it. Clients never downgrade, so fix it by publishing a higher version.`,
    );
    if (!ok) return;
    setBusy(true);
    try {
      await api.yankRelease(release.id);
      onChanged();
    } catch (err) {
      onError(err);
    } finally {
      setBusy(false);
    }
  }

  return (
    <tr className={live ? '' : 'dim'}>
      <td><code>{release.app_id}</code></td>
      <td>{release.version}</td>
      <td>{release.platform}</td>
      <td>{release.channel}</td>
      <td>{formatBytes(release.size)}</td>
      <td>
        {live ? (
          <span className="rollout">
            <input type="range" min="0" max="100" value={pct} onChange={(e) => setPct(e.target.value)} aria-label="Rollout percent" />
            <input className="pct" type="number" min="0" max="100" value={pct} onChange={(e) => setPct(e.target.value)} />%
            <button className="icon" disabled={!dirty || busy} onClick={apply} aria-label="Apply rollout"><Check size={16} /></button>
          </span>
        ) : (
          '-'
        )}
      </td>
      <td>{live ? 'live' : 'yanked'}</td>
      <td>{formatDate(release.created_at)}</td>
      <td>{live && <button className="danger" disabled={busy} onClick={yank}><Ban size={14} /> Yank</button>}</td>
    </tr>
  );
}

export default function Releases({ api }) {
  const [appFilter, setAppFilter] = useState('');
  const apps = useAsync(() => api.listApps(), [api]);
  const { loading, data, error, reload } = useAsync(() => api.listReleases(appFilter), [api, appFilter]);
  const [rowError, setRowError] = useState(null);

  const changed = () => { setRowError(null); reload(); };

  return (
    <section>
      <header className="page-head">
        <h2>Releases</h2>
        <select value={appFilter} onChange={(e) => setAppFilter(e.target.value)} aria-label="Filter by app">
          <option value="">All apps</option>
          {(apps.data || []).map((a) => <option key={a.id} value={a.id}>{a.id}</option>)}
        </select>
        <button className="icon" onClick={reload} aria-label="Refresh"><RefreshCw size={16} /></button>
      </header>
      <ErrorBanner error={error} onRetry={reload} />
      <ErrorBanner error={rowError} />
      {loading && !data && <Loading />}
      {data && data.length === 0 && <Empty>No releases yet. Publish one with <code>node tools/sign/sign.js publish</code>.</Empty>}
      {data && data.length > 0 && (
        <div className="scroll">
          <table>
            <thead>
              <tr><th>App</th><th>Version</th><th>Platform</th><th>Channel</th><th>Size</th><th>Rollout</th><th>Status</th><th>Published</th><th></th></tr>
            </thead>
            <tbody>
              {data.map((r) => <ReleaseRow key={r.id} release={r} api={api} onChanged={changed} onError={setRowError} />)}
            </tbody>
          </table>
        </div>
      )}
      <p className="hint">Publishing happens in the signing tool, offline. This page controls what is already published.</p>
    </section>
  );
}
