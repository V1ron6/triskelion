CREATE TABLE IF NOT EXISTS apps (
  id          TEXT PRIMARY KEY,
  name        TEXT NOT NULL,
  owner       TEXT NOT NULL DEFAULT 'self',
  public_key  TEXT NOT NULL,              -- base64 raw Ed25519 public key (32 bytes)
  created_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);

-- Signing keys an app trusts. apps.public_key is the first key; it is copied here on first use so it can be
-- retired like any other. A release is accepted only if signed by an ACTIVE key.
CREATE TABLE IF NOT EXISTS app_keys (
  app_id      TEXT NOT NULL REFERENCES apps(id) ON DELETE CASCADE,
  public_key  TEXT NOT NULL,
  status      TEXT NOT NULL DEFAULT 'active' CHECK (status IN ('active', 'retired')),
  created_at  TIMESTAMPTZ NOT NULL DEFAULT now(),
  retired_at  TIMESTAMPTZ,
  PRIMARY KEY (app_id, public_key)
);

CREATE TABLE IF NOT EXISTS releases (
  id            SERIAL PRIMARY KEY,
  app_id        TEXT NOT NULL REFERENCES apps(id) ON DELETE CASCADE,
  platform      TEXT NOT NULL CHECK (platform IN ('android', 'windows')),
  channel       TEXT NOT NULL DEFAULT 'stable' CHECK (channel IN ('stable', 'beta')),
  version       TEXT NOT NULL,
  sha256        TEXT NOT NULL,            -- hex
  size          BIGINT NOT NULL,
  artifact_key  TEXT NOT NULL,            -- object key in R2
  signature     TEXT NOT NULL,            -- base64 Ed25519 signature of the manifest
  changelog     TEXT NOT NULL DEFAULT '',
  min_version   TEXT,                     -- installs below this must update
  rollout_pct   INTEGER NOT NULL DEFAULT 100 CHECK (rollout_pct BETWEEN 0 AND 100),
  status        TEXT NOT NULL DEFAULT 'live' CHECK (status IN ('live', 'yanked')),
  created_at    TIMESTAMPTZ NOT NULL DEFAULT now(),
  UNIQUE (app_id, platform, version)
);

CREATE TABLE IF NOT EXISTS installs (
  id          TEXT PRIMARY KEY,           -- client-generated UUID
  app_id      TEXT NOT NULL REFERENCES apps(id) ON DELETE CASCADE,
  platform    TEXT NOT NULL CHECK (platform IN ('android', 'windows')),
  channel     TEXT NOT NULL DEFAULT 'stable' CHECK (channel IN ('stable', 'beta')),
  version     TEXT NOT NULL,
  push_token  TEXT,
  last_seen   TIMESTAMPTZ NOT NULL DEFAULT now(),
  created_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS events (
  id      SERIAL PRIMARY KEY,
  at      TIMESTAMPTZ NOT NULL DEFAULT now(),
  kind    TEXT NOT NULL,
  app_id  TEXT,
  detail  TEXT
);

CREATE INDEX IF NOT EXISTS releases_lookup ON releases (app_id, platform, channel, status);
CREATE INDEX IF NOT EXISTS installs_lookup ON installs (app_id, platform, channel);
