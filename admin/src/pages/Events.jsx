import { RefreshCw } from 'lucide-react';
import { useAsync } from '../data/useAsync.js';
import { formatDate } from '../data/format.js';
import { Empty, ErrorBanner, Loading } from '../components/ui.jsx';

export default function Events({ api }) {
  const { loading, data, error, reload } = useAsync(() => api.listEvents(100), [api]);

  return (
    <section>
      <header className="page-head">
        <h2>Activity</h2>
        <button className="icon" onClick={reload} aria-label="Refresh"><RefreshCw size={16} /></button>
      </header>
      <ErrorBanner error={error} onRetry={reload} />
      {loading && !data && <Loading />}
      {data && data.length === 0 && <Empty>Nothing has happened yet.</Empty>}
      {data && data.length > 0 && (
        <table>
          <thead><tr><th>When</th><th>Event</th><th>App</th><th>Detail</th></tr></thead>
          <tbody>
            {data.map((e) => (
              <tr key={e.id}><td>{formatDate(e.at)}</td><td>{e.kind.replace('_', ' ')}</td><td>{e.app_id ? <code>{e.app_id}</code> : '-'}</td><td>{e.detail}</td></tr>
            ))}
          </tbody>
        </table>
      )}
    </section>
  );
}
