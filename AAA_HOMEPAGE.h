const char ECU_HOMEPAGE[] PROGMEM = R"=====(
<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>APsystems ECU</title>
<link rel="icon" href="/favicon.ico">
<link rel="stylesheet" href="/stylesheet">
</head>
<body>
<header class="topbar">
<a class="brand" href="/">APsystems ECU</a>
<nav class="nav">
<a class="button secondary" href="/energy">Energy history</a>
<a class="button secondary" href="/menu">Menu</a>
</nav>
</header>
<main class="page">
<section class="hero">
<div class="eyebrow">Solar production</div>
<h1>APsystems Fleet</h1>
<p id="statusText">Loading the latest cached telemetry...</p>
<div class="metrics">
<div class="metric">
<strong id="totalPower">&mdash;
</strong>
<span>Current output</span>
</div>
<div class="metric">
<strong id="todayEnergy">&mdash;
</strong>
<span>Energy today</span>
</div>
<div class="metric">
<strong id="lifetimeEnergy">&mdash;
</strong>
<span>Lifetime Energy</span>
</div>
</div>
</section>
<section>
<div class="card-head">
<div>
<div class="eyebrow">Inverters</div>
<h2>Current status</h2>
<p class="muted">
<span id="lastPoll">Last successful poll: not yet completed</span>
<br>
<span id="nextPoll">Next poll: checking...</span>
</p>
</div>
<span class="badge" id="pollBadge">Loading</span>
</div>
<div id="inverters" class="card-grid">
<div class="card empty">Loading inverter data...</div>
</div>
</section>
</main>
<script>
const q=s=>document.querySelector(s),fmt=(n,d=1)=>Number(n).toFixed(d),safe=n=>Number.isFinite(Number(n));

function statusBadge(ok,text){return `<span class="badge ${ok?'':'warn'}">${text}</span>`}
function inverterCard(n){let panels=(n.p||[]).slice(0,n.panel_count||2).map((p,i)=>`<div class="panel-power">
<span class="muted">Panel ${i+1}</span>
<br>
<strong>${safe(p)?fmt(p)+' W':p}</strong>
</div>`).join('');
return `<article class="card">
<div class="card-head">
<div>
<div class="eyebrow">Inverter ${n.index+1}</div>
<h2>${n.name||'Unnamed inverter'}</h2>
</div>${statusBadge(n.polled,n.polled?'Online':'No recent data')}</div>
<dl class="kv">
<dt>Current output</dt>
<dd>${safe(n.power_total)?fmt(n.power_total)+' W':'&mdash;'}</dd>
<dt>Today</dt>
<dd>${fmt(n.today_wh/1000,3)} kWh</dd>
<dt>Lifetime Energy</dt>
<dd>${fmt(n.lifetime_wh/1000,3)} kWh</dd>
<dt>Firmware</dt>
<dd>${n.firmware||'unknown'}</dd>
<dt>Transport</dt>
<dd>${n.encrypted?'AES encrypted':'Plain'}</dd>
<dt>Last inverter response</dt>
<dd>${n.last_success||'Not yet'}</dd>
</dl>
<div class="panel-grid">${panels}</div>
<div class="actions">
<a class="button secondary" href="/inverter-details?inv=${n.index}">Details</a>
<a class="button secondary" href="/energy?inv=${n.index}">History</a>
</div>
</article>`}
async function refresh(){try{let g=await fetch('/api/data?General=1',{cache:'no-store'}).then(r=>r.json()),items=await Promise.all(Array.from({length:g.cnt},(_,i)=>fetch('/api/data?Power=1&inv='+i,{cache:'no-store'}).then(r=>r.json()).then(n=>({...n,index:i}))));
let watts=items.reduce((s,n)=>s+Number(n.power_total||0),0),today=items.reduce((s,n)=>s+Number(n.today_wh||0),0),life=items.reduce((s,n)=>s+Number(n.lifetime_wh||0),0);
q('#totalPower').textContent=fmt(watts)+' W';
q('#todayEnergy').textContent=fmt(today/1000,3)+' kWh';
q('#lifetimeEnergy').textContent=fmt(life/1000,3)+' kWh';
q('#inverters').innerHTML=items.length?items.map(inverterCard).join(''):'<div class="card empty">No inverters configured. Open Menu to add one.</div>';
q('#pollBadge').textContent=g.polling?(g.poll_in_progress?'Polling now':g.poll_interval+' s interval'):'Automatic polling off';
q('#pollBadge').className='badge '+(g.polling?'':'warn');
q('#statusText').textContent=g.st===1?'Radio ready; showing the latest cached values.':'Radio is not ready.';
q('#lastPoll').textContent='Last successful fleet poll: '+(g.last_poll_success||'not yet completed');
q('#nextPoll').textContent='Next poll: '+(g.next_poll||(g.polling?'pending valid time or daylight window':'automatic polling disabled'));
}catch(e){q('#statusText').textContent='Unable to load ECU data.';
q('#pollBadge').textContent='Unavailable';
q('#pollBadge').className='badge warn'}}
refresh();
setInterval(refresh,15000);

</script>
</body>
</html>
)=====";
