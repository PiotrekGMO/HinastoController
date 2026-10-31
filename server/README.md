# Hinasto Controller: server and panel

The control panel for the heater controllers (ESP32 + CC1101). Controllers have no web page of their own: they call this
server, send their state and fetch commands. The panel is a web page (installable as an app), the controllers talk to
`/api/`. PHP 8.1+ with `pdo_mysql`, MySQL or MariaDB. No framework, no Composer: a small MVC core in `app/Core`.

## Install

1. Point the document root of a (sub)domain at `public/` (HTTPS only: the panel session and the controller secret travel in headers).
   Apache needs `mod_rewrite` (see `.htaccess`); on nginx send every request that is not a file to `public/index.php`.
2. Open the site in a browser. Until the installer has run, every page redirects to `/setup`.
3. The installer asks for the **language** (Polish, English, German, Czech or Slovak; chosen once, there is no switcher in the
   panel), the database (host, name, user, password, **table prefix** for a shared database), optionally a MySQL admin
   account to create the database and user, the panel admin password and a time zone. It creates the tables, writes
   `storage/config.php` and shows the **enrollment key** for the controllers once.
4. After that `/setup` returns 404. To install again, delete `storage/config.php`.

Command line instead of the browser: `php tools/setup.php`. Change the admin password later: `php tools/hash.php 'new password'`
and put the output into `admin_hash` in `storage/config.php`. Change the language later: set `'lang'` in `storage/config.php`
(`pl`, `en`, `de`, `cs`, `sk`).

`storage/` must be writable by the web server during the installation. Keep `app/`, `config/`, `database/` and `tools/` out
of the web root (each also has a deny-all `.htaccess` as a second line of defence).

## Languages

The panel, the installer and the messages of the API are translated; the language is stored in `storage/config.php`
(`lang`, default `pl` for installs made before languages existed). Texts live in `app/Lang/<code>.php` as flat
`key => text` maps with `{name}` placeholders; English is the fallback for a missing key. Keys that start with `js.` are
sent to the browser (a `<script type="application/json" id="i18n">` in the layout) and used by `public/assets/app.js` and
`analytics.js` through `T('key', {param})`. Views use `$t('key')` (escaped) and `$th('key')` (the text itself may hold markup).
To add a language: copy `app/Lang/en.php`, translate it, add the code to `I18n::LANGS` / `LOCALES` / `defaultTimezone()`
and to the offline texts in `public/sw.js`.

The language of a **controller** is the language of its firmware: every build is made for one language (see
`../docs/firmware.md`) and the server keeps one release line per language.

## Deploy with the scripts

`deploy_test.cmd` (needs Docker Desktop) checks the syntax of every PHP file, packs the site and uploads it over FTPS or SFTP
(`docker/deploy.sh`: upload to `<dir>.new`, check every file size, resume truncated files, swap directories).
`deploy_test.cmd test` only checks the connection. Login and password are in `deploy_test.env` (made from
`deploy_test.env.example`, never committed). `KEEP=storage` keeps the server's `storage/` (it holds `config.php`
written by the installer) over every swap. Copy the two files to `deploy_production.*` for another target.

- The app also runs under a sub path (for example `https://example.com/hinasto/public/`); on a domain whose document root is
  `public/` it runs at the root with no change.
- `storage/` must be writable by the web server user (PHP often runs as another user than the FTP login). `docker/deploy.sh`
  sets it and the installer shows a check list at the top (PHP version, `pdo_mysql`, write access to `storage/`). If the folder
  is still not writable, the installer shows the text of `storage/config.php` to save by hand.
- Apache with `.htaccess` blocks `config/`, `storage/`, `app/`, `database/`, `tools/` (403) and rewrites everything that is not a
  file to `public/index.php`.

## Security notes

