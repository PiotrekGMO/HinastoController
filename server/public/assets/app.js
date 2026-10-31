'use strict';
// Hinasto Controller panel. All markup lives in the <template> elements of the page;
// this script only clones them, fills them with data and updates the elements in place.

const $ = (selector, root = document) => root.querySelector(selector);
const $$ = (selector, root = document) => [...root.querySelectorAll(selector)];

// Texts come from the server (language chosen at install, see app/Lang): <script id="i18n"> holds the "js.*" strings.
const I18N = JSON.parse(document.getElementById('i18n').textContent);
const LOCALE = I18N._locale;
function T(key, params) {
  const text = I18N['js.' + key] !== undefined ? I18N['js.' + key] : key;
  return params ? text.replace(/\{(\w+)\}/g, (m, name) => (params[name] !== undefined ? params[name] : m)) : text;
}
const num = (value, digits) => value.toLocaleString(LOCALE, {minimumFractionDigits: digits, maximumFractionDigits: digits});

const RUN_TONE = {unknown: 'bad', off: '', starting: 'hot', on: 'hot', cooling: 'cool'};
const BASE = document.querySelector('meta[name="base"]').content;  // URL prefix of the app, empty on a (sub)domain root
const POLL_MS = 2000;
const STATE_FRESH_S = 30;

let csrf = '';
let pending = [];
let rejected = [];  // controllers that were rejected: they can be taken back or deleted (in the add-controller dialog)
let pollTimer = null;
let modalKey = null;
const cards = new Map();  // device id -> {el, heaters: Map, setpoint: {}, log: {last, rows}, hist: {at, points}}

// ---- helpers ----

async function api(path, body) {
  const options = {credentials: 'same-origin', headers: {}};
  if (body !== undefined) {
    options.method = 'POST';
    options.headers['Content-Type'] = 'application/json';
    options.body = JSON.stringify(body);
  }
  if (csrf) options.headers['X-CSRF'] = csrf;
  try {
    const r = await fetch(BASE + '/api/' + path, options);
    let json = {};
    try { json = await r.json(); } catch (e) { /* empty body */ }
    return {ok: r.ok, status: r.status, json};
  } catch (e) {
    return {ok: false, status: 0, json: {error: T('no_connection')}};
  }
}

function flash(text) {
  const box = $('#msg');
  box.textContent = text || '';
  box.hidden = !text;
}

function fromTemplate(id) {
  return $('#' + id).content.firstElementChild.cloneNode(true);
}

function bind(root, values) {
  for (const [key, value] of Object.entries(values)) {
    const el = $(`[data-bind="${key}"]`, root);
    if (el) el.textContent = value;
  }
}

function setStatus(el, text, tone) {
  el.className = 'status' + (tone ? ' ' + tone : '');
  $('b', el).textContent = text;
}

// ---- login ----

function showLogin() {
  clearInterval(pollTimer);
  pollTimer = null;
  $('#app').hidden = true;
  $('#login').hidden = false;
  $('#logout').hidden = true;
  $('#addDevice').hidden = true;
  $('#firmware').hidden = true;
  $('#menuBtn').hidden = true;
  $('#openAnalytics').hidden = true;
  document.body.classList.remove('view-analytics');
  updateInstall();
  $('#pw').focus();
}

function showApp() {
  $('#login').hidden = true;
  $('#app').hidden = false;
  $('#logout').hidden = false;
  $('#addDevice').hidden = false;
  $('#firmware').hidden = false;
  $('#menuBtn').hidden = false;
  $('#openAnalytics').hidden = false;
  updateInstall();
  refresh();
  clearInterval(pollTimer);
  pollTimer = setInterval(refresh, POLL_MS);
}

$('#loginForm').addEventListener('submit', async (event) => {
  event.preventDefault();
  const r = await api('panel/login', {password: $('#pw').value});
  if (!r.ok) { flash(r.json.error || T('login_error')); return; }
  flash('');
  $('#pw').value = '';
  csrf = r.json.csrf;
  showApp();
});

$('#logout').addEventListener('click', async () => {
  await api('panel/logout', {});
  csrf = '';
  for (const c of cards.values()) c.el.remove();
  cards.clear();
  showLogin();
});

// ---- hamburger menu of the top bar (phone) ----

function closeMenu() {
  $('#topActions').classList.remove('open');
  $('#menuBtn').setAttribute('aria-expanded', 'false');
}
$('#menuBtn').addEventListener('click', (event) => {
  event.stopPropagation();
  const open = $('#topActions').classList.toggle('open');
  $('#menuBtn').setAttribute('aria-expanded', String(open));
});
$('#topActions').addEventListener('click', closeMenu);  // after the chosen item has done its job
document.addEventListener('click', (event) => { if (!event.target.closest('#topActions, #menuBtn')) closeMenu(); });
document.addEventListener('keydown', (event) => { if (event.key === 'Escape') closeMenu(); });

