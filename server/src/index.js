import { loadConfig } from './config.js';
import { createDb } from './db.js';
import { createStorage } from './storage.js';
import { createBroadcaster } from './broadcast.js';
import { createApp } from './app.js';

const config = loadConfig();
const app = createApp({
  db: createDb(config.databaseUrl),
  storage: createStorage(config.r2),
  broadcaster: createBroadcaster(),
  adminToken: config.adminToken,
  corsOrigin: config.corsOrigin,
});

app.listen(config.port, () => console.log(`triskelion listening on :${config.port}`));
