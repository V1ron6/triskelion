import { useState } from 'react';
import { Plus, Trash2 } from 'lucide-react';
import { useAsync } from '../data/useAsync.js';
import { formatDate } from '../data/format.js';
import { useLoadedKey } from './KeyLoader.jsx';
import { ErrorBanner, Loading } from './ui.jsx';

// Rotation: add the NEXT key, ship an app build that trusts both, publish with the new key, then retire the old one.
export default function KeysPanel({ api, appId }) {
  const { loading, data, error, reload } = useAsync(() => api.listKeys(appId), [api, appId]);
  const loaded = useLoadedKey();
  const [pub, setPub] = useState('');
  const [busy, setBusy] = useState(false);
  const [err, setErr] = useState(null);

  async function run(fn) {
    setBusy(true);
    setErr(null);
    try {
      await fn();
      reload();
    } catch (e) {
      setErr(e);
    } finally {
      setBusy(false);
    }
  }

  const active = (data || []).filter((k) => k.status === 'active');
  const embed = active.map((k) => k.public_key).join(',');

  return (
    <div className="card keys">
      <h4>Signing keys for <code>{appId}</code></h4>
      {loading && !data && <Loading />}
      <ErrorBanner error={error} onRetry={reload} />
      {data && (
        <table>
          <thead><tr><th>Public key</th><th>Status</th><th>Added</th><th /></tr></thead>
          <tbody>
            {data.map((k) => (
              <tr key={k.public_key} className={k.status === 'active' ? '' : 'dim'}>
                <td><code className="wrap">{k.public_key}</code>{k.public_key === loaded ? ' (loaded)' : ''}</td>
                <td>{k.status}</td>
                <td>{formatDate(k.created_at)}</td>
                <td>
                  {k.status === 'active' && (
                    <button className="icon" disabled={busy} aria-label="Retire key" onClick={() => {
                      if (window.confirm('Retire this key? New releases signed with it will be refused. Apps that only trust this key can no longer be updated.')) run(() => api.retireKey(appId, k.public_key));
                    }}><Trash2 size={16} /></button>
                  )}
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      )}
      <div className="row">
        <input value={pub} onChange={(e) => setPub(e.target.value)} placeholder="public key of the next signing key" aria-label="New public key" />
        {loaded && <button type="button" onClick={() => setPub(loaded)}>Use loaded key</button>}
        <button className="primary" disabled={busy || !pub.trim()} onClick={() => run(async () => { await api.addKey(appId, pub.trim()); setPub(''); })}><Plus size={16} /> Add key</button>
      </div>
      <ErrorBanner error={err} />
      {active.length > 0 && (
        <p className="hint">Embed in the app build (comma-separated, one value): <code className="wrap">{embed}</code></p>
      )}
    </div>
  );
}
