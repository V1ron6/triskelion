import { describe, expect, it } from 'vitest';
import { renderToString } from 'react-dom/server';
import App from '../src/App.jsx';
import Login from '../src/pages/Login.jsx';
import Overview from '../src/pages/Overview.jsx';
import Apps from '../src/pages/Apps.jsx';
import Releases from '../src/pages/Releases.jsx';
import Events from '../src/pages/Events.jsx';
import { ErrorBanner } from '../src/components/ui.jsx';

// Server rendering runs the first render of each page (no effects), which proves imports, JSX and the
// initial branch work. Interactive behavior is covered by the data tests and by using the panel.
const never = () => new Promise(() => {});
const api = { listApps: never, createApp: never, listReleases: never, setRollout: never, yankRelease: never, listInstalls: never, listEvents: never };

describe('pages render', () => {
  it('signed-out app shows the login', () => {
    const html = renderToString(<App />);
    expect(html).toContain('Admin token');
    expect(html).toContain('Server URL');
  });

  it('login', () => {
    expect(renderToString(<Login initialUrl="https://x.example" onSignedIn={() => {}} />)).toContain('value="https://x.example"');
  });

  it('each page shows its loading state first', () => {
    for (const [name, Page] of Object.entries({ Overview, Apps, Releases, Events })) {
      const html = renderToString(<Page api={api} />);
      expect(html, name).toContain('Loading');
    }
  });

  it('apps page has the add form with the offline-key reminder', () => {
    const html = renderToString(<Apps api={api} />);
    expect(html).toContain('Add an app');
    expect(html).toContain('private key stays offline');
  });

  it('error banner renders readable text', () => {
    expect(renderToString(<ErrorBanner error={{ code: 'unauthorized' }} />)).toContain('Wrong admin token.');
    expect(renderToString(<ErrorBanner error={null} />)).toBe('');
  });
});