// ---- install as an app (PWA): Chrome / Edge / Android offer it by an event, iPhone only by hand ----

let installEvent = null;
const standalone = () => matchMedia('(display-mode: standalone)').matches || navigator.standalone === true;
const iosSafari = () => /iphone|ipad|ipod/i.test(navigator.userAgent) && !/crios|fxios|edgios/i.test(navigator.userAgent);

function updateInstall() {
  const loggedIn = !$('#menuBtn').hidden;
  $('#installApp').hidden = !(loggedIn && !standalone() && (installEvent || iosSafari()));
}
window.addEventListener('beforeinstallprompt', (event) => { event.preventDefault(); installEvent = event; updateInstall(); });
window.addEventListener('appinstalled', () => { installEvent = null; updateInstall(); });
$('#installApp').addEventListener('click', async () => {
  if (installEvent) {
    installEvent.prompt();
    await installEvent.userChoice;
    installEvent = null;
    updateInstall();
    return;
  }
  const dialog = fromTemplate('tpl-ios-install');
  const box = $('#info');
  const close = () => { box.hidden = true; box.replaceChildren(); };
  $('[data-a="close"]', dialog).addEventListener('click', close);
  box.replaceChildren(dialog);
  box.hidden = false;
});
if ('serviceWorker' in navigator) {  // network only, see sw.js; needed by browsers to treat the page as an installable app
  navigator.serviceWorker.register(BASE + '/sw.js?l=' + document.documentElement.lang, {scope: BASE + '/'}).catch(() => { /* not fatal */ });
}

// ---- the ⋯ menu in the footer of a card (narrow screens) ----

function closeMore(except) {
  for (const m of $$('.more.open')) {
    if (m === except) continue;
    m.classList.remove('open');
    $('[data-more]', m).setAttribute('aria-expanded', 'false');
  }
}
document.addEventListener('click', (event) => {
  const btn = event.target.closest('[data-more]');
  const menu = btn ? btn.closest('.more') : null;
  closeMore(menu);  // a click anywhere else, or on a chosen item, closes the open menu
  if (menu) {
    const open = menu.classList.toggle('open');
    btn.setAttribute('aria-expanded', String(open));
  }
});
document.addEventListener('keydown', (event) => { if (event.key === 'Escape') closeMore(null); });

// ---- adding a controller: shows what its setup portal asks for ----

$('#addDevice').addEventListener('click', async () => {
  const r = await api('panel/enroll');
  if (!r.ok) { flash(r.json.error || T('error')); return; }
  const dialog = fromTemplate('tpl-enroll');
  bind(dialog, {url: location.origin + BASE, key: r.json.key || T('enroll.no_key')});
  const box = $('#info');
  const close = () => { box.hidden = true; box.replaceChildren(); };
  $('[data-a="close"]', dialog).addEventListener('click', close);
  if (rejected.length) {
    $('[data-rejected]', dialog).hidden = false;
    for (const d of rejected) {
      const li = document.createElement('li');
      li.append(`${d.device_id} · ${d.ip || '–'} `);
      for (const [op, label] of [['approve', T('rejected.add')], ['delete', T('rejected.delete')]]) {
        const b = document.createElement('button');
        b.className = 'btn ghost';
        b.textContent = label;
        b.addEventListener('click', async () => {
          const res = await api('panel/device', {id: d.id, op});
          if (!res.ok) flash(res.json.error || T('error_n', {n: res.status}));
          close();
          refresh();
        });
        li.append(b);
      }
      $('[data-rejected-list]', dialog).append(li);
    }
  }
  $('[data-a="copy"]', dialog).addEventListener('click', async (e) => {
    try { await navigator.clipboard.writeText(r.json.key); e.target.textContent = T('enroll.copied'); } catch (err) { e.target.textContent = T('enroll.copy_manual'); }
  });
  box.replaceChildren(dialog);
  box.hidden = false;
});

// ---- firmware releases ----

