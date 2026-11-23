import { loadConfig } from '../config/index.js';
import { createDb } from './db.js';
import { createStorage } from './storage.js';
import { createBroadcaster } from './broadcast.js';
import { createApp } from './app.js';
import { createFcmNotifier } from './fcm.js';

const config = loadConfig();
const db = createDb(config.databaseUrl);
const broadcaster = createBroadcaster();
if (config.fcmServiceAccount) {
  broadcaster.addNotifier(createFcmNotifier({ serviceAccount: config.fcmServiceAccount, db }));
  console.log('FCM push enabled');
}
const app = createApp({
  db,
  storage: createStorage(config.r2),
  broadcaster,
  adminToken: config.adminToken,
  corsOrigin: config.corsOrigin,
});

app.listen(config.port, () => console.log(`triskelion listening on :${config.port}`));
