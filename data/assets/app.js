// === Theme ===
function toggleTheme() {
  document.body.classList.toggle('dark');
  const isDark = document.body.classList.contains('dark');
  const btn = document.getElementById('themeBtn');
  if (btn) btn.innerText = isDark ? '☀️' : '🌙';
  try { localStorage.setItem('aqua_theme', isDark ? 'dark' : 'light'); } catch (e) {}
}
(function initTheme() {
  try {
    if (localStorage.getItem('aqua_theme') === 'dark') {
      document.body.classList.add('dark');
      const btn = document.getElementById('themeBtn');
      if (btn) btn.innerText = '☀️';
    }
  } catch (e) {}
})();

// === Time sync: шлём время клиента, если на устройстве его нет ===
(function syncTime() {
  function send() {
    fetch('/api/time').then(r => r.json()).then(j => {
      if (!j.valid) {
        fetch('/api/time', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ epoch: Math.floor(Date.now() / 1000) })
        }).catch(() => {});
      }
    }).catch(() => {});
  }
  send();
  setInterval(send, 60000);
})();

// === PIN modal ===
(function injectPinModal() {
  if (document.getElementById('pinModal')) return;

  const style = document.createElement('style');
  style.textContent = `
    #pinModal { border:1px solid var(--border); background:var(--surface); color:var(--text);
      border-radius:12px; padding:20px; max-width:320px; margin:auto;
      box-shadow:0 10px 25px rgba(0,0,0,0.3); }
    #pinModal::backdrop { background:rgba(0,0,0,0.5); }
    #pinModal h3 { margin-bottom:8px; }
    #pinModal p  { font-size:13px; color:var(--text-dim); margin-bottom:12px; }
    #pinModal input { width:100%; background:var(--surface-subtle);
      border:1px solid var(--border); color:var(--text);
      padding:10px 12px; border-radius:6px; font-size:16px;
      letter-spacing:4px; text-align:center; box-sizing:border-box; }
    #pinModal .pin-err { color:var(--btn-reset); font-size:12px;
      margin-top:8px; min-height:14px; text-align:center; }
    #pinModal .dlg-btns { display:flex; gap:10px; margin-top:14px; justify-content:flex-end; }
  `;
  document.head.appendChild(style);

  const dlg = document.createElement('dialog');
  dlg.id = 'pinModal';
  dlg.innerHTML = `
    <h3>Введите PIN</h3>
    <p>Доступ к этой операции защищён PIN-кодом.</p>
    <input type="password" id="pinInput" inputmode="numeric"
           maxlength="8" placeholder="••••" autocomplete="off">
    <div class="pin-err" id="pinErr"></div>
    <div class="dlg-btns">
      <button class="dlg-btn" id="pinCancel">Отмена</button>
      <button class="dlg-btn dlg-btn-confirm" id="pinOk">Войти</button>
    </div>
  `;
  document.body.appendChild(dlg);

  dlg.querySelector('#pinCancel').onclick = () => {
    dlg.close();
    if (pinReject) pinReject(new Error('cancelled'));
    pinResolve = null; pinReject = null;
  };
  dlg.querySelector('#pinOk').onclick = () => submitPin();
  dlg.querySelector('#pinInput').addEventListener('keydown', e => {
    if (e.key === 'Enter') { e.preventDefault(); submitPin(); }
  });
})();

let pinResolve = null;
let pinReject  = null;

function askPin() {
  return new Promise((res, rej) => {
    pinResolve = res;
    pinReject  = rej;
    const dlg = document.getElementById('pinModal');
    const inp = document.getElementById('pinInput');
    document.getElementById('pinErr').innerText = '';
    inp.value = '';
    dlg.showModal();
    setTimeout(() => inp.focus(), 50);
  });
}

async function submitPin() {
  const pin = document.getElementById('pinInput').value.trim();
  const err = document.getElementById('pinErr');
  if (!/^\d{4,8}$/.test(pin)) { err.innerText = 'PIN: 4–8 цифр'; return; }

  try {
    const r = await fetch('/api/login', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ pin })
    });
    if (!r.ok) {
      err.innerText = 'Неверный PIN';
      document.getElementById('pinInput').value = '';
      document.getElementById('pinInput').focus();
      return;
    }
    document.getElementById('pinModal').close();
    if (pinResolve) pinResolve();
    pinResolve = null; pinReject = null;
  } catch (e) {
    err.innerText = 'Ошибка сети';
  }
}

// === API helper с авто-перехватом 401 ===
async function api(path, opts) {
  let r = await fetch(path, opts);
  if (r.status === 401) {
    try {
      await askPin();
    } catch (e) {
      throw new Error('cancelled');
    }
    r = await fetch(path, opts);
  }
  if (!r.ok) throw new Error(r.status + ' ' + r.statusText);
  return r.json();
}

