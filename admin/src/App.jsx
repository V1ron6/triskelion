import { useMemo, useState } from 'react';
import { Activity, Boxes, LayoutDashboard, LogOut, Package } from 'lucide-react';
import { createApi } from './data/api.js';
import ErrorBoundary from './components/ErrorBoundary.jsx';
import Login from './pages/Login.jsx';
import Overview from './pages/Overview.jsx';
import Apps from './pages/Apps.jsx';
import Releases from './pages/Releases.jsx';
import Events from './pages/Events.jsx';

const PAGES = [
  { id: 'overview', label: 'Overview', icon: LayoutDashboard, Page: Overview },
  { id: 'releases', label: 'Releases', icon: Package, Page: Releases },
  { id: 'apps', label: 'Apps', icon: Boxes, Page: Apps },
  { id: 'events', label: 'Activity', icon: Activity, Page: Events },
];

const KEY = 'triskelion-admin-session';

// sessionStorage: the token lives only as long as this tab. Wrapped because it can be blocked.
function loadSession() {
  try {
    const s = JSON.parse(sessionStorage.getItem(KEY));
    return s?.baseUrl && s?.token ? s : null;
  } catch {
    return null;
  }
}
function saveSession(s) {
  try {
    if (s) sessionStorage.setItem(KEY, JSON.stringify(s));
    else sessionStorage.removeItem(KEY);
  } catch {
    /* works without it, you just sign in again after a reload */
  }
}

export default function App() {
  const [session, setSession] = useState(loadSession);
  const [page, setPage] = useState('overview');
  const api = useMemo(() => (session ? createApi(session) : null), [session]);

  const signIn = (s) => { saveSession(s); setSession(s); };
  const signOut = () => { saveSession(null); setSession(null); };

  if (!api) return <Login initialUrl={import.meta.env?.VITE_SERVER_URL || ''} onSignedIn={signIn} />;

  const { Page } = PAGES.find((p) => p.id === page);
  return (
    <div className="shell">
      <aside>
        <div className="brand"><img src="/logo.svg" alt="" width="28" height="28" /> Triskelion</div>
        <nav>
          {PAGES.map(({ id, label, icon: Icon }) => (
            <button key={id} className={id === page ? 'active' : ''} onClick={() => setPage(id)}>
              <Icon size={16} /> {label}
            </button>
          ))}
        </nav>
        <button className="signout" onClick={signOut}><LogOut size={16} /> Sign out</button>
      </aside>
      <main>
        {/* key resets the boundary when you switch page */}
        <ErrorBoundary key={page}><Page api={api} /></ErrorBoundary>
      </main>
    </div>
  );
}