async function openFirmware() {
  const r = await api('panel/firmware');
  if (!r.ok) { flash(r.json.error || T('error')); return; }
  const dialog = fromTemplate('tpl-firmware');
  const box = $('#info');
  const close = () => { box.hidden = true; box.replaceChildren(); };
  const latest = {};  // newest release per language: controllers update only within their own language
  for (const rel of r.json.latest) latest[rel.lang] = rel;
  const rows = Object.values(latest).map((rel) => `${rel.lang.toUpperCase()} v${rel.version}, ${(rel.size / 1048576).toFixed(2)} MB, ${rel.created_at}`);
  bind(dialog, {latest: rows.length ? rows.join('\n') : T('fw.none')});
  const list = $('[data-list]', dialog);
  for (const d of r.json.devices) {
    const li = document.createElement('li');
    const rel = latest[d.lang];
    const v = parseInt(d.fw, 10);
    const state = !rel || !v ? '' : v >= rel.version ? T('fw.current') : T('fw.will_update');
    li.textContent = `${d.name}: v${d.fw || '?'} ${d.lang.toUpperCase()} ${state}`.trim();
    list.appendChild(li);
  }
  $('[data-a="close"]', dialog).addEventListener('click', close);
  $('[data-a="upload"]', dialog).addEventListener('click', async (e) => {
    const file = $('#fwFile', dialog).files[0];
    const msg = $('#fwMsg', dialog);
    msg.hidden = false;
    if (!file) { msg.textContent = T('fw.pick'); return; }
    e.target.disabled = true;
    msg.textContent = T('fw.uploading');
    const form = new FormData();
    form.append('file', file);
    let json = {};
    try {
      const resp = await fetch(BASE + '/api/panel/firmware', {method: 'POST', body: form, credentials: 'same-origin', headers: {'X-CSRF': csrf}});
      try { json = await resp.json(); } catch (err) { /* empty body */ }
      msg.textContent = resp.ok ? T('fw.published', {version: json.version, lang: String(json.lang).toUpperCase()}) : (json.error || T('error'));
      if (resp.ok) setTimeout(openFirmware, 1500);
    } catch (err) { msg.textContent = T('no_connection'); }
    e.target.disabled = false;
  });
  box.replaceChildren(dialog);
  box.hidden = false;
}
$('#firmware').addEventListener('click', openFirmware);

// ---- rendering ----

// While a command has not been answered by the controller no further command may be issued (they would race).
// Power off stays available: it is the emergency stop and it interrupts whatever is running.
function lockButtons(root) {
  for (const b of $$('button[data-a]', root)) if (b.dataset.a !== 'off') b.disabled = true;
}

function updateHeater(card, device, h) {
  let el = card.heaters.get(h.i);
  if (!el) {
    el = fromTemplate('tpl-heater');
    el.dataset.h = h.i;
    card.heaters.set(h.i, el);
    $('[data-heaters]', card.el).appendChild(el);
  }
  const live = device.online && device.state_ago !== null && device.state_ago <= STATE_FRESH_S;
  const run = live ? h.run : 'unknown';
  const busy = !!h.busy;
  const on = run === 'on';
  const off = run === 'off';
  const usable = on && !busy;
  const key = String(h.i);
  const smart = h.smart && h.smart.on ? h.smart : null;
  if (card.setpoint[key] === undefined) card.setpoint[key] = (smart ? smart.target : h.setpoint) || 20;
  if (!!smart !== !!card.smartWas) {  // the target box follows the mode: smart target while smart runs, the heater's own setpoint otherwise
    card.smartWas = !!smart;
    card.setpoint[key] = (smart ? smart.target : h.setpoint) || card.setpoint[key];
  }
  card.lastSmart = device.last_smart || null;

  const runText = T('run.' + run);
  const tone = RUN_TONE[run];
  const many = (device.state.heaters || []).length > 1;
  bind(el, {
    title: many ? (h.name || T('heater_n', {n: h.i + 1})) : T('heater'),
    chamber: on && h.chamber != null ? h.chamber + ' °C' : '–',
    volts: h.volts != null ? h.volts + ' V' : '–',
    level: h.level != null ? 'H' + h.level : '–',
    setpoint: smart ? smart.target + ' °C' : h.setpoint != null ? h.setpoint + ' °C' : '–',
    autolabel: smart ? T('smart_target_label') : T('setpoint_label'),
  });
  setStatus($('[data-bind="run"]', el), runText + (busy ? ' · ' + h.busy + '…' : ''), tone);

  const err = $('[data-bind="err"]', el);
  err.textContent = h.err || '';
  err.hidden = !h.err;

  $('[data-a="on"]', el).disabled = !(off && !busy);
  $('[data-a="off"]', el).disabled = !(live && h.run !== 'unknown');

  $('[data-modebar]', el).hidden = !(live && h.run !== 'unknown');  // smart can start a heater that is off
  for (const b of $$('[data-a="mode"]', el)) {
    if (b.dataset.v === '2') {
      b.disabled = busy;
      b.classList.toggle('sel', !!smart);
    } else {
      b.disabled = !usable;
      b.classList.toggle('sel', on && !smart && (b.dataset.v === '1') === !!h.manual);
    }
  }
  $('[data-idle]', el).hidden = on || !!smart;
  $('[data-manual]', el).hidden = !(on && h.manual && !smart);
  $('[data-auto]', el).hidden = !(smart || (on && !h.manual));
  const tempBtn = $('[data-tempbtn]', el);
  tempBtn.dataset.a = smart ? 'smart' : 'temp';
  tempBtn.textContent = smart ? T('set_smart') : T('set_temp');
  const info = $('[data-smartinfo]', el);
  info.textContent = '';  // the box keeps its height (see .smartinfo) so the panels never jump
  if (smart) {
    const phase = 'phase.' + smart.phase;
    const parts = [I18N['js.' + phase] !== undefined ? T(phase) : smart.phase];
    if (smart.level) parts.push('H' + smart.level);
    if (smart.rate != null) parts.push((smart.rate > 0 ? '+' : '') + num(smart.rate, 1) + ' °C/h');
    info.textContent = T('smart_info', {parts: parts.join(' · ')}) + (on ? '' : T('smart_waiting', {t: smart.target - 3}));
  }
  $('[data-smartoff]', el).hidden = !smart;

  for (const b of $$('[data-a="level"]', el)) {
    b.disabled = !usable;
    b.classList.toggle('sel', Number(b.dataset.v) === h.level);
  }
  for (const b of $$('[data-a="bump"]', el)) b.disabled = !(usable || smart);
  tempBtn.disabled = !(usable || smart);
  $('[data-smartoff]', el).disabled = false;
  if (device.locked) lockButtons(el);
  const input = $('input.sp', el);
  if (document.activeElement !== input) input.value = card.setpoint[key];
}

