// The admin model: every call to the Triskelion server lives here. Pages never touch fetch.
// Publishing is deliberately NOT here: releases are signed offline with tools/sign, so the private
// key never sits in a browser.

export class ApiError extends Error {
  constructor(status, code) {
    super(code);
    this.status = status; // 0 = could not reach the server
    this.code = code;
  }
}

export function createApi({ baseUrl, token, fetchImpl = (...args) => fetch(...args) }) {
  const base = String(baseUrl || '').replace(/\/+$/, '');

  async function request(method, path, body) {
    let res;
    try {
      res = await fetchImpl(base + path, {
        method,
        headers: { authorization: `Bearer ${token}`, ...(body ? { 'content-type': 'application/json' } : {}) },
        body: body ? JSON.stringify(body) : undefined,
      });
    } catch {
      throw new ApiError(0, 'network');
    }
    const data = await res.json().catch(() => null);
    if (!res.ok) throw new ApiError(res.status, data?.error || `http_${res.status}`);
    return data;
  }

  return {
    listApps: () => request('GET', '/admin/apps'),
    createApp: ({ id, name, publicKey }) => request('POST', '/admin/apps', { id, name, publicKey }),
    listReleases: (appId) => request('GET', `/admin/releases${appId ? `?appId=${encodeURIComponent(appId)}` : ''}`),
    setRollout: (id, rolloutPct) => request('PATCH', `/admin/releases/${Number(id)}`, { rolloutPct }),
    yankRelease: (id) => request('POST', `/admin/releases/${Number(id)}/yank`),
    listInstalls: () => request('GET', '/admin/installs'),
    listEvents: (limit = 50) => request('GET', `/admin/events?limit=${Number(limit)}`),
  };
}
