const required = (name) => {
  const v = process.env[name];
  if (!v) throw new Error(`Missing env var ${name}`);
  return v;
};

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
    corsOrigin: process.env.CORS_ORIGIN || '*',
  };
}