// ---- temperature chart: axes, grid, selectable range, a readout under the finger or the mouse ----

const RANGES = [1, 3, 6, 12, 24];
const SVG = 'http://www.w3.org/2000/svg';

function svgEl(name, attrs, parent) {
  const n = document.createElementNS(SVG, name);
  for (const [k, v] of Object.entries(attrs || {})) n.setAttribute(k, v);
  if (parent) parent.appendChild(n);
  return n;
}

function niceStep(span, want) {
  const raw = Math.max(span, 0.0001) / want;
  const p = Math.pow(10, Math.floor(Math.log10(raw)));
  const f = raw / p;
  return (f < 1.5 ? 1 : f < 3.5 ? 2 : f < 7.5 ? 5 : 10) * p;
}

function clock(t) {
  return new Date(t * 1000).toLocaleTimeString(LOCALE, {hour: '2-digit', minute: '2-digit'});
}

function ago(seconds) {
  const m = Math.round(seconds / 60);
  return m < 1 ? T('now') : m < 60 ? T('min_ago', {n: m}) : T('hm_ago', {h: Math.floor(m / 60), m: String(m % 60).padStart(2, '0')});
}

function fmtDur(seconds) {
  let m = Math.round(seconds / 60);
  const h = Math.floor(m / 60);
  m -= h * 60;
  return h > 0 ? T('dur_hm', {h, m: String(m).padStart(2, '0')}) : T('dur_m', {m});
}

