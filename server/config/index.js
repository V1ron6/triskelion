const required = (name) => {
  const v = process.env[name];
  if (!v) throw new Error(`Missing env var ${name}`);
  return v;
};

function fcmAccount() {
  const raw = process.env.FCM_SERVICE_ACCOUNT_JSON;
  if (!raw) return null;
  try { return JSON.parse(raw); } catch { throw new Error('FCM_SERVICE_ACCOUNT_JSON is not valid JSON'); }
}

export function loadConfig() {
  return {
    port: Number(process.env.PORT) || 3000,
    databaseUrl: required('DATABASE_URL'),
    adminToken: required('ADMIN_TOKEN'),
    r2: {
      accountId: required('R2_ACCOUNT_ID'),
      accessKeyId: required('R2_ACCESS_KEY_ID'),
      secretAccessKey: required('R2_SECRET_ACCESS_KEY'),
      bucket: required('R2_BUCKET'),
    },
    fcmServiceAccount: fcmAccount(), // optional: enables push to backgrounded Android installs
    corsOrigin: process.env.CORS_ORIGIN || '*',
  };
}
