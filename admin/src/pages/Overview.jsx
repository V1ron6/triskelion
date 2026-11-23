import { RefreshCw } from 'lucide-react';
import { useAsync } from '../data/useAsync.js';
import { formatDate, groupInstalls } from '../data/format.js';
import { Empty, ErrorBanner, Loading } from '../components/ui.jsx';

export default function Overview({ api }) {
  const { loading, data, error, reload } = useAsync(() => api.listInstalls(), [api]);
  const groups = groupInstalls(data);

  return (
    <section>
      <header className="page-head">
        <h2>Overview</h2>
        <button className="icon" onClick={reload} aria-label="Refresh"><RefreshCw size={16} /></button>
      </header>
      <ErrorBanner error={error} onRetry={reload} />
      {loading && !data && <Loading />}
      {data && groups.length === 0 && <Empty>No installs have registered yet. They appear once an app starts the client.</Empty>}
      {groups.map((g) => (
        <article className="card" key={g.app}>
          <h3>{g.app} <span className="muted">{g.total} install{g.total === 1 ? '' : 's'}</span></h3>
          <ul className="bars">
            {g.versions.map((v) => (
              <li key={v.key}>
                <span className="label">{v.version} <span className="muted">{v.platform}{v.channel !== 'stable' ? ` / ${v.channel}` : ''}</span></span>
                <span className="bar"><span style={{ width: `${Math.max(2, Math.round(v.share * 100))}%` }} /></span>
                <span className="count">{v.count}</span>
              </li>
            ))}
          </ul>
        </article>
      ))}
      {data && data.length > 0 && (
        <p className="hint">Last activity {formatDate(data.reduce((a, r) => (r.last_seen > a ? r.last_seen : a), ''))}</p>
      )}
    </section>
  );
}