function drawChart(card) {
  const wrap = $('[data-spark]', card.el);
  const svg = $('svg', wrap);
  const tip = $('[data-tip]', wrap);
  const {points, hours, at} = card.hist;
  if (!svg.clientWidth) return false;  // the panel is hidden (the analysis view is open): no width to draw for, try again when it is shown
  const W = Math.max(240, svg.clientWidth);
  const H = 190;
  const pad = {l: 40, r: 10, t: 10, b: 22};
  svg.setAttribute('viewBox', `0 0 ${W} ${H}`);
  svg.replaceChildren();

  const tEnd = at / 1000;
  const tStart = tEnd - hours * 3600;
  let lo = Infinity;
  let hi = -Infinity;
  for (const p of points) { lo = Math.min(lo, p[1]); hi = Math.max(hi, p[1]); }
  const shown = {lo, hi};
  if (hi - lo < 1) { const mid = (hi + lo) / 2; lo = mid - 0.5; hi = mid + 0.5; }
  const step = niceStep(hi - lo, 4);
  lo = Math.floor((lo - (hi - lo) * 0.05) / step) * step;
  hi = Math.ceil((hi + (hi - lo) * 0.05) / step) * step;
  const x = (t) => pad.l + ((t - tStart) / (tEnd - tStart)) * (W - pad.l - pad.r);
  const y = (v) => pad.t + (1 - (v - lo) / (hi - lo)) * (H - pad.t - pad.b);

  const grid = svgEl('g', {class: 'grid'}, svg);
  const decimals = step < 1 ? 1 : 0;
  for (let v = lo; v <= hi + step / 2; v += step) {
    svgEl('line', {x1: pad.l, x2: W - pad.r, y1: y(v), y2: y(v)}, grid);
    svgEl('text', {x: pad.l - 6, y: y(v) + 4, 'text-anchor': 'end'}, grid).textContent = v.toFixed(decimals);
  }
  const mins = [10, 15, 30, 60, 120, 180, 360, 720].find((m) => hours * 60 / m <= 6) || 720;
  const off = -new Date().getTimezoneOffset() * 60;  // ticks on whole local hours / half hours
  const unit = mins * 60;
  for (let t = Math.ceil((tStart + off) / unit) * unit - off; t <= tEnd; t += unit) {
    svgEl('line', {x1: x(t), x2: x(t), y1: pad.t, y2: H - pad.b, class: 'vgrid'}, grid);
    svgEl('text', {x: x(t), y: H - 6, 'text-anchor': 'middle'}, grid).textContent = clock(t);
  }

  // periods in which the heater worked: a light band behind the line, the line itself turns red inside it
  const bands = (card.hist.runs || []).map((p) => [Math.max(p[0], tStart), Math.min(p[1], tEnd)]).filter((p) => p[1] > p[0]);
  const clipId = 'runclip' + card.el.dataset.dev;
  if (bands.length) {
    const clip = svgEl('clipPath', {id: clipId}, svgEl('defs', {}, svg));
    for (const b of bands) {
      const w = Math.max(1.5, x(b[1]) - x(b[0]));
      svgEl('rect', {x: x(b[0]), y: pad.t, width: w, height: H - pad.t - pad.b}, clip);
      svgEl('rect', {class: 'run-band', x: x(b[0]), y: pad.t, width: w, height: H - pad.t - pad.b}, svg);
    }
  }

  // a gap in the data (controller offline) breaks the line instead of drawing a false slope
  const gap = Math.max(600, (hours * 3600 / 300) * 3);
  const segments = [];
  let seg = [];
  for (const p of points) {
    if (seg.length && p[0] - seg[seg.length - 1][0] > gap) { segments.push(seg); seg = []; }
    seg.push(p);
  }
  if (seg.length) segments.push(seg);
  for (const sg of segments) {
    const line = sg.map((p) => x(p[0]).toFixed(1) + ',' + y(p[1]).toFixed(1));
    if (sg.length > 1) {
      svgEl('polygon', {class: 'area', points: `${x(sg[0][0]).toFixed(1)},${y(lo)} ${line.join(' ')} ${x(sg[sg.length - 1][0]).toFixed(1)},${y(lo)}`}, svg);
      svgEl('polyline', {class: 'line', points: line.join(' ')}, svg);
      if (bands.length) svgEl('polyline', {class: 'line run', points: line.join(' '), 'clip-path': 'url(#' + clipId + ')'}, svg);
    } else {
      svgEl('circle', {class: 'dot', cx: x(sg[0][0]), cy: y(sg[0][1]), r: 2}, svg);
    }
  }

  const cross = svgEl('line', {class: 'cross', y1: pad.t, y2: H - pad.b, visibility: 'hidden'}, svg);
  const mark = svgEl('circle', {class: 'dot', r: 4, visibility: 'hidden'}, svg);
  const hide = () => { cross.setAttribute('visibility', 'hidden'); mark.setAttribute('visibility', 'hidden'); tip.hidden = true; };
  const move = (event) => {
    const box = svg.getBoundingClientRect();
    const px = (event.clientX - box.left) * (W / box.width);
    let best = null;
    for (const p of points) if (!best || Math.abs(x(p[0]) - px) < Math.abs(x(best[0]) - px)) best = p;
    if (!best || Math.abs(x(best[0]) - px) > 40) { hide(); return; }
    cross.setAttribute('x1', x(best[0])); cross.setAttribute('x2', x(best[0])); cross.setAttribute('visibility', 'visible');
    mark.setAttribute('cx', x(best[0])); mark.setAttribute('cy', y(best[1])); mark.setAttribute('visibility', 'visible');
    tip.hidden = false;
    const working = bands.some((b) => best[0] >= b[0] && best[0] <= b[1]);
    tip.textContent = `${best[1].toFixed(1)} °C · ${clock(best[0])} (${ago(tEnd - best[0])})${working ? T('tip_worked') : ''}`;
    const left = Math.min(Math.max(x(best[0]) / W * box.width - tip.offsetWidth / 2, 0), box.width - tip.offsetWidth);
    tip.style.left = left + 'px';
  };
  svg.onpointermove = move;
  svg.onpointerdown = move;
  svg.onpointerleave = hide;
  const worked = bands.reduce((sum, b) => sum + (b[1] - b[0]), 0);
  $('[data-legend]', wrap).hidden = !bands.length;
  bind(wrap, {sparkNote: T('spark_note', {hours, min: num(shown.lo, 1), max: num(shown.hi, 1)}) + (bands.length ? T('spark_worked', {dur: fmtDur(worked)}) : '')});
  for (const b of $$('[data-r]', wrap)) b.classList.toggle('sel', Number(b.dataset.r) === card.range);
  return true;
}
function updateCard(device) {
  let card = cards.get(device.id);
  if (!card) {
    const el = fromTemplate('tpl-device');
    el.dataset.dev = device.id;
    card = {el, heaters: new Map(), setpoint: {}, log: {last: 0, rows: []}, hist: null, range: savedRange(device.id), drawn: 0};
    cards.set(device.id, card);
    $('#devices').appendChild(el);
  }
  const el = card.el;
  const s = device.state || {heaters: [], env: null};

  bind(el, {
    name: device.name,
    meta: `${device.device_id} · fw ${device.fw || '–'}${s.lang ? ' ' + s.lang.toUpperCase() : ''} · ${device.ip}`,
    seen: T('last_message', {when: device.seen_ago === null ? T('never') : T('s_ago', {n: device.seen_ago})}) + (s.rssi != null ? ` · WiFi ${s.rssi} dBm` : ''),
  });
  setStatus($('[data-bind="online"]', el), device.online ? 'online' : 'offline', device.online ? 'ok' : 'bad');

  const env = s.env;
  $('[data-env]', el).hidden = !env;
  if (env) bind(el, {envT: num(env.t, 1) + ' °C', envH: Math.round(env.h) + ' %'});
  const spark = $('[data-spark]', el);
  const points = card.hist && card.hist.points;
  spark.hidden = !(env && points && points.length > 1);
  if (!spark.hidden && card.drawn !== card.hist.at) {
    if (drawChart(card)) card.drawn = card.hist.at;
  }

  const tt = card.hist && card.hist.totals;
  $('[data-runtiles]', el).hidden = !tt;
  if (tt) {
    const since = tt.since ? new Date(tt.since * 1000).toLocaleDateString(LOCALE, {day: '2-digit', month: '2-digit'}) : null;
    bind(el, {runDay: fmtDur(tt.day), run24: fmtDur(tt.h24), runTotal: fmtDur(tt.total), runTotalLabel: since ? T('total_since', {date: since}) : T('total')});
  }

  const heaters = s.heaters || [];
  $('[data-noheaters]', el).hidden = heaters.length > 0;
  const seen = new Set(heaters.map((h) => h.i));
  for (const [i, node] of card.heaters) if (!seen.has(i)) { node.remove(); card.heaters.delete(i); }
  const locked = card.sending || device.commands.some((c) => c.status === 'queued' || c.status === 'sent');
  for (const h of heaters) updateHeater(card, {...device, state: s, locked}, h);

  const box = $('[data-cmds]', el);
  const SHOW_S = 15;  // a finished command stays on screen this long, one that is still running until it ends
  box.replaceChildren(...device.commands.filter((c) => c.status === 'queued' || c.status === 'sent' || c.age <= SHOW_S).map((c) => {
    const line = document.createElement('div');
    line.className = c.status === 'done' ? 'ok' : (c.status === 'error' || c.status === 'expired') ? 'err' : '';
    const arg = c.action === 'smart' ? (c.value ? ' ' + c.value + ' °C' : ' ' + T('cmd.smart_off')) : ['level', 'temp', 'mode'].includes(c.action) ? ' ' + c.value : '';
    const known = (kind, id) => (I18N['js.' + kind + '.' + id] !== undefined ? T(kind + '.' + id) : id);
    line.textContent = `${known('cmd', c.action)}${arg}: ${known('cmdstate', c.status)}${c.result ? ' (' + c.result + ')' : ''}`;
    return line;
  }));
}