// === INDEX: рендер состояния фильтров и статистики ===
const FILTER_COLORS = ['f1-col','f2-col','f3-col','f4-col','f5-col','f6-col'];

// Аура карточки по остатку ресурса: 0% — красная, 1-19% — розовая, 20-79% — голубая, 80-100% — зелёная
function auraClass(p) {
  if (p <= 0)          return 'aura-red';
  if (p < 20)          return 'aura-pink';
  if (p < 80)          return 'aura-cyan';
  return 'aura-green';
}

function renderFilters(filters) {
  const root = document.querySelector('.flasks-container');
  if (!root) return;
  root.innerHTML = filters.map((f, i) => `
    <div class="flask-card ${auraClass(f.percent)}">
      <div class="flask-num">#${f.id} ${f.name}</div>
      <div class="flask-visual">
        <div class="flask-level ${FILTER_COLORS[i] || 'f1-col'}" style="height:${f.percent.toFixed(0)}%;"></div>
      </div>
      <div class="flask-meta">
        <div class="meta-row"><span class="meta-label">Ресурс:</span><span class="meta-val">${Math.round(f.resource)} л</span></div>
        <div class="meta-row"><span class="meta-label">Остаток:</span><span class="meta-val">${Math.round(f.remain)} л</span></div>
        <div class="date-row">Замена: ${f.replace || '—'}</div>
      </div>
      <button class="btn-reset" onclick="confirmReset(${f.id}, '${f.name.replace(/'/g, "\\'")}')">Сброс</button>
    </div>
  `).join('');
}

function fmt(n, d = 1) { return Number(n).toFixed(d); }

function renderStats(s) {
  const cards = document.querySelectorAll('.stats-box .stat-card');
  if (cards.length < 5) return;
  cards[0].querySelector('.stat-val').innerText  = fmt(s.total) + ' л';
  cards[1].querySelector('.stat-val').innerText  = fmt(s.today) + ' л';
  cards[2].querySelector('.stat-val').innerText  = fmt(s.before) + ' л';
  cards[3].querySelector('.stat-val').innerText  = fmt(s.after) + ' л';
  cards[4].querySelector('.stat-val').innerHTML  =
    fmt(s.efficiency) + '% <span style="font-size:13px;font-weight:normal;color:var(--text-dim);">(1 л чистой на ' +
    (s.after > 0.01 ? fmt((s.before - s.after) / s.after, 2) : '—') + ' л дренажа)</span>';
  const badge = document.querySelector('.kpi-badge');
  if (badge) badge.innerText = fmt(s.efficiency) + '%';
  const since = cards[0].querySelector('.stat-sub');
  if (since && s.since) since.innerText = 'отсчет ведется с ' + s.since;
}

let _lastDevice = null;

function renderDevice(d) {
  const f = document.querySelector('footer');
  if (!f || !d) return;
  f.innerText = 'ESP32-C3 WebUI • ' + (d.time || '--/--/---- | --:--:--');
}

async function loadStatus() {
  try {
    const j = await api('/api/status');
    renderFilters(j.filters);
    renderStats(j.stats);
    _lastDevice = j.device;
    renderDevice(_lastDevice);
  } catch (e) {
    console.warn('status:', e);
  }
}

// === Reset filter ===
let activeFilterId = null;
function confirmReset(id, name) {
  activeFilterId = id;
  const t = document.getElementById('modalTitle');
  const m = document.getElementById('modalMsg');
  const dlg = document.getElementById('resetModal');
  if (!dlg) return;
  if (t) t.innerText = 'Сброс фильтра #' + id;
  if (m) m.innerText = 'Сбросить остаток для "' + name + '" до 100% и обновить дату последней замены на сегодняшнюю?';
  dlg.showModal();
}
function closeResetModal() {
  const dlg = document.getElementById('resetModal');
  if (dlg) dlg.close();
}
async function executeReset() {
  if (!activeFilterId) return;
  try {
    await api('/api/reset', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ id: activeFilterId })
    });
    closeResetModal();
    await loadStatus();
  } catch (e) {
    if (e.message !== 'cancelled') alert('Ошибка сброса: ' + e.message);
    closeResetModal();
  }
}

// === SERVICE page ===
async function loadFiltersForm() {
  const form = document.querySelector('form');
  if (!form || !form.querySelector('.filter-row')) return;
  try {
    const j = await api('/api/filters');
    const rows = form.querySelectorAll('.filter-row:not(.filter-header)');
    j.filters.forEach((f, i) => {
      const row = rows[i];
      if (!row) return;
      const [res, rem, rep] = row.querySelectorAll('input');
      if (res) res.value = Math.round(f.resource);
      if (rem) rem.value = Math.round(f.remain);
      if (rep && f.replace) rep.value = f.replace;
    });
  } catch (e) { console.warn('filters:', e); }
}

