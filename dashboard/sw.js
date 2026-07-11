/* EDEN dashboard service worker
 * App shell: cache-first with background refresh.
 * Device endpoints (/api/data, /control, /data): network-only, never cached —
 * a cached sensor snapshot or control ack would silently lie about live state.
 */
const VERSION = "eden-v6";
const SHELL = ["./", "./index.html", "./manifest.webmanifest", "./icon.svg", "./icon-192.png", "./icon-512.png", "./apple-touch-icon.png", "./fonts/inter.woff2", "./fonts/inter-tight.woff2"];

self.addEventListener("install", (e) => {
  e.waitUntil(caches.open(VERSION).then((c) => c.addAll(SHELL)).then(() => self.skipWaiting()));
});

self.addEventListener("activate", (e) => {
  e.waitUntil(
    caches.keys()
      .then((keys) => Promise.all(keys.filter((k) => k !== VERSION).map((k) => caches.delete(k))))
      .then(() => self.clients.claim())
  );
});

self.addEventListener("fetch", (e) => {
  const url = new URL(e.request.url);
  if (e.request.method !== "GET") return;

  // live device traffic: straight to the network, no caching
  if (/^\/(api\/|control\b|data\b|settings\b)/.test(url.pathname)) return;

  // app shell: cache-first, refresh the cache in the background
  e.respondWith(
    caches.match(e.request).then((hit) => {
      const refresh = fetch(e.request)
        .then((res) => {
          if (res && res.ok) {
            const copy = res.clone();
            caches.open(VERSION).then((c) => c.put(e.request, copy));
          }
          return res;
        })
        .catch(() => hit);
      return hit || refresh;
    })
  );
});