function renderDevices(devices) {
  const ids = new Set(devices.map((d) => d.id));
  for (const [id, card] of cards) if (!ids.has(id)) { card.el.remove(); cards.delete(id); }
  devices.forEach((d, index) => {
    updateCard(d);
    const el = cards.get(d.id).el;
    if ($('#devices').children[index] !== el) $('#devices').insertBefore(el, $('#devices').children[index] || null);
    $('[data-a="up"]', el).disabled = index === 0;
    $('[data-a="down"]', el).disabled = index === devices.length - 1;
  });
  $('#empty').hidden = devices.length > 0;
}

function renderModal() {
  const box = $('#modal');
  const p = pending[0];
  if (!p) { modalKey = null; box.hidden = true; box.replaceChildren(); return; }
  const key = p.id + ':' + pending.length;
  if (key === modalKey && !box.hidden) return;  // do not rebuild while the user may be typing
  modalKey = key;
  const dialog = fromTemplate('tpl-modal');
  bind(dialog, {device_id: p.device_id, ip: p.ip || '–', fw: p.fw || '–', first_seen: p.first_seen});
  const more = $('[data-bind="more"]', dialog);
  more.hidden = pending.length < 2;
  more.textContent = T('waiting_more', {n: pending.length - 1});
  for (const b of $$('button[data-a]', dialog)) b.dataset.id = p.id;
  box.replaceChildren(dialog);
  box.hidden = false;
  $('#newName').focus();
}