async function submitFiltersForm(e) {
  e.preventDefault();
  const form = e.target;
  const rows = form.querySelectorAll('.filter-row:not(.filter-header)');
  const filters = [];
  rows.forEach((row, i) => {
    const inputs = row.querySelectorAll('input');
    const res = inputs[0], rem = inputs[1], rep = inputs[2];
    const f = {
      id: i + 1,
      resource: parseFloat(res.value) || 0,
      remain:   parseFloat(rem.value) || 0,
      replace:  rep.value || ''
    };
    // Чекбокс "мембрана заменена" есть только в строке #4
    const kpdCb = document.getElementById('memKpdReset');
    if (kpdCb && row.contains(kpdCb)) f.memKpdReset = kpdCb.checked;
    filters.push(f);
  });
  try {
    await api('/api/filters', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ filters })
    });
    if (kpdNotified()) alert('Параметры фильтров сохранены, отсчёт КПД мембраны сброшен');
    else alert('Параметры фильтров сохранены');
  } catch (err) {
    if (err.message !== 'cancelled') alert('Ошибка сохранения: ' + err.message);
  }
}
function kpdNotified() {
  const cb = document.getElementById('memKpdReset');
  return !!(cb && cb.checked);
}

// === SETTINGS page ===
async function loadSettingsForm() {
  try {
    const j = await api('/api/settings');
    const apSsid = document.querySelector('input[value="AquaControl_AP"]');
    const staSsid = document.querySelector('input[value="Home_Network_2.4G"]');
    const authCb = document.getElementById('auth_enabled');
    const curIn  = document.getElementById('cur_k_in');
    const curPer = document.getElementById('cur_k_per');
    if (curIn)  curIn.innerText  = Number(j.inlet_k).toFixed(1);
    if (curPer) curPer.innerText = Number(j.permeate_k).toFixed(1);
    if (authCb) authCb.checked = !!j.auth_enabled;
    if (apSsid)  apSsid.value = j.ap.ssid;
    if (staSsid) staSsid.value = j.sta.ssid;
    if (document.getElementById('inlet_k'))    document.getElementById('inlet_k').value    = j.inlet_k;
    if (document.getElementById('permeate_k')) document.getElementById('permeate_k').value = j.permeate_k;
  } catch (e) { console.warn('settings:', e); }
}

async function saveCalib() {
  const inlet_k    = parseFloat(document.getElementById('inlet_k').value);
  const permeate_k = parseFloat(document.getElementById('permeate_k').value);
  if (!(inlet_k >= 1) || !(permeate_k >= 1)) { alert('Коэффициенты должны быть ≥ 1'); return; }
  try {
    await api('/api/settings/general', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ inlet_k, permeate_k })
    });
    alert('Калибровка сохранена');
  } catch (err) {
    if (err.message !== 'cancelled') alert('Ошибка: ' + err.message);
  }
}

async function saveAuth() {
  const enabled = document.getElementById('auth_enabled').checked;
  const pin     = document.getElementById('auth_pin').value.trim();

  if (enabled && pin && !/^\d{4,8}$/.test(pin)) {
    alert('PIN должен быть 4–8 цифр');
    return;
  }

  const body = { auth_enabled: enabled };
  if (pin) body.auth_pass = pin;

  try {
    await api('/api/settings/general', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body)
    });
    document.getElementById('auth_pin').value = '';
    alert('Сохранено');
  } catch (err) {
    if (err.message !== 'cancelled') alert('Ошибка: ' + err.message);
  }
}

async function resetStats(what) {
  const msg = what === 'total'
    ? 'Обнулить ВСЮ статистику (тотал + история 30 дней)? Ресурс фильтров не изменится.'
    : 'Обнулить счётчики за текущие сутки?';
  if (!confirm(msg)) return;
  try {
    await api('/api/stats/reset', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ what })
    });
    alert('Готово');
  } catch (e) {
    if (e.message !== 'cancelled') alert('Ошибка: ' + e.message);
  }
}

async function submitApForm(e) {
  e.preventDefault();
  const inputs = e.target.querySelectorAll('input');
  try {
    await api('/api/settings/ap', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ ssid: inputs[0].value, pass: inputs[1].value })
    });
    alert('Параметры точки доступа сохранены');
  } catch (err) {
    if (err.message !== 'cancelled') alert('Ошибка: ' + err.message);
  }
}

