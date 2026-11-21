import pg from 'pg';

// Neon needs TLS; its connection string already carries sslmode=require.
export const createDb = (connectionString) => new pg.Pool({ connectionString, max: 5 });
