import express from 'express';
import cors from 'cors';
import { publicRoutes } from './routes/public.js';
import { adminRoutes } from './routes/admin.js';

// Dependencies are injected so tests can run against in-memory Postgres and fake storage.
export function createApp({ db, storage, broadcaster, adminToken, corsOrigin = '*' }) {
  const app = express();
  app.set('trust proxy', 1); // behind Render's proxy: req.ip is the real client
  app.use(cors({ origin: corsOrigin }));
  app.use(express.json({ limit: '64kb' }));

  app.get('/health', (_req, res) => res.json({ ok: true }));
  app.use('/v1', publicRoutes({ db, storage, broadcaster }));
  app.use('/admin', adminRoutes({ db, storage, broadcaster, adminToken }));

  // Express 5 forwards rejected async handlers here, so no per-route try/catch is needed.
  app.use((err, _req, res, _next) => {
    console.error(err);
    res.status(500).json({ error: 'internal' });
  });
  return app;
}
