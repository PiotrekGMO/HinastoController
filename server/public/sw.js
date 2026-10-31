// Service worker of the Hinasto panel. It does NOT cache anything: the panel shows live data and a cache would show
// stale controls. It exists so that the page can be installed as an app and shows a short page instead of the browser's
// error when the server cannot be reached. The page registers it as sw.js?l=<language> (see app.js).
const TEXTS = {
  pl: ['Brak połączenia z serwerem', 'Sprawdź internet i spróbuj ponownie.', 'Spróbuj ponownie'],
  en: ['Cannot reach the server', 'Check your internet connection and try again.', 'Try again'],
  de: ['Keine Verbindung zum Server', 'Prüfe die Internetverbindung und versuche es erneut.', 'Erneut versuchen'],
  cs: ['Server není dostupný', 'Zkontrolujte připojení k internetu a zkuste to znovu.', 'Zkusit znovu'],
  sk: ['Server nie je dostupný', 'Skontrolujte pripojenie na internet a skúste to znova.', 'Skúsiť znova'],
};
const lang = new URLSearchParams(self.location.search).get('l');
const [TITLE, TEXT, RETRY] = TEXTS[lang] || TEXTS.en;
const OFFLINE = `<!doctype html><html lang="${TEXTS[lang] ? lang : 'en'}"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">` +
  '<title>Hinasto</title><style>body{font:16px system-ui,sans-serif;display:grid;place-items:center;min-height:100vh;margin:0;background:#0d1116;color:#e8ecf0;text-align:center}' +
  'button{margin-top:16px;padding:12px 22px;border:0;border-radius:10px;background:#ff7a2e;color:#1a1204;font:600 16px system-ui;cursor:pointer}p{color:#9fb0bf;margin:6px 0 0}</style></head>' +
  `<body><main><h1>${TITLE}</h1><p>${TEXT}</p><button onclick="location.reload()">${RETRY}</button></main></body></html>`;

self.addEventListener('install', () => self.skipWaiting());
self.addEventListener('activate', (event) => event.waitUntil(self.clients.claim()));
self.addEventListener('fetch', (event) => {
  if (event.request.mode !== 'navigate') return;  // only page loads; scripts, styles and the API always go to the network
  event.respondWith(fetch(event.request).catch(() => new Response(OFFLINE, {status: 503, headers: {'Content-Type': 'text/html; charset=utf-8'}})));
});