- **Sessions**: 30 days sliding, but never longer than 90 days in total, and a session ends when dmin_hash changes (so changing the admin password logs everybody out).
- **Login throttle**: 8 wrong passwords per 15 minutes per address (an IPv6 address counts by its /64). Behind a reverse proxy or CDN every visitor has the proxy's address: set 'trusted_proxy' => true in storage/config.php to use the last entry of X-Forwarded-For (never without a proxy: the header can be forged).
- **Sync robustness**: acknowledgements and commands are handled before the telemetry is stored, and a failure while storing telemetry is logged and never keeps a command (power off included) from reaching the controller. Texts from controllers are cut on character boundaries and numbers are limited to what the columns take.
- **Permissions**: the deploy script makes storage/ and public/api/v1 mode 777 by default because PHP often runs as another user than the FTP login. Where it does not, set STORAGE_MODE=775 in deploy_<target>.env. public/api/v1/.htaccess forbids running anything as PHP there.
- **Firmware images are public** (static copies in public/api/v1) and **not signed**: whoever has the panel password can flash any firmware to the controllers.

## Layout

```
public/            document root: index.php (front controller), assets/ (css, js, logo, icons), sw.js
app/Core/          Router, Request, Response, View, Db, Config, Auth, I18n, HttpException
app/Controllers/   thin: request in, response out
app/Repositories/  all SQL (tables written as {p}name, the prefix is added in Db)
app/Services/      rules: CommandPolicy, StateNormalizer, Installer, FuelModel, FirmwareImage, Maintenance
app/Lang/          translations (pl, en, de, cs, sk)
app/Views/         HTML only (layout, panel with <template>s, setup); the panel JS clones the templates
config/            routes.php, routes.setup.php
database/          schema.sql
storage/           config.php (written by the installer), firmware images, sessions
tools/             setup.php, hash.php
```

## How a controller joins

1. First boot: the controller generates a random secret (32+ letters or digits) and keeps it in flash. Its id is `hinasto-<MAC>`.
2. It POSTs `/api/v1/sync` with `X-Device-Id`, `X-Device-Secret` and `X-Enroll-Key` (the key from the installer).
3. The server stores a **pending** controller (only the sha256 of the secret) and answers `{"status":"pending","retry":10}`.
4. The panel shows a dialog: id, IP, firmware, first contact. Approving adds it, rejecting makes it back off for 10 min.
5. After approval the same secret authenticates every request, nobody else can take over that id. A rejected controller stays in the list of the add-controller dialog, where it can be taken back or deleted. Pending controllers that are not heard for an hour are dropped, and one IP address can have at most 5 waiting.

The panel button for adding a controller shows the server address and the enrollment key (`GET /api/panel/enroll`, login
required) that the setup portal of the controller asks for. A controller that was factory reset has a new secret and gets 401
until its old record is deleted in the panel.

## Device API

`POST /api/v1/sync`, JSON body, every ~3 s (use the `retry` seconds from the answer).

```json
{
  "fw": "22", "lang": "en", "uptime": 12345, "rssi": -43,
  "env": {"t": 24.5, "h": 41, "age": 2},
  "heaters": [{
    "i": 0, "name": "Heater", "run": "on", "manual": true, "level": 3, "setpoint": null,
    "chamber": 206, "volts": 12, "age": 1, "busy": null, "err": null
  }],
  "log": [{"m": "off, voltage 12 V"}],
  "acks": [{"id": 42, "ok": true, "msg": ""}]
}
```

`run` is one of `unknown off starting on cooling`. `lang` is the language of the firmware build (missing = a Polish build from
before languages existed). `log` carries only lines not sent before. `acks` confirm commands received earlier (`ok:false` with
a reason when the controller refused or the heater did not follow). The body must be UTF-8 JSON. The controller keeps its
pending `acks` and `log` lines until it gets an HTTP 200 back, and only then drops them.

Answer:

