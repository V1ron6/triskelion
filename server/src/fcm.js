// FCM notifier: wakes backgrounded Android installs. Plugs into the broadcaster with addNotifier.
// The message carries no update, only "something exists"; the app then runs its normal signed pull.
// Uses a Google service account directly (RS256 JWT -> OAuth token -> HTTP v1), no firebase-admin dependency.
import { createSign } from 'node:crypto';

const SCOPE = 'https://www.googleapis.com/auth/firebase.messaging';
const b64url = (b) => Buffer.from(b).toString('base64url');

export function createFcmNotifier({ serviceAccount, db, fetchImpl = fetch, now = () => Date.now() }) {
  const { client_email, private_key, project_id } = serviceAccount;
  if (!client_email || !private_key || !project_id) throw new Error('service account needs client_email, private_key, project_id');
  const tokenUri = serviceAccount.token_uri || 'https://oauth2.googleapis.com/token';
  let cached = null; // { token, expiresAt }

  async function accessToken() {
    if (cached && cached.expiresAt - now() > 60_000) return cached.token;
    const iat = Math.floor(now() / 1000);
    const head = b64url(JSON.stringify({ alg: 'RS256', typ: 'JWT' }));
    const claims = b64url(JSON.stringify({ iss: client_email, scope: SCOPE, aud: tokenUri, iat, exp: iat + 3600 }));
    const sig = createSign('RSA-SHA256').update(`${head}.${claims}`).sign(private_key).toString('base64url');
    const res = await fetchImpl(tokenUri, {
      method: 'POST',
      headers: { 'content-type': 'application/x-www-form-urlencoded' },
      body: new URLSearchParams({ grant_type: 'urn:ietf:params:oauth:grant-type:jwt-bearer', assertion: `${head}.${claims}.${sig}` }),
    });
    if (!res.ok) throw new Error(`fcm auth failed: http ${res.status}`);
    const j = await res.json();
    cached = { token: j.access_token, expiresAt: now() + (j.expires_in || 3600) * 1000 };
    return cached.token;
  }

  return async function notify({ install, appId, version }) {
    if (!install.push_token) return;
    const send = async (token) => fetchImpl(`https://fcm.googleapis.com/v1/projects/${project_id}/messages:send`, {
      method: 'POST',
      headers: { authorization: `Bearer ${token}`, 'content-type': 'application/json' },
      body: JSON.stringify({
        message: {
          token: install.push_token,
          data: { triskelion: 'update', app: appId, version },
          android: { priority: 'HIGH' },
        },
      }),
    });
    let res = await send(await accessToken());
    if (res.status === 401) { cached = null; res = await send(await accessToken()); }
    if (res.ok) return;
    const body = await res.json().catch(() => ({}));
    const code = body?.error?.details?.find?.((d) => d.errorCode)?.errorCode;
    // A token that FCM says is gone is useless forever: forget it so we stop sending to it.
    if (res.status === 404 || code === 'UNREGISTERED') {
      await db.query('UPDATE installs SET push_token = NULL WHERE id = $1', [install.id]);
      return;
    }
    throw new Error(`fcm send failed: http ${res.status}`);
  };
}