async function submitStaForm(e) {
  e.preventDefault();
  const inputs = e.target.querySelectorAll('input');
  try {
    await api('/api/settings/sta', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ ssid: inputs[0].value, pass: inputs[1].value })
    });
    alert('Параметры STA сохранены, идёт переподключение');
  } catch (err) {
    if (err.message !== 'cancelled') alert('Ошибка: ' + err.message);
  }
}

// === ANALYTICS page ===
async function loadAnalytics() {
  const root = document.querySelector('.metrics-grid');
  if (!root) return;
  try {
    const j = await api('/api/analytics');
    const nums = root.querySelectorAll('.metric-num');
    if (nums[0]) nums[0].innerText = fmt(j.total_in) + ' л';
    if (nums[1]) nums[1].innerText = fmt(j.drain) + ' л';
    if (nums[2]) nums[2].innerText = fmt(j.permeate) + ' л';
    if (nums[3]) nums[3].innerText = fmt(j.efficiency) + '%';
    if (nums[4]) nums[4].innerText = fmt(j.daily_avg_30d) + ' л / сутки';

    const fills = document.querySelectorAll('.bar-fill');
    const labels = document.querySelectorAll('.bar-labels span:nth-child(2)');
    const permPct = j.total_in > 0 ? (j.permeate / j.total_in * 100) : 0;
    const drainPct = 100 - permPct;
    if (fills[0]) fills[0].style.width = permPct.toFixed(1) + '%';
    if (fills[1]) fills[1].style.width = drainPct.toFixed(1) + '%';
    if (labels[0]) labels[0].innerText = fmt(j.permeate) + ' л (' + permPct.toFixed(1) + '%)';
    if (labels[1]) labels[1].innerText = fmt(j.drain) + ' л (' + drainPct.toFixed(1) + '%)';
  } catch (e) { console.warn('analytics:', e); }
}

// === Router ===
document.addEventListener('DOMContentLoaded', () => {
  // Service
  const serviceForm = document.querySelector('form');
  if (serviceForm && serviceForm.querySelector('.filter-row')) {
    loadFiltersForm();
    serviceForm.addEventListener('submit', submitFiltersForm);
  }
  
    // Settings
  const apForm  = document.getElementById('apForm');
  const staForm = document.getElementById('staForm');
  if (apForm || staForm || document.getElementById('inlet_k')) {
    loadSettingsForm();
    if (apForm)  apForm.addEventListener('submit', submitApForm);
    if (staForm) staForm.addEventListener('submit', submitStaForm);
  }
  // Analytics
  if (document.querySelector('.metrics-grid')) loadAnalytics();

  // Index: если есть флаконы — грузим статус и обновляем каждые 3 с
  if (document.querySelector('.flasks-container')) {
    loadStatus();
    setInterval(loadStatus, 3000);
  }
});
// Calibrating
let calibTimer = null;

async function calibRefresh() {
  try {
    const j = await api('/api/calib');
    const a = document.getElementById('calib_in');
    const b = document.getElementById('calib_per');
    if (a) a.value = j.inlet_pulses;
    if (b) b.value = j.permeate_pulses;
    if (j.running && !calibTimer) {
      calibTimer = setInterval(calibRefresh, 500);
    } else if (!j.running && calibTimer) {
      clearInterval(calibTimer);
      calibTimer = null;
    }
  } catch (e) {}
}

async function calibStart() {
  try {
    await api('/api/calib', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ action: 'start' })
    });
    calibRefresh();
  } catch (e) {
    if (e.message !== 'cancelled') alert('Ошибка: ' + e.message);
  }
}

async function calibStop() {
  try {
    await api('/api/calib', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ action: 'stop' })
    });
    calibRefresh();
  } catch (e) {
    if (e.message !== 'cancelled') alert('Ошибка: ' + e.message);
  }
}

async function calibSave() {
  const liters = parseFloat(document.getElementById('calib_liters').value);
  const inPulses  = parseFloat(document.getElementById('calib_in').value);
  const perPulses = parseFloat(document.getElementById('calib_per').value);

  if (!(liters > 0)) { alert('Укажите объём'); return; }
  if (inPulses <= 0 || perPulses <= 0) { alert('Сначала прокачайте воду'); return; }

  const inlet_k    = inPulses  / liters;
  const permeate_k = perPulses / liters;

  try {
    await api('/api/settings/general', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ inlet_k, permeate_k })
    });
    alert(`K вход=${inlet_k.toFixed(1)}, K пермеат=${permeate_k.toFixed(1)}`);
    loadSettingsForm();
  } catch (e) {
    if (e.message !== 'cancelled') alert('Ошибка: ' + e.message);
  }
}