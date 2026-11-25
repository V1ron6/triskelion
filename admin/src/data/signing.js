// Browser-side release signing. The private key is read from the file the user picks, held ONLY in the
// closure below (never in React state, storage, cookies or any request), and wiped on forget(), sign-out,
// page hide, or after an idle timeout. It is used to sign locally; the server only ever sees signatures.
import nacl from 'tweetnacl';

export const IDLE_FORGET_MS = 15 * 60 * 1000;

const b64 = (bytes) => {
  let s = '';
  for (const b of bytes) s += String.fromCharCode(b);
  return btoa(s);
};
const unb64 = (str) => {
  try {
    const bin = atob(str);
    return Uint8Array.from(bin, (c) => c.charCodeAt(0));
  } catch {
    return null;
  }
};

export class KeyFileError extends Error {
  constructor(code) {
    super(code);
    this.code = code;
  }
}

// The file written by `sign.js keygen`: { seed, publicKey }, both base64 of 32 bytes.
export function parseKeyFile(text) {
  let j;
  try {
    j = JSON.parse(text);
  } catch {
    throw new KeyFileError('key_not_json');
  }
  const seed = typeof j?.seed === 'string' ? unb64(j.seed) : null;
  if (!seed || seed.length !== 32) throw new KeyFileError('key_bad_seed');
  const pair = nacl.sign.keyPair.fromSeed(seed);
  const publicKey = b64(pair.publicKey);
  if (typeof j.publicKey === 'string' && j.publicKey !== publicKey) throw new KeyFileError('key_mismatch');
  return { secretKey: pair.secretKey, publicKey };
}

// Same message the server and every client rebuild. Must stay byte-identical to server/src/crypto.js.
export const manifestMessage = ({ appId, platform, version, sha256, size }) =>
  `triskelion:v1\n${appId}\n${platform}\n${version}\n${sha256}\n${size}`;

export async function sha256Hex(arrayBuffer) {
  const digest = await crypto.subtle.digest('SHA-256', arrayBuffer);
  return [...new Uint8Array(digest)].map((x) => x.toString(16).padStart(2, '0')).join('');
}

// The holder. `secret` is private to this closure; there is no getter for it.
export function createKeyring({ idleMs = IDLE_FORGET_MS, setTimer = setTimeout, clearTimer = clearTimeout } = {}) {
  let secret = null;
  let publicKey = null;
  let timer = null;
  const listeners = new Set();
  const emit = () => listeners.forEach((fn) => fn());

  const arm = () => {
    clearTimer(timer);
    timer = setTimer(() => ring.forget(), idleMs);
  };

  const ring = {
    load(text) {
      const k = parseKeyFile(text); // throws before anything is stored
      secret = k.secretKey;
      publicKey = k.publicKey;
      arm();
      emit();
      return publicKey;
    },
    forget() {
      if (secret) secret.fill(0); // best effort: overwrite before dropping the reference
      secret = null;
      publicKey = null;
      clearTimer(timer);
      timer = null;
      emit();
    },
    loaded: () => secret !== null,
    publicKey: () => publicKey,
    // Signs a UTF-8 message, returns base64. Using the key counts as activity.
    sign(message) {
      if (!secret) throw new KeyFileError('no_key_loaded');
      arm();
      return b64(nacl.sign.detached(new TextEncoder().encode(message), secret));
    },
    subscribe(fn) {
      listeners.add(fn);
      return () => listeners.delete(fn);
    },
  };
  return ring;
}

// One keyring for the whole page.
export const keyring = createKeyring();
if (typeof window !== 'undefined') window.addEventListener('pagehide', () => keyring.forget());
