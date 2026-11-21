// Broadcaster: tells installs "a new version exists". It never carries the update itself;
// the app reacts by running its normal pull, which verifies the signature.
//
// Transports are pluggable. SSE is built in (Windows, and Android while foregrounded).
// FCM for backgrounded Android installs plugs in as another notifier (step 4).
export function createBroadcaster() {
  const streams = new Map(); // installId -> Set<res>
  const notifiers = [];      // async ({ install, app, version }) => void

  return {
    addNotifier: (fn) => notifiers.push(fn),

    attach(installId, res) {
      let set = streams.get(installId);
      if (!set) streams.set(installId, (set = new Set()));
      set.add(res);
      res.on('close', () => {
        set.delete(res);
        if (!set.size) streams.delete(installId);
      });
    },

    async notify(installs, { appId, version }) {
      let sse = 0;
      for (const install of installs) {
        const set = streams.get(install.id);
        if (set) {
          for (const res of set) res.write(`event: update\ndata: ${JSON.stringify({ app: appId, version })}\n\n`);
          sse++;
        }
        for (const fn of notifiers) {
          try { await fn({ install, appId, version }); } catch { /* one bad transport must not stop the rest */ }
        }
      }
      return { targeted: installs.length, sse };
    },

    connected: () => streams.size,
  };
}
