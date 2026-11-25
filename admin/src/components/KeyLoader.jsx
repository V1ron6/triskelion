import { useRef, useState, useSyncExternalStore } from 'react';
import { KeyRound, ShieldOff } from 'lucide-react';
import { keyring } from '../data/signing.js';
import { ErrorBanner } from './ui.jsx';

export function useLoadedKey() {
  return useSyncExternalStore(keyring.subscribe, () => keyring.publicKey(), () => null);
}

// Loads the private key file into memory only. Nothing is stored, and the file picker is cleared right after.
export default function KeyLoader() {
  const publicKey = useLoadedKey();
  const input = useRef(null);
  const [error, setError] = useState(null);

  async function pick(e) {
    setError(null);
    const file = e.target.files?.[0];
    if (!file) return;
    try {
      if (file.size > 4096) throw Object.assign(new Error('key_not_json'), { code: 'key_not_json' });
      keyring.load(await file.text());
    } catch (err) {
      setError(err);
    } finally {
      if (input.current) input.current.value = '';
    }
  }

  return (
    <div className="card keyloader">
      <h3><KeyRound size={16} /> Signing key</h3>
      {publicKey ? (
        <>
          <p>Loaded. Public key:</p>
          <code className="wrap">{publicKey}</code>
          <p className="hint">Held in this tab's memory only. It is wiped when you click below, sign out, close the tab, or after 15 idle minutes. It is never sent to the server.</p>
          <button type="button" onClick={() => keyring.forget()}><ShieldOff size={16} /> Forget key now</button>
        </>
      ) : (
        <>
          <p>Choose your private key file (the <code>.key</code> file from <code>keygen</code>).</p>
          <input ref={input} type="file" accept=".key,.json,application/json,text/plain" onChange={pick} aria-label="Private key file" />
          <p className="hint">The file is read in your browser and kept in memory only. Reload or close the tab and it is gone.</p>
        </>
      )}
      <ErrorBanner error={error} />
    </div>
  );
}
