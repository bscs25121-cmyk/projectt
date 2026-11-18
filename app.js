'use strict';
const $ = id => document.getElementById(id);
const W = 256, H = 128, CELL = 5, WW = 1280, WH = 640, K = 3.5556;
const PAL = ['', '#4a0d2c', '#7b1e4d', '#c2185b', '#d81b60', '#e0457b', '#f48fb1', '#ffc2da', '#fff0f6', '#ffd9c9', '#e6d0ff',
  '#b39ddb', '#ffffff', '#000000', '#6d6d6d', '#bdbdbd', '#ff5252', '#ff9800', '#ffeb3b', '#8bc34a', '#2e7d32', '#00bcd4',
  '#0288d1', '#1a237e', '#7e57c2', '#8d6e63', '#5d4037', '#ffccbc', '#ffab91', '#b2ebf2', '#c5e1a5', '#f8bbd0'];
const RGB = PAL.map(h => h ? [1, 3, 5].map(i => parseInt(h.substr(i, 2), 16)) : [0, 0, 0]);
const POLYS = [
  [[-168,66],[-152,70],[-125,70],[-95,69],[-82,73],[-80,63],[-65,60],[-56,52],[-67,44],[-76,35],[-81,25],[-90,29],[-97,26],[-97,20],[-88,21],[-84,15],[-79,9],[-85,10],[-92,14],[-105,20],[-110,24],[-115,30],[-118,34],[-124,40],[-124,48],[-135,58],[-148,61],[-165,60]],
  [[-55,60],[-45,60],[-20,70],[-18,80],[-40,83],[-65,80],[-70,76],[-58,68]],
  [[-80,9],[-72,12],[-62,10],[-50,0],[-35,-6],[-39,-15],[-48,-25],[-58,-35],[-65,-42],[-68,-52],[-74,-52],[-73,-40],[-71,-30],[-70,-18],[-76,-14],[-81,-5],[-79,2]],
  [[-17,21],[-10,30],[-6,36],[10,37],[20,32],[32,31],[35,28],[43,12],[51,12],[40,-3],[40,-15],[35,-25],[27,-34],[18,-34],[12,-18],[13,-6],[9,4],[-8,4],[-17,14]],
  [[-9,37],[-9,43],[-1,46],[-5,48],[3,51],[8,54],[10,58],[5,62],[15,69],[28,71],[45,68],[70,73],[100,77],[140,73],[170,69],[180,66],[165,60],[160,52],[142,53],[140,45],[130,42],[127,35],[122,30],[120,22],[108,20],[106,10],[103,1],[98,8],[97,17],[92,22],[87,21],[80,15],[77,8],[73,17],[67,25],[57,25],[56,26],[58,22],[52,16],[43,13],[35,28],[34,31],[36,36],[28,36],[26,40],[23,36],[19,42],[13,45],[16,40],[9,44],[3,43],[-5,36]],
  [[114,-22],[122,-18],[131,-12],[137,-12],[142,-11],[146,-19],[153,-26],[150,-37],[141,-38],[131,-31],[115,-34]],
  [[109,2],[117,7],[119,1],[116,-4],[110,-3]], [[95,5],[104,-3],[106,-6],[98,0]], [[130,32],[135,34],[141,38],[142,44],[138,38]],
  [[-5,50],[1,51],[-2,57],[-5,58],[-6,54]], [[44,-25],[49,-15],[50,-13],[47,-24]], [[172,-35],[178,-38],[175,-41],[167,-46],[171,-43]]];
const land = new Path2D();
POLYS.forEach(p => { p.forEach(([lo, la], i) => { const x = (lo + 180) * K, y = (90 - la) * K; i ? land.lineTo(x, y) : land.moveTo(x, y); }); land.closePath(); });

// ---------- state ----------
let token = localStorage.getItem('zixel_token') || '', myName = localStorage.getItem('zixel_name') || '';
let ver = 0, color = 4, mode = 'look', customMode = false, stamp = null, hover = null, sel = null, mine = null;
let dirty = true, pixDirty = false, polling = false, drag = null;
const custom = new Set(), cells = new Uint8Array(W * H), hud = { charges: 0, max: 10, nextIn: 0 };
const cv = $('map'), ctx = cv.getContext('2d'), cam = { s: 1, x: 0, y: 0 };
const pc = document.createElement('canvas'); pc.width = W; pc.height = H;
const pctx = pc.getContext('2d'), img = pctx.createImageData(W, H);

async function api(url, body) {
  const r = await fetch(url, body ? { method: 'POST', body: JSON.stringify(body) } : {});
  const j = await r.json().catch(() => ({}));
  if (!r.ok) { const e = new Error(j.error || 'Request failed'); e.status = r.status; throw e; }
  return j;
}
function toast(msg) { const t = $('toast'); t.textContent = msg; t.hidden = false; clearTimeout(toast.t); toast.t = setTimeout(() => t.hidden = true, 2600); }