// ---- polling ----

// every controller remembers its own chart range (the old shared key "hinasto-range" is simply not used any more)
function savedRange(deviceId) {
  try { const v = Number(localStorage.getItem('hinasto-range-' + deviceId)); if (RANGES.includes(v)) return v; } catch (e) { /* storage may be blocked */ }
  return 12;
}

async function pollHistory(device, card) {
  if (card.hist && card.hist.hours === card.range && Date.now() - card.hist.at < 60000) return;
  const hasEnv = !!(device.state && device.state.env);
  const [r, runs] = await Promise.all([
    hasEnv ? api(`panel/history?device=${device.id}&kind=env_t&hours=${card.range}`) : Promise.resolve({ok: true, json: {points: []}}),
    api(`panel/runs?device=${device.id}&hours=${card.range}`),
  ]);
  if (r.ok) {
    card.hist = {at: Date.now(), points: r.json.points, hours: card.range,
      runs: runs.ok ? runs.json.intervals : [], totals: runs.ok ? runs.json.totals : null};
  }
}

document.addEventListener('click', (event) => {
  const b = event.target.closest('[data-r]');
  if (!b) return;
  const card = cards.get(Number(b.closest('.device').dataset.dev));
  card.range = Number(b.dataset.r);
  card.hist = null;
  try { localStorage.setItem('hinasto-range-' + b.closest('.device').dataset.dev, String(card.range)); } catch (e) { /* ignore */ }
  refresh();
});

let resizeTimer = null;
window.addEventListener('resize', () => {
  clearTimeout(resizeTimer);
  resizeTimer = setTimeout(() => { for (const c of cards.values()) if (c.hist) c.drawn = 0; }, 150);  // redrawn by the next poll
});

async function pollLog(id, card) {
  const pre = $('[data-console]', card.el);
  if (pre.hidden) return;
  const r = await api(`panel/log?device=${id}&after=${card.log.last}`);
  if (!r.ok || !r.json.lines.length) return;
  const atBottom = pre.scrollTop + pre.clientHeight >= pre.scrollHeight - 24;
  for (const line of r.json.lines) card.log.rows.push(line.t + '  ' + line.msg);
  card.log.rows = card.log.rows.slice(-300);
  card.log.last = r.json.last;
  pre.textContent = card.log.rows.join('\n');
  if (atBottom) pre.scrollTop = pre.scrollHeight;
}

// A phone freezes a page in the background: the first requests after coming back fail. The message about the lost
// connection appears only when it lasts (3 failures in a row) and goes away by itself with the first answer.
let failures = 0;
let offlineShown = false;

// A deployment while the page is open (or a phone app resumed from the background) leaves the old scripts in memory.
// The server tells the build of the files it serves; when it differs from the one this page was loaded with, reload.
let updatePending = false;
function checkBuild(build) {
  const mine = (document.querySelector('meta[name="app-build"]') || {}).content;
  if (!build || !mine || String(build) === mine || updatePending) return;
  updatePending = true;
  $('#updateBar').hidden = false;
  setTimeout(maybeReload, 4000);
}
function maybeReload() {
  const busy = !$('#modal').hidden || !$('#info').hidden || (document.activeElement && ['INPUT', 'TEXTAREA', 'SELECT'].includes(document.activeElement.tagName));
  if (updatePending && !busy) location.reload();
  else if (updatePending) setTimeout(maybeReload, 4000);  // somebody is typing or answering a dialog: do not take the page away
}
$('#updateNow').addEventListener('click', () => location.reload());

async function refresh() {
  if (document.hidden) return;  // no polling in a background tab, it resumes with the tab
  const r = await api('panel/state');
  if (r.status === 401) { showLogin(); return; }
  if (!r.ok) {
    if (r.status === 0 && ++failures < 3) return;
    flash(r.json.error || T('no_connection_short'));
    offlineShown = true;
    return;
  }
  failures = 0;
  if (offlineShown) { flash(''); offlineShown = false; }
  csrf = r.json.csrf;
  checkBuild(r.json.build);
  pending = r.json.pending;
  rejected = r.json.rejected || [];
  for (const d of r.json.devices) {
    const card = cards.get(d.id);
    if (card) await pollHistory(d, card);
  }
  renderDevices(r.json.devices);
  renderModal();
  for (const [id, card] of cards) pollLog(id, card);
}

