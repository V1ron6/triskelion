import { useState } from 'react';
import { KeyRound } from 'lucide-react';
import { createApi } from '../data/api.js';
import { ErrorBanner } from '../components/ui.jsx';

export default function Login({ initialUrl, onSignedIn }) {
  const [baseUrl, setBaseUrl] = useState(initialUrl || '');
  const [token, setToken] = useState('');
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState(null);

  async function submit(e) {
    e.preventDefault();
    setBusy(true);
    setError(null);
    try {
      // Any admin call proves both the URL and the token.
      await createApi({ baseUrl, token }).listApps();
      onSignedIn({ baseUrl: baseUrl.trim(), token });
    } catch (err) {
      setError(err.status === 401 ? { code: 'unauthorized' } : err);
    } finally {
      setBusy(false);
    }
  }

  return (
    <main className="login">
      <img src="/logo.svg" alt="" width="72" height="72" />
      <h1>Triskelion</h1>
      <form onSubmit={submit}>
        <label>
          Server URL
          <input value={baseUrl} onChange={(e) => setBaseUrl(e.target.value)} placeholder="https://your-service.onrender.com" required />
        </label>
        <label>
          Admin token
          <input type="password" value={token} onChange={(e) => setToken(e.target.value)} autoComplete="current-password" required />
        </label>
        <ErrorBanner error={error} />
        <button className="primary" disabled={busy}>
          <KeyRound size={16} /> {busy ? 'Checking' : 'Sign in'}
        </button>
      </form>
      <p className="hint">The token stays in this browser tab only and is sent to your server, nowhere else.</p>
    </main>
  );
}