// ---------- canvas data ----------
function setCell(x, y, c) {
  const i = y * W + x, o = i * 4, d = img.data; cells[i] = c;
  if (c) { const r = RGB[c]; d[o] = r[0]; d[o + 1] = r[1]; d[o + 2] = r[2]; d[o + 3] = 255; } else d[o + 3] = 0;
  pixDirty = dirty = true;
}
async function loadState() {
  const s = await api('/api/state');
  for (let i = 0; i < W * H; i++) setCell(i % W, (i / W) | 0, parseInt(s.cells[i], 32));
  ver = s.version;
}

// ---------- camera ----------
const minS = () => Math.min(innerWidth / WW, innerHeight / WH) * 0.5;
function clampCam() {
  cam.x = Math.max(100 - WW * cam.s, Math.min(innerWidth - 100, cam.x));
  cam.y = Math.max(100 - WH * cam.s, Math.min(innerHeight - 100, cam.y)); dirty = true;
}
function fit() { cam.s = Math.max(innerWidth / WW, 0.6 * innerHeight / WH); cam.x = (innerWidth - WW * cam.s) / 2; cam.y = (innerHeight - WH * cam.s) / 2; clampCam(); }
function zoomAt(sx, sy, f) {
  const ns = Math.max(minS(), Math.min(40, cam.s * f)), wx = (sx - cam.x) / cam.s, wy = (sy - cam.y) / cam.s;
  cam.s = ns; cam.x = sx - wx * ns; cam.y = sy - wy * ns; clampCam();
}
function flyTo(x, y, z = 14) { cam.s = z; cam.x = innerWidth / 2 - (x * CELL + CELL / 2) * z; cam.y = innerHeight / 2 - (y * CELL + CELL / 2) * z; clampCam(); }
function cellAt(sx, sy) {
  const x = Math.floor((sx - cam.x) / cam.s / CELL), y = Math.floor((sy - cam.y) / cam.s / CELL);
  return x >= 0 && y >= 0 && x < W && y < H ? { x, y } : null;
}
function resize() { const k = devicePixelRatio || 1; cv.width = innerWidth * k; cv.height = innerHeight * k; dirty = true; }

// ---------- drawing ----------
function outline(c, w, h, col) { ctx.strokeStyle = col; ctx.lineWidth = 2 / cam.s; ctx.strokeRect(c.x * CELL, c.y * CELL, w * CELL, h * CELL); }
function draw() {
  dirty = false;
  if (pixDirty) { pctx.putImageData(img, 0, 0); pixDirty = false; }
  const k = devicePixelRatio || 1;
  ctx.setTransform(1, 0, 0, 1, 0, 0); ctx.fillStyle = '#f7c6dd'; ctx.fillRect(0, 0, cv.width, cv.height);
  ctx.setTransform(k * cam.s, 0, 0, k * cam.s, k * cam.x, k * cam.y);
  ctx.fillStyle = '#fff3f8'; ctx.strokeStyle = '#e48ab3'; ctx.lineWidth = 1.5 / cam.s; ctx.lineJoin = 'round'; ctx.fill(land); ctx.stroke(land);
  ctx.imageSmoothingEnabled = false; ctx.drawImage(pc, 0, 0, WW, WH);
  if (cam.s * CELL >= 4) {
    ctx.strokeStyle = 'rgba(194,24,91,.14)'; ctx.lineWidth = 1 / cam.s; ctx.beginPath();
    for (let i = 0; i <= W; i++) { ctx.moveTo(i * CELL, 0); ctx.lineTo(i * CELL, WH); }
    for (let j = 0; j <= H; j++) { ctx.moveTo(0, j * CELL); ctx.lineTo(WW, j * CELL); }
    ctx.stroke();
  }
  if (stamp && hover) {
    ctx.globalAlpha = .75;
    for (const p of stamp.px) { const x = hover.x + p.dx, y = hover.y + p.dy; if (x < 0 || y < 0 || x >= W || y >= H) continue; ctx.fillStyle = PAL[p.c]; ctx.fillRect(x * CELL, y * CELL, CELL, CELL); }
    ctx.globalAlpha = 1; outline(hover, stamp.w, stamp.h, '#4a0d2c');
  } else if (hover) {
    if (mode === 'paint') { ctx.globalAlpha = .6; ctx.fillStyle = PAL[color]; ctx.fillRect(hover.x * CELL, hover.y * CELL, CELL, CELL); ctx.globalAlpha = 1; }
    outline(hover, 1, 1, '#4a0d2c');
  }
  if (sel) outline(sel, 1, 1, '#d81b60');
  placeTip();
}
(function loop() { if (dirty) draw(); requestAnimationFrame(loop); })();

