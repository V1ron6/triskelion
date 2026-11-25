// The admin model: every call to the Triskelion server lives here. Pages never touch fetch.
// Signing happens in data/signing.js and only SIGNATURES come here: the private key is never sent anywhere.

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
    listKeys: (appId) => request('GET', `/admin/apps/${encodeURIComponent(appId)}/keys`),
    addKey: (appId, publicKey) => request('POST', `/admin/apps/${encodeURIComponent(appId)}/keys`, { publicKey }),
    retireKey: (appId, publicKey) => request('POST', `/admin/apps/${encodeURIComponent(appId)}/keys/retire`, { publicKey }),
    uploadUrl: ({ appId, platform, version, filename }) => request('POST', '/admin/upload-url', { appId, platform, version, filename }),
    publishRelease: (release) => request('POST', '/admin/releases', release),
    // Straight to storage with the presigned URL (no admin token goes there). The bucket needs a CORS rule for PUT.
    async uploadArtifact(url, body) {
      let res;
      try {
        res = await fetchImpl(url, { method: 'PUT', body });
      } catch {
        throw new ApiError(0, 'upload_blocked');
      }
      if (!res.ok) throw new ApiError(res.status, 'upload_failed');
    },
    listInstalls: () => request('GET', '/admin/installs'),
    listEvents: (limit = 50) => request('GET', `/admin/events?limit=${Number(limit)}`),
  };
}