```json
{"status": "approved", "retry": 3,
 "commands": [{"id": 42, "heater": 0, "action": "level", "value": 6}],
 "update": {"version": 23, "size": 1258432, "md5": "...", "file": "firmware-en"}}
```

`action` is `on`, `off`, `level` (1-6), `temp` (6-35, degrees C), `mode` (`1` manual, `0` auto) or `smart` (`0` off, 6-35 on or a
new target). The server only queues commands that make sense for the last reported state, and the controller must check again
before acting: `on` only when `off`; `level` only in manual; `temp` only in auto; `mode` only when running; `off` always.
Commands not acknowledged within `command_ttl_seconds` expire. Errors: 400 bad request, 401 wrong secret, 403 enrollment key
missing, 429 too many pending controllers.

`update` appears when a release of the controller's own language is newer than its `fw`; the image is downloaded with
`GET /api/v1/<file>?v=<version>` using the normal device headers.

## Firmware releases

The panel button **Firmware** uploads a `.bin` from the build (`../tools/publish-firmware.ps1` builds and uploads every
language). The server reads the version (`HINASTO-FW:<n>`) and the language (`HINASTO-LANG:<code>`) from the image, keeps the
last 5 releases of each language in `storage/firmware/<version>-<lang>.bin` and offers the newest of its own language to every
controller that reports an older number. Controllers never switch language by updating. Needs `upload_max_filesize` of at least
2 MB in PHP. Set `auto_update` to `false` in `storage/config.php` to stop offering updates.

If the web server in front of PHP answers dynamic requests in chunks (some reverse proxies do), old firmware refuses the
download because it has no `Content-Length`; the server therefore also writes the newest image of each language as a plain file
`public/api/v1/firmware-<lang>` (and `firmware`, the Polish one, for controllers from before languages), which is served
statically with its length. Those files are public (the images hold no secrets) and are recreated on demand after a deploy.

## Server version

The version of the application is the first line of the `VERSION` file (raise it by hand per release). The deploy stamps the
build time into `BUILD` (Dockerfile). The version shows in the footer of every page. The firmware has its own number
(`FW_VERSION` in `sniffer/control.h`).

## Smart mode

The command `smart` makes the controller keep the room at the target by itself using its temperature sensor; the state of
every heater then carries `smart: {on, target, phase, level, rate}` (`rate` in degrees per hour; `phase` is one of the legacy
identifiers `czeka`, `start`, `rozgrzewka`, `grzanie`, `utrzymanie`, translated by the panel). Any other command from the panel
switches smart off. Logic: `../sniffer/smart.h`, simulation: `../tools/smart-sim.cpp`.

## Heater work time, analysis, retention

Every accepted sync updates the table `runs`: a period starts when a heater reports `on` or `starting` and ends with the first
other known state (5 minutes of silence from the controller end it at the last time it was seen). `GET /api/panel/runs` returns
the periods and the totals; the panel draws them on the temperature chart.

The analysis view shows one heater at a time: charts of temperature, humidity, chamber and voltage with the working periods,
the power level and mode (manual or smart, never the heater's own auto) over time, work time, starts, an estimated fuel use
(`FuelModel`: 0.16 to 0.82 l/h for H1 to H6 and 0.03 l per start, override with `fuel_lph` and `fuel_start_l` in
`storage/config.php`) and a day by day list (`GET /api/panel/analysis`). Old data is deleted automatically: readings, log lines,
mode records and commands after `retention_days` (default 7); the working periods are kept for the total work time.

## Install as an app (PWA)

The panel has a web app manifest, icons and a network-only service worker (`public/sw.js`), so a browser can install it:
Chrome, Edge and Android offer it (also in the panel menu), on an iPhone use Safari: Share, then Add to Home Screen. HTTPS is
required (`localhost` is exempt for testing). The worker does not cache anything, so an updated panel is always what you see.

## Local test

```
php -S 0.0.0.0:8080 -t public public/index.php
```

The router script is needed for `public/api` to work with the built-in server.