// ---------- pixel info popup ----------
function placeTip() {
  const t = $('tip'); if (!sel || t.hidden) return;
  t.style.left = cam.x + (sel.x * CELL + CELL / 2) * cam.s + 'px'; t.style.top = cam.y + sel.y * CELL * cam.s + 'px';
}
function ago(s) { return s < 60 ? s + 's ago' : s < 3600 ? Math.floor(s / 60) + ' min ago' : Math.floor(s / 3600) + ' h ago'; }
async function inspect(c) {
  const r = await api(`/api/pixel?x=${c.x}&y=${c.y}`), t = $('tip');
  sel = c; t.replaceChildren();
  const a = document.createElement('b'), b = document.createElement('small');
  if (r.color) { a.textContent = 'Painted by ' + r.owner; b.textContent = ago(r.ago) + ' · part of a region of ' + (r.region >= 5000 ? '5000+' : r.region) + ' connected pixels'; }
  else { a.textContent = 'Empty pixel'; b.textContent = `(${c.x}, ${c.y})`; }
  t.append(a, b); t.hidden = false; dirty = true;
}

// ---------- painting ----------
async function paintAt(c) {
  try {
    const r = await api('/api/paint', { token, stamp: false, pixels: [[c.x, c.y, color]] });
    setCell(c.x, c.y, color); mine = c; hud.charges = r.charges; renderHud();
  } catch (e) { toast(e.message); if (e.status === 401) logout(); }
}
async function placeStamp(c) {
  const title = prompt('Name this artwork (optional, makes it searchable)', '') || '';
  const pixels = stamp.px.map(p => [c.x + p.dx, c.y + p.dy, p.c]).filter(p => p[0] >= 0 && p[1] >= 0 && p[0] < W && p[1] < H);
  try {
    const r = await api('/api/paint', { token, stamp: true, title, pixels });
    pixels.forEach(p => setCell(p[0], p[1], p[2])); mine = c; toast('Stamped ' + r.placed + ' pixels'); stamp = null; dirty = true;
  } catch (e) { toast(e.message); }
}
function clickCell(c) {
  if (!c || !token) return;
  if (stamp) placeStamp(c); else if (mode === 'paint') paintAt(c); else inspect(c).catch(() => {});
}

// ---------- image import (resize -> nearest palette colour) ----------
function nearest(r, g, b, allowed) {
  let best = allowed[0], bd = Infinity;
  for (const i of allowed) { const p = RGB[i], d = (p[0] - r) ** 2 + (p[1] - g) ** 2 + (p[2] - b) ** 2; if (d < bd) { bd = d; best = i; } }
  return best;
}
function importImage(file) {
  const im = new Image();
  im.onload = () => {
    const sc = 24 / Math.max(im.width, im.height), w = Math.max(1, Math.round(im.width * sc)), h = Math.max(1, Math.round(im.height * sc));
    const t = document.createElement('canvas'); t.width = w; t.height = h;
    const x = t.getContext('2d'); x.imageSmoothingEnabled = true; x.drawImage(im, 0, 0, w, h);
    const d = x.getImageData(0, 0, w, h).data, allowed = custom.size >= 2 ? [...custom] : PAL.map((_, i) => i).slice(1), px = [];
    for (let j = 0; j < h; j++) for (let i = 0; i < w; i++) {
      const o = (j * w + i) * 4; if (d[o + 3] < 128) continue;
      px.push({ dx: i, dy: j, c: nearest(d[o], d[o + 1], d[o + 2], allowed) });
    }
    stamp = { w, h, px }; toast('Click the map to place it (Esc cancels)'); dirty = true; URL.revokeObjectURL(im.src);
  };
  im.src = URL.createObjectURL(file);
}

// ---------- HUD ----------
function renderHud() {
  $('chg').textContent = `Pixels ${Math.floor(hud.charges)} / ${hud.max}`;
  $('fill').style.width = Math.min(100, hud.charges / hud.max * 100) + '%';
  const n = Math.ceil(hud.nextIn); $('next').textContent = hud.charges >= hud.max ? 'full' : `next in 0:${String(n).padStart(2, '0')}`;
}
function buildPalette() {
  const box = $('sw');
  for (let i = 1; i < PAL.length; i++) {
    const b = document.createElement('button'); b.className = 'sw' + (i === color ? ' sel' : ''); b.style.background = PAL[i]; b.setAttribute('aria-label', 'Color ' + i);
    b.onclick = () => {
      if (customMode) { custom.has(i) ? custom.delete(i) : custom.add(i); b.classList.toggle('pick'); }
      else { color = i; box.querySelectorAll('.sel').forEach(e => e.classList.remove('sel')); b.classList.add('sel'); }
    };
    box.appendChild(b);
  }
}

