// web_page.h - RITA's own web page. Written by scripts/make_web.py from scripts/esp32/web/:
// edit the files there, not this.
#pragma once
#include <Arduino.h>

const char WEB_INDEX[] PROGMEM = R"=====(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>RITA</title>
  <link rel="stylesheet" href="/style.css">
</head>
<body>
  <header>
    <img src="/avatar.jpg" alt="RITA" class="avatar">
    <div>
      <h1>RITA <span id="rift" class="rift"><span class="dot"></span>RIFT</span></h1>
      <p class="sub">Remote Interface for Total Autonomy</p>
      <p id="net" class="net">...</p>
    </div>
  </header>

  <section class="panel now">
    <h2>On her screen now</h2>
    <p id="screen" class="screen">...</p>
    <p id="doing" class="doing"></p>
    <p id="toast" class="toast"></p>
  </section>

  <section class="panel">
    <h2>Her</h2>
    <dl id="her"></dl>
  </section>

  <section class="panel">
    <h2>The fleet</h2>
    <table id="fleet">
      <thead><tr><th>Robot</th><th>Temp</th><th>Humidity</th><th>Distance</th><th>Battery</th><th>WiFi</th><th></th></tr></thead>
      <tbody></tbody>
    </table>
    <p class="note">From each robot's own <code>/status</code>, asked one at a time while this page is open. Driving stays on her touchscreen.</p>
  </section>

  <section class="panel" id="hostedPanel" hidden>
    <h2>On her network</h2>
    <ul id="hosted"></ul>
  </section>

  <script src="/app.js"></script>
</body>
</html>
)=====";

const char WEB_STYLE[] PROGMEM = R"=====(/* RITA's own page: her colours (colour_scheme.xml) */
:root {
  --background: #10151D; --panel: #19212E; --border: #2E3B4E; --text: #EFE9D2;
  --text-sec: #B6B097; --text-dim: #7C7A68; --accent: #E0702A; --accent2: #4FB0A5;
  --ok: #50D282; --bad: #EB505A; --warn: #F0AA3C;
}
* { box-sizing: border-box; margin: 0; padding: 0; }
body { background: var(--background); color: var(--text); font-family: 'Segoe UI', Tahoma, sans-serif; padding: 20px; max-width: 900px; margin: 0 auto; }
header { display: flex; align-items: center; gap: 18px; margin-bottom: 20px; }
.avatar { width: 96px; height: 96px; border-radius: 50%; border: 2px solid var(--accent); object-fit: cover; }
h1 { color: var(--accent); font-size: 2em; }
.sub { color: var(--text-sec); }
.net { margin-top: 6px; color: var(--accent2); }
.panel { background: var(--panel); border: 1px solid var(--border); border-radius: 12px; padding: 16px; margin-bottom: 16px; }
h2 { font-size: 1.1em; color: var(--accent); margin-bottom: 10px; }
dl { display: grid; grid-template-columns: max-content 1fr; gap: 4px 16px; }
dt { color: var(--text-dim); }
table { width: 100%; border-collapse: collapse; }
th { text-align: left; color: var(--text-dim); font-weight: normal; padding: 4px 6px; border-bottom: 1px solid var(--border); }
td { padding: 6px; border-bottom: 1px solid var(--border); }
td.dim { color: var(--text-dim); }
.dot { display: inline-block; width: 10px; height: 10px; border-radius: 50%; margin-right: 8px; background: var(--bad); }
.dot.on { background: var(--ok); }
.dot.quiet { background: var(--warn); }
a { color: var(--accent2); text-decoration: none; }
a:hover { text-decoration: underline; }
.note { margin-top: 10px; color: var(--text-dim); font-size: 0.85em; }
code { color: var(--text-sec); }
ul { list-style: none; }
li { padding: 3px 0; }
@media (max-width: 600px) { th:nth-child(3), td:nth-child(3) { display: none; } }
.now .screen { font-size: 1.3em; }
.now .doing { color: var(--accent2); margin-top: 4px; }
.now .toast { color: var(--text-sec); margin-top: 4px; font-style: italic; }
.btn { display: inline-block; padding: 4px 12px; border: 1px solid var(--accent); border-radius: 8px; color: var(--accent); white-space: nowrap; }
.btn:hover { background: var(--accent); color: var(--background); text-decoration: none; }
.rift { font-size: 0.45em; color: var(--text-dim); vertical-align: middle; margin-left: 10px; font-weight: normal; }
)=====";

const char WEB_APP[] PROGMEM = R"=====(// RITA's own page, in step with her: what's on her screen, what she's doing, the fleet she sees.
// /api/status every second.
const $ = id => document.getElementById(id);

function cell(text) {
  const td = document.createElement('td');
  td.textContent = text || '--';
  if (!text) td.className = 'dim';
  return td;
}

function uptime(s) {
  const h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60);
  return h ? `${h} h ${m} min` : `${m} min ${s % 60} s`;
}

async function refresh() {
  let d;
  try { d = await (await fetch('/api/status', { cache: 'no-store' })).json(); }
  catch { $('net').textContent = "can't reach her"; return; }

  $('rift').querySelector('.dot').className = 'dot' + (d.rift ? ' on' : '');   // as on her screen's bar
  $('screen').textContent = d.screen;
  $('doing').textContent = d.doing || '';
  $('toast').textContent = d.toast ? `"${d.toast}"` : '';
  $('net').textContent = d.hosting ? `Hosting "RITA" (${d.hosted.length} on it)` : d.net ? `On ${d.net}` : 'Looking for the fleet';
  const her = $('her');
  her.textContent = '';
  for (const [k, v] of [['Address', d.ip], ['Up', uptime(d.uptime_s)], ['Free memory', `${Math.round(d.heap / 1024)} KB`],
                        ['SD card', d.sd ? 'yes: DREAM\'s clips' : 'none'], ['Sound', d.sound ? 'on' : 'off']]) {
    const dt = document.createElement('dt'); dt.textContent = k;
    const dd = document.createElement('dd'); dd.textContent = v;
    her.append(dt, dd);
  }

  const body = $('fleet').querySelector('tbody');
  body.textContent = '';
  for (const r of d.robots) {
    const tr = document.createElement('tr');
    const name = document.createElement('td');
    const dot = document.createElement('span');
    dot.className = 'dot' + (r.answered ? ' on' : r.ip ? ' quiet' : '');
    name.append(dot, r.label);
    tr.append(name, cell(r.temp), cell(r.hum), cell(r.dist), cell(r.batt), cell(r.wifi));
    const link = document.createElement('td');
    if (r.ip && r.page) {
      const a = document.createElement('a');
      a.href = r.page; a.target = '_blank'; a.rel = 'noopener';
      a.className = 'btn';
      a.textContent = r.name === 'DREAM' ? 'Open dashboard' : 'Open site';
      link.append(a);
    }
    tr.append(link);
    body.append(tr);
  }

  $('hostedPanel').hidden = !d.hosting;
  const ul = $('hosted');
  ul.textContent = '';
  for (const h of d.hosted || []) {
    const li = document.createElement('li');
    li.textContent = `${h.name} (${h.type}) ${h.ip}`;
    ul.append(li);
  }
}

refresh();
setInterval(refresh, 1000);
)=====";

