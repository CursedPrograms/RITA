// RITA's own page, in step with her: what's on her screen, what she's doing, the fleet she sees.
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