// ---- actions ----

async function sendCommand(device, heater, action, value, confirmed) {
  flash('');
  const card = cards.get(device);
  card.sending = true;  // locks at once, before the server has even answered
  for (const el of $$('.heater', card.el)) lockButtons(el);
  const r = await api('panel/command', {device, heater, action, value, confirm: !!confirmed});
  if (!r.ok) flash(r.json.error || T('error_n', {n: r.status}));
  await refresh();  // the queued command is now in the list and keeps the lock until the controller answers
  card.sending = false;
}

document.addEventListener('input', (event) => {
  const input = event.target;
  if (!input.classList.contains('sp')) return;
  const card = cards.get(Number(input.closest('.device').dataset.dev));
  card.setpoint[input.closest('.heater').dataset.h] = parseInt(input.value, 10) || 0;
});

document.addEventListener('click', async (event) => {
  const button = event.target.closest('button[data-a]');
  if (!button || button.disabled) return;
  const action = button.dataset.a;
  const deviceEl = button.closest('.device');
  const heaterEl = button.closest('.heater');
  const device = deviceEl ? Number(deviceEl.dataset.dev) : 0;
  const heater = heaterEl ? Number(heaterEl.dataset.h) : 0;
  const value = Number(button.dataset.v || 0);
  const card = cards.get(device);

  switch (action) {
    case 'on':
      if (confirm(T('confirm_on'))) sendCommand(device, heater, 'on', 0, true);
      break;
    case 'off':
      if (confirm(T('confirm_off'))) sendCommand(device, heater, 'off', 0, true);
      break;
    case 'level':
      sendCommand(device, heater, 'level', value, false);
      break;
    case 'temp':
      sendCommand(device, heater, 'temp', card.setpoint[heater] || 20, false);
      break;
    case 'smart':
      sendCommand(device, heater, 'smart', card.setpoint[heater] || 20, true);
      break;
    case 'smartoff':
      sendCommand(device, heater, 'smart', 0, true);
      break;
    case 'mode':
      if (value === 2) {
        const target = card.lastSmart || card.setpoint[heater] || 20;  // the last smart target is remembered
        if (confirm(T('confirm_smart', {t: target}))) {
          sendCommand(device, heater, 'smart', target, true);
        }
        break;
      }
      if (confirm(value === 1 ? T('confirm_manual') : T('confirm_auto'))) {
        sendCommand(device, heater, 'mode', value, true);
      }
      break;
    case 'bump': {
      card.setpoint[heater] = Math.min(35, Math.max(6, (card.setpoint[heater] || 20) + value));
      $('input.sp', heaterEl).value = card.setpoint[heater];
      break;
    }
    case 'console': {
      const pre = $('[data-console]', deviceEl);
      pre.hidden = !pre.hidden;
      button.textContent = pre.hidden ? T('console') : T('console_hide');
      pre.textContent = card.log.rows.join('\n');
      pollLog(device, card);
      break;
    }
    case 'up':
    case 'down':
      await api('panel/device', {id: device, op: 'move', dir: action === 'up' ? -1 : 1});
      refresh();
      break;
    case 'rename': {
      const name = prompt(T('prompt_rename'),$('[data-bind="name"]', deviceEl).textContent);
      if (name) { await api('panel/device', {id: device, op: 'rename', name}); refresh(); }
      break;
    }
    case 'delete':
      if (confirm(T('confirm_delete'))) {
        await api('panel/device', {id: device, op: 'delete'});
        refresh();
      }
      break;
    case 'approve':
    case 'reject': {
      const r = await api('panel/device', {id: Number(button.dataset.id), op: action, name: $('#newName').value});
      if (!r.ok) flash(r.json.error || T('error_n', {n: r.status}));
      modalKey = null;
      refresh();
      break;
    }
  }
});

// ---- start ----

async function start() {
  const r = await api('panel/state');
  if (r.status === 401) showLogin();
  else if (r.ok) { flash(''); csrf = r.json.csrf; showApp(); }
  else {
    flash(r.json.error || T('no_connection'));
    setTimeout(start, 3000);  // keep trying, the message goes away when the server answers
  }
}
start();

// back from another app or the network returned: do not wait for the next tick
document.addEventListener('visibilitychange', () => { if (!document.hidden && pollTimer) refresh(); });
window.addEventListener('online', () => { if (pollTimer) refresh(); });
