// The analysis view: everything about ONE heater for a chosen range. It only reads (GET /api/panel/analysis) and shares
// the helpers of app.js (api, svgEl, niceStep, clock, fmtDur); the heater cards are not touched.
(() => {
  const METRICS = {
    env_t: {unit: '°C', dec: 1},
    env_h: {unit: '%', dec: 0},
    chamber: {unit: '°C', dec: 0},
    volts: {unit: 'V', dec: 1},
  };
  const state = {device: null, hours: 24, metric: 'env_t', data: null};
  const store = {
    get(k, d) { try { const v = localStorage.getItem('hinasto-ana-' + k); return v === null ? d : v; } catch (e) { return d; } },
    set(k, v) { try { localStorage.setItem('hinasto-ana-' + k, String(v)); } catch (e) { /* ignore */ } },
  };
  const visible = () => document.body.classList.contains('view-analytics');
  const litres = (l) => num(l || 0, 1) + ' l';
  const deg = (v) => (v === null || v === undefined ? '–' : num(v, 1) + ' °C');

  // ---- axes shared by both charts ----
  function timeTicks(t0, t1, width) {
    const span = t1 - t0;
    const most = Math.max(3, Math.floor(width / 58));
    const unitMin = [30, 60, 120, 180, 360, 720, 1440, 2880, 5760].find((m) => span / 60 / m <= most) || 5760;
    const unit = unitMin * 60;
    const off = -new Date().getTimezoneOffset() * 60;
    const ticks = [];
    if (unitMin >= 1440) {  // whole days: step by local midnights, a clock change in the range must not shift them
      const d = new Date(t0 * 1000);
      d.setHours(24, 0, 0, 0);
      for (; d.getTime() / 1000 <= t1; d.setDate(d.getDate() + unitMin / 1440)) ticks.push(d.getTime() / 1000);
    } else {
      for (let t = Math.ceil((t0 + off) / unit) * unit - off; t <= t1; t += unit) ticks.push(t);
    }
    const longSpan = span > 30 * 3600;
    const label = (t) => {
      const d = new Date(t * 1000);
      return longSpan && d.getHours() === 0 ? d.toLocaleDateString(LOCALE, {day: '2-digit', month: '2-digit'}) : clock(t);
    };
    return {ticks, label};
  }

  function frame(svg, W, H, pad, t0, t1) {
    svg.setAttribute('viewBox', `0 0 ${W} ${H}`);
    svg.replaceChildren();
    const x = (t) => pad.l + ((t - t0) / (t1 - t0)) * (W - pad.l - pad.r);
    const grid = svgEl('g', {class: 'grid'}, svg);
    const {ticks, label} = timeTicks(t0, t1, W - pad.l - pad.r);
    for (const t of ticks) {
      svgEl('line', {x1: x(t), x2: x(t), y1: pad.t, y2: H - pad.b, class: 'vgrid'}, grid);
      svgEl('text', {x: x(t), y: H - 6, 'text-anchor': 'middle'}, grid).textContent = label(t);
    }
    return {x, grid};
  }

  function bands(svg, x, runs, t0, t1, pad, H) {
    for (const r of runs) {
      const a = Math.max(r[0], t0);
      const b = Math.min(r[1], t1);
      if (b > a) svgEl('rect', {class: 'run-band', x: x(a), y: pad.t, width: Math.max(1.5, x(b) - x(a)), height: H - pad.t - pad.b}, svg);
    }
  }

  const empty = (svg, W, H, text) => {
    svg.setAttribute('viewBox', `0 0 ${W} ${H}`);
    svg.replaceChildren();
    svgEl('text', {x: W / 2, y: H / 2, 'text-anchor': 'middle', class: 'muted', fill: 'currentColor', 'font-size': 13}, svg).textContent = text;
  };

  // ---- the measured value over time, with the working periods behind it ----
  function drawMetric() {
    const d = state.data;
    const m = METRICS[state.metric];
    const svg = $('#anaChart');
    const tip = $('#anaTip');
    const W = Math.max(260, svg.clientWidth || 300);
    const H = 200;
    const pad = {l: 44, r: 10, t: 10, b: 24};
    const pts = d.series[state.metric] || [];
    if (pts.length < 2) { empty(svg, W, H, T('ana.no_data')); return; }

    let lo = Math.min(...pts.map((p) => p[1]));
    let hi = Math.max(...pts.map((p) => p[1]));
    if (hi - lo < 1) { const mid = (hi + lo) / 2; lo = mid - 0.5; hi = mid + 0.5; }
    const step = niceStep(hi - lo, 4);
    lo = Math.floor((lo - (hi - lo) * 0.05) / step) * step;
    hi = Math.ceil((hi + (hi - lo) * 0.05) / step) * step;
    const {x, grid} = frame(svg, W, H, pad, d.from, d.to);
    const y = (v) => pad.t + (1 - (v - lo) / (hi - lo)) * (H - pad.t - pad.b);
    for (let v = lo; v <= hi + step / 2; v += step) {
      svgEl('line', {x1: pad.l, x2: W - pad.r, y1: y(v), y2: y(v)}, grid);
      svgEl('text', {x: pad.l - 6, y: y(v) + 4, 'text-anchor': 'end'}, grid).textContent = v.toFixed(step < 1 ? 1 : 0);
    }
    bands(svg, x, d.runs, d.from, d.to, pad, H);

    const gap = Math.max(900, (state.hours * 3600 / 300) * 3);  // a hole in the data breaks the line
    const segments = [];
    let seg = [];
    for (const p of pts) {
      if (seg.length && p[0] - seg[seg.length - 1][0] > gap) { segments.push(seg); seg = []; }
      seg.push(p);
    }
    if (seg.length) segments.push(seg);
    for (const sg of segments) {
      const line = sg.map((p) => x(p[0]).toFixed(1) + ',' + y(p[1]).toFixed(1));
      if (sg.length > 1) {
        svgEl('polygon', {class: 'area', points: `${x(sg[0][0]).toFixed(1)},${y(lo)} ${line.join(' ')} ${x(sg[sg.length - 1][0]).toFixed(1)},${y(lo)}`}, svg);
        svgEl('polyline', {class: 'line', points: line.join(' ')}, svg);
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
      for (const p of pts) if (!best || Math.abs(x(p[0]) - px) < Math.abs(x(best[0]) - px)) best = p;
      if (!best || Math.abs(x(best[0]) - px) > 40) { hide(); return; }
      cross.setAttribute('x1', x(best[0])); cross.setAttribute('x2', x(best[0])); cross.setAttribute('visibility', 'visible');
      mark.setAttribute('cx', x(best[0])); mark.setAttribute('cy', y(best[1])); mark.setAttribute('visibility', 'visible');
      tip.hidden = false;
      const working = d.runs.some((r) => best[0] >= r[0] && best[0] <= r[1]);
      tip.textContent = `${best[1].toFixed(m.dec)} ${m.unit} · ${clock(best[0])} (${ago(d.to - best[0])})${working ? T('tip_worked') : ''}`;
      tip.style.left = Math.min(Math.max(x(best[0]) / W * box.width - tip.offsetWidth / 2, 0), box.width - tip.offsetWidth) + 'px';
    };
    svg.onpointermove = move;
    svg.onpointerdown = move;
    svg.onpointerleave = hide;
  }

  // ---- power level over time: a coloured stroke per period, blue for smart, orange for manual ----
  function drawLevels() {
    const d = state.data;
    const svg = $('#anaLevels');
    const tip = $('#anaTip2');
    const W = Math.max(260, svg.clientWidth || 300);
    const H = 170;
    const pad = {l: 44, r: 10, t: 10, b: 24};
    if (!d.runs.length) { empty(svg, W, H, T('ana.no_work')); return; }
    const {x, grid} = frame(svg, W, H, pad, d.from, d.to);
    const y = (level) => pad.t + (1 - (level - 0.5) / 6) * (H - pad.t - pad.b);
    for (let level = 1; level <= 6; level++) {
      svgEl('line', {x1: pad.l, x2: W - pad.r, y1: y(level), y2: y(level)}, grid);
      svgEl('text', {x: pad.l - 6, y: y(level) + 4, 'text-anchor': 'end'}, grid).textContent = 'H' + level;
    }
    bands(svg, x, d.runs, d.from, d.to, pad, H);
    for (const st of d.steps) {
      const a = Math.max(st[0], d.from);
      const b = Math.min(st[1], d.to);
      if (b > a) svgEl('line', {class: 'lv ' + st[2], x1: x(a), x2: x(Math.max(b, a + 120)), y1: y(st[3]), y2: y(st[3])}, svg);
    }
    const hide = () => { tip.hidden = true; };
    const move = (event) => {
      const box = svg.getBoundingClientRect();
      const px = (event.clientX - box.left) * (W / box.width);
      const t = d.from + ((px - pad.l) / (W - pad.l - pad.r)) * (d.to - d.from);
      const st = d.steps.find((s) => t >= s[0] && t <= Math.max(s[1], s[0] + 120));
      const run = d.runs.find((r) => t >= r[0] && t <= r[1]);
      if (!st && !run) { hide(); return; }
      tip.hidden = false;
      tip.textContent = st
        ? `H${st[3]} · ${st[2] === 'smart' ? T('ana.tip_smart') : T('ana.tip_manual')} · ${clock(st[0])}–${clock(st[1])} (${fmtDur(st[1] - st[0])})`
        : `${T('ana.tip_auto')} · ${clock(run[0])}–${clock(run[1])}`;
      tip.style.left = Math.min(Math.max(px / W * box.width - tip.offsetWidth / 2, 0), box.width - tip.offsetWidth) + 'px';
    };
    svg.onpointermove = move;
    svg.onpointerdown = move;
    svg.onpointerleave = hide;
  }

  // ---- numbers ----
  function fillSummary() {
    const d = state.data;
    const s = d.summary;
    const set = (key, text) => { $(`[data-ana="${key}"]`).textContent = text; };
    set('work', fmtDur(s.work_s));
    set('starts', String(s.starts));
    set('fuel', '≈ ' + litres(s.fuel_l));
    set('tmin', deg(s.temp.min));
    set('tavg', deg(s.temp.avg));
    set('tmax', deg(s.temp.max));
    set('level', s.avg_level === null ? '–' : 'H' + num(s.avg_level, 1));
    set('chamber', s.chamber_max === null ? '–' : Math.round(s.chamber_max) + ' °C');
    const known = s.by_mode.manual + s.by_mode.smart;
    set('smart', known > 0 ? T('ana.percent_time', {n: Math.round((s.by_mode.smart / known) * 100)}) : '–');
    $('#anaNote').textContent = T('ana.retention', {days: d.retention_days});
    let note = T('ana.fuel_note', {
      min: d.lph[0].toLocaleString(LOCALE), max: d.lph[5].toLocaleString(LOCALE),
      start: num(s.starts ? s.fuel_start_l / s.starts : 0.03, 2),
    });
    if (s.auto_s > 60) {
      note += T('ana.auto_note', {dur: fmtDur(s.auto_s), from: litres(s.fuel_auto_min_l), to: litres(s.fuel_auto_max_l)});
    }
    $('#anaFuelNote').textContent = note;
  }

  function fillBars() {
    const d = state.data;
    const box = $('#anaLevelBars');
    const max = Math.max(1, ...Object.values(d.summary.by_level).map((v) => v.seconds));
    box.replaceChildren(...Object.entries(d.summary.by_level).map(([level, v]) => {
      const row = document.createElement('div');
      row.className = 'bar-row';
      row.innerHTML = '<span class="bar-label"></span><div class="bar"><i></i></div><span class="bar-val"></span>';
      row.children[0].textContent = 'H' + level;
      row.querySelector('i').style.width = (v.seconds / max) * 100 + '%';
      row.children[2].textContent = v.seconds ? `${fmtDur(v.seconds)} · ${litres(v.litres)}` : '–';
      return row;
    }));
    const daily = $('#anaDaily');
    const dmax = Math.max(1, ...d.daily.map((r) => r.work_s));
    daily.replaceChildren(...d.daily.map((r) => {
      const row = document.createElement('div');
      row.className = 'bar-row day';
      row.innerHTML = '<span class="bar-label"></span><div class="bar"><i></i></div><span class="bar-val"></span>';
      const day = new Date(r.day + 'T12:00:00').toLocaleDateString(LOCALE, {weekday: 'short', day: '2-digit', month: '2-digit'});
      row.children[0].textContent = day;
      row.querySelector('i').style.width = (r.work_s / dmax) * 100 + '%';
      row.children[2].textContent = r.work_s ? `${fmtDur(r.work_s)} · ${litres(r.fuel_l)} · ${T('ana.starts_short', {n: r.starts})}` : '–';
      return row;
    }));
  }

  function render() {
    if (!state.data || !visible()) return;
    for (const b of $$('#anaRanges [data-h]')) b.classList.toggle('sel', Number(b.dataset.h) === state.hours);
    for (const b of $$('#anaMetrics [data-m]')) b.classList.toggle('sel', b.dataset.m === state.metric);
    fillSummary();
    fillBars();
    drawMetric();
    drawLevels();
  }

  async function load() {
    if (!state.device) return;
    const r = await api(`panel/analysis?device=${state.device}&hours=${state.hours}`);
    if (!r.ok) return;
    state.data = r.json;
    render();
  }

  async function loadDevices() {
    const r = await api('panel/state');
    if (!r.ok) return;
    const select = $('#anaDevice');
    const wanted = Number(store.get('device', 0)) || state.device;
    select.replaceChildren(...r.json.devices.map((dev) => {
      const o = document.createElement('option');
      o.value = dev.id;
      o.textContent = dev.name;
      return o;
    }));
    const ids = r.json.devices.map((dev) => dev.id);
    state.device = ids.includes(wanted) ? wanted : (ids[0] || null);
    if (state.device !== null) select.value = String(state.device);
  }

  async function open(on) {
    document.body.classList.toggle('view-analytics', on);
    $('#openAnalytics').textContent = on ? T('top.panel') : T('top.analytics');
    if (on) {
      if (location.hash !== '#analytics') history.replaceState(null, '', '#analytics');
      window.scrollTo(0, 0);
      await loadDevices();
      await load();
    } else if (location.hash === '#analytics') {
      history.replaceState(null, '', location.pathname + location.search);
    }
  }

  function init() {
    state.hours = Number(store.get('hours', 24)) || 24;
    state.metric = METRICS[store.get('metric', 'env_t')] ? store.get('metric', 'env_t') : 'env_t';
    $('#openAnalytics').addEventListener('click', () => open(!visible()));
    $('#anaDevice').addEventListener('change', (e) => { state.device = Number(e.target.value); store.set('device', state.device); state.data = null; load(); });
    $('#anaRanges').addEventListener('click', (e) => {
      const b = e.target.closest('[data-h]');
      if (!b) return;
      state.hours = Number(b.dataset.h);
      store.set('hours', state.hours);
      load();
    });
    $('#anaMetrics').addEventListener('click', (e) => {
      const b = e.target.closest('[data-m]');
      if (!b) return;
      state.metric = b.dataset.m;
      store.set('metric', state.metric);
      render();
    });
    let timer = null;
    window.addEventListener('resize', () => { clearTimeout(timer); timer = setTimeout(render, 150); });
    setInterval(() => { if (visible() && !document.hidden) load(); }, 60000);
    // a reload or a link to #analytics: open the view as soon as the panel is shown after the login check
    if (location.hash === '#analytics') {
      const wait = setInterval(() => {
        if (!$('#openAnalytics').hidden) { clearInterval(wait); open(true); }
      }, 200);
      setTimeout(() => clearInterval(wait), 8000);
    }
  }

  window.addEventListener('DOMContentLoaded', init);
})();