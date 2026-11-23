import { describe, expect, it } from 'vitest';
import { ApiError, createApi } from '../src/data/api.js';
import { clampPct, errorText, formatBytes, formatDate, groupInstalls } from '../src/data/format.js';

const reply = (status, body) => async () => ({ ok: status >= 200 && status < 300, status, json: async () => body });

describe('api', () => {
  it('sends the bearer token and normalizes the base url', async () => {
    let seen;
    const api = createApi({ baseUrl: 'https://x.example///', token: 'tok', fetchImpl: async (url, init) => { seen = { url, init }; return { ok: true, json: async () => [] }; } });
    await api.listApps();
    expect(seen.url).toBe('https://x.example/admin/apps');
    expect(seen.init.method).toBe('GET');
    expect(seen.init.headers.authorization).toBe('Bearer tok');
    expect(seen.init.body).toBeUndefined();
  });

  it('builds the right request for each call', async () => {
    const calls = [];
    const api = createApi({ baseUrl: 'https://x', token: 't', fetchImpl: async (url, init) => { calls.push([init.method, url, init.body]); return { ok: true, json: async () => ({}) }; } });
    await api.createApp({ id: 'locked', name: 'Locked', publicKey: 'k' });
    await api.listReleases('locked');
    await api.listReleases('');
    await api.listReleases('a b&c');
    await api.setRollout('7', 40);
    await api.yankRelease(7);
    await api.listInstalls();
    await api.listEvents(10);
    expect(calls).toEqual([
      ['POST', 'https://x/admin/apps', '{"id":"locked","name":"Locked","publicKey":"k"}'],
      ['GET', 'https://x/admin/releases?appId=locked', undefined],
      ['GET', 'https://x/admin/releases', undefined],
      ['GET', 'https://x/admin/releases?appId=a%20b%26c', undefined],
      ['PATCH', 'https://x/admin/releases/7', '{"rolloutPct":40}'],
      ['POST', 'https://x/admin/releases/7/yank', undefined],
      ['GET', 'https://x/admin/installs', undefined],
      ['GET', 'https://x/admin/events?limit=10', undefined],
    ]);
  });

  it('turns server errors, bad json and network failures into ApiError', async () => {
    const mk = (fetchImpl) => createApi({ baseUrl: 'https://x', token: 't', fetchImpl });
    await expect(mk(reply(401, { error: 'unauthorized' })).listApps()).rejects.toMatchObject({ status: 401, code: 'unauthorized' });
    await expect(mk(reply(409, { error: 'app_exists' })).createApp({})).rejects.toMatchObject({ status: 409, code: 'app_exists' });
    await expect(mk(reply(502, null)).listApps()).rejects.toMatchObject({ status: 502, code: 'http_502' });
    await expect(mk(async () => { throw new TypeError('failed to fetch'); }).listApps()).rejects.toMatchObject({ status: 0, code: 'network' });
    await expect(mk(async () => ({ ok: false, status: 500, json: async () => { throw new Error('not json'); } })).listApps()).rejects.toBeInstanceOf(ApiError);
  });
});

describe('format', () => {
  it('maps known error codes to readable text and falls back safely', () => {
    expect(errorText({ code: 'network' })).toMatch(/Cannot reach/);
    expect(errorText({ code: 'app_exists' })).toMatch(/already exists/);
    expect(errorText({ code: 'weird' })).toBe('Request failed (weird).');
    expect(errorText(undefined)).toBe('Request failed (unknown).');
  });

  it('formats sizes and dates, including garbage', () => {
    expect(formatBytes(0)).toBe('0 B');
    expect(formatBytes(1023)).toBe('1023 B');
    expect(formatBytes(1536)).toBe('1.5 KB');
    expect(formatBytes('5242880')).toBe('5.0 MB');
    expect(formatBytes(15 * 1024 * 1024 * 1024)).toBe('15 GB');
    expect(formatBytes('nope')).toBe('-');
    expect(formatBytes(-1)).toBe('-');
    expect(formatDate('2026-11-21T16:35:12.000Z')).toBe('2026-11-21 16:35 UTC');
    expect(formatDate('not a date')).toBe('-');
  });

  it('clamps rollout input', () => {
    expect(clampPct('40')).toBe(40);
    expect(clampPct('40.6')).toBe(41);
    expect(clampPct(-5)).toBe(0);
    expect(clampPct(250)).toBe(100);
    expect(clampPct('')).toBe(0);     // Number('') is 0, a blank field means 0%
    expect(clampPct('abc')).toBeNull();
  });

  it('groups installs per app with shares that add up', () => {
    const rows = [
      { app_id: 'locked', platform: 'android', channel: 'stable', version: '1.0.0', count: 3 },
      { app_id: 'locked', platform: 'android', channel: 'stable', version: '1.1.0', count: 1 },
      { app_id: 'hollow', platform: 'windows', channel: 'beta', version: '2.0.0', count: 2 },
    ];
    const g = groupInstalls(rows);
    expect(g.map((x) => [x.app, x.total])).toEqual([['hollow', 2], ['locked', 4]]);
    expect(g[1].versions.map((v) => v.share)).toEqual([0.75, 0.25]);
    expect(groupInstalls(undefined)).toEqual([]);
    expect(groupInstalls([])).toEqual([]);
  });
});