// ---------- live updates (poll every second) ----------
async function poll() {
  try {
    const r = await api(`/api/poll?since=${ver}&token=${token}`);
    if (r.reset) await loadState(); else { r.changes.forEach(([x, y, c]) => setCell(x, y, c)); ver = r.version; }
    hud.charges = +r.charges; hud.max = r.max; hud.nextIn = +r.nextIn;
    $('count').textContent = r.painted + ' pixels painted'; $('live').textContent = 'LIVE · ' + r.online;
    $('imp').title = r.stampIn > 0 ? `Next stamp in ${Math.ceil(r.stampIn)}s` : '';
    const ol = $('top5'); ol.replaceChildren();
    r.top.forEach(([n, c]) => { const li = document.createElement('li'); li.textContent = `${n} · ${c}`; ol.appendChild(li); });
    renderHud();
  } catch (e) { if (e.status === 401) { logout(); return; } }
  setTimeout(poll, 1000);
}

// ---------- login ----------
function logout() { token = ''; localStorage.removeItem('zixel_token'); polling = false; $('login').hidden = false; $('name').focus(); }
function start(name) { myName = name; $('who').textContent = name; $('login').hidden = true; if (!polling) { polling = true; poll(); } }
$('form').onsubmit = async e => {
  e.preventDefault(); $('err').textContent = '';
  try {
    const r = await api('/api/login', { name: $('name').value });
    token = r.token; localStorage.setItem('zixel_token', token); localStorage.setItem('zixel_name', r.name); start(r.name);
  } catch (er) { $('err').textContent = er.message; }
};

// ---------- events ----------
cv.addEventListener('pointerdown', e => { cv.setPointerCapture(e.pointerId); drag = { sx: e.clientX, sy: e.clientY, cx: cam.x, cy: cam.y, moved: false }; });
cv.addEventListener('pointermove', e => {
  hover = cellAt(e.clientX, e.clientY);
  if (drag) { const dx = e.clientX - drag.sx, dy = e.clientY - drag.sy; if (Math.abs(dx) + Math.abs(dy) > 4) drag.moved = true; if (drag.moved) { cam.x = drag.cx + dx; cam.y = drag.cy + dy; clampCam(); } }
  dirty = true;
});
cv.addEventListener('pointerup', e => { const d = drag; drag = null; if (d && !d.moved) clickCell(cellAt(e.clientX, e.clientY)); });
cv.addEventListener('pointerleave', () => { hover = null; dirty = true; });
cv.addEventListener('wheel', e => { e.preventDefault(); zoomAt(e.clientX, e.clientY, Math.pow(1.0015, -e.deltaY)); }, { passive: false });
addEventListener('resize', () => { resize(); clampCam(); });
addEventListener('keydown', e => { if (e.key === 'Escape') { stamp = null; sel = null; $('tip').hidden = true; dirty = true; } });
$('zin').onclick = () => zoomAt(innerWidth / 2, innerHeight / 2, 1.6);
$('zout').onclick = () => zoomAt(innerWidth / 2, innerHeight / 2, 1 / 1.6);
$('loc').onclick = () => mine ? flyTo(mine.x, mine.y) : fit();
$('paint').onclick = () => { mode = mode === 'paint' ? 'look' : 'paint'; $('paint').classList.toggle('on', mode === 'paint'); $('paint').textContent = mode === 'paint' ? 'Painting (tap to stop)' : 'Paint'; cv.classList.toggle('paint', mode === 'paint'); $('tip').hidden = true; sel = null; dirty = true; };
$('undo').onclick = async () => { try { await api('/api/undo', { token }); toast('Undone'); } catch (e) { toast(e.message); } };
$('imp').onclick = () => $('file').click();
$('file').onchange = e => { if (e.target.files[0]) importImage(e.target.files[0]); e.target.value = ''; };
$('cust').onclick = () => { customMode = !customMode; $('cust').classList.toggle('on', customMode); $('cust').textContent = 'Custom palette: ' + (customMode ? 'pick colors' : custom.size >= 2 ? custom.size + ' colors' : 'off'); };
let st; $('q').oninput = () => {
  clearTimeout(st);
  st = setTimeout(async () => {
    const q = $('q').value.trim(), box = $('results'); box.replaceChildren();
    if (!q) { box.hidden = true; return; }
    const res = await api('/api/search?q=' + encodeURIComponent(q)).catch(() => []);
    res.forEach(p => { const b = document.createElement('button'); b.textContent = p.name; b.onclick = () => { flyTo(p.x, p.y, 12); box.hidden = true; $('q').value = p.name; inspect({ x: p.x, y: p.y }).catch(() => {}); }; box.appendChild(b); });
    box.hidden = res.length === 0;
  }, 150);
};

// ---------- boot ----------
buildPalette(); resize(); fit();
loadState().then(() => { if (token && myName) start(myName); else $('name').focus(); }).catch(() => toast('Cannot reach the server'));
