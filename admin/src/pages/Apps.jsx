import { useState } from 'react';
import { Plus } from 'lucide-react';
import { useAsync } from '../data/useAsync.js';
import { formatDate } from '../data/format.js';
import { Empty, ErrorBanner, Loading } from '../components/ui.jsx';

export default function Apps({ api }) {
  const { loading, data, error, reload } = useAsync(() => api.listApps(), [api]);
  const [form, setForm] = useState({ id: '', name: '', publicKey: '' });
  const [busy, setBusy] = useState(false);
  const [formError, setFormError] = useState(null);

  const set = (k) => (e) => setForm((f) => ({ ...f, [k]: e.target.value }));

  async function create(e) {
    e.preventDefault();
    setBusy(true);
    setFormError(null);
    try {
      await api.createApp({ id: form.id.trim(), name: form.name.trim(), publicKey: form.publicKey.trim() });
      setForm({ id: '', name: '', publicKey: '' });
      reload();
    } catch (err) {
      setFormError(err);
    } finally {
      setBusy(false);
    }
  }

  return (
    <section>
      <header className="page-head"><h2>Apps</h2></header>
      <ErrorBanner error={error} onRetry={reload} />
      {loading && !data && <Loading />}
      {data && data.length === 0 && <Empty>No apps yet. Add one below.</Empty>}
      {data && data.length > 0 && (
        <table>
          <thead><tr><th>Id</th><th>Name</th><th>Owner</th><th>Created</th></tr></thead>
          <tbody>
            {data.map((a) => (
              <tr key={a.id}><td><code>{a.id}</code></td><td>{a.name}</td><td>{a.owner}</td><td>{formatDate(a.created_at)}</td></tr>
            ))}
          </tbody>
        </table>
      )}

      <form className="card form" onSubmit={create}>
        <h3>Add an app</h3>
        <label>Id <input value={form.id} onChange={set('id')} placeholder="locked" required /></label>
        <label>Name <input value={form.name} onChange={set('name')} placeholder="Locked" required /></label>
        <label>
          Public key
          <input value={form.publicKey} onChange={set('publicKey')} placeholder="base64 from: node tools/sign/sign.js keygen" required />
        </label>
        <ErrorBanner error={formError} />
        <button className="primary" disabled={busy}><Plus size={16} /> Add app</button>
        <p className="hint">Only the public key goes here. The private key stays offline with the signing tool.</p>
      </form>
    </section>
  );
}
