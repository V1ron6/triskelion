import { AlertTriangle, Loader2 } from 'lucide-react';
import { errorText } from '../data/format.js';

export function ErrorBanner({ error, onRetry }) {
  if (!error) return null;
  return (
    <div className="banner error" role="alert">
      <AlertTriangle size={16} />
      <span>{errorText(error)}</span>
      {onRetry && <button className="link" onClick={onRetry}>Retry</button>}
    </div>
  );
}

export function Loading({ label = 'Loading' }) {
  return (
    <div className="loading" role="status">
      <Loader2 size={16} className="spin" /> {label}
    </div>
  );
}

export function Empty({ children }) {
  return <p className="empty">{children}</p>;
}
