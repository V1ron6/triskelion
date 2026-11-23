import { useCallback, useEffect, useRef, useState } from 'react';

// Runs an async loader on mount and on reload(). A failure becomes `error` and never throws into the
// tree, so one broken page cannot take the whole panel down.
export function useAsync(loader, deps = []) {
  const [state, setState] = useState({ loading: true, data: null, error: null });
  const alive = useRef(true);
  const run = useCallback(() => {
    setState((s) => ({ ...s, loading: true, error: null }));
    Promise.resolve()
      .then(loader)
      .then((data) => alive.current && setState({ loading: false, data, error: null }))
      .catch((error) => alive.current && setState({ loading: false, data: null, error }));
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, deps);

  useEffect(() => {
    alive.current = true;
    run();
    return () => { alive.current = false; };
  }, [run]);

  return { ...state, reload: run };
}
