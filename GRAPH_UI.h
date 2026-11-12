#pragma once
const char GRAPH_SCRIPT[] PROGMEM = R"graphjs(
/* Local SVG renderer: no CDN, cookies, chart library or browser history store. */
const PowerHistory = (() => {
  const niceMax = value => {
    if (!(value > 0)) return 100;
    const power = 10 ** Math.floor(Math.log10(value)), n = value / power;
    return (n <= 1 ? 1 : n <= 2 ? 2 : n <= 5 ? 5 : 10) * power;
  };
  function pointsFromDays(days, from, to) {
    const points = [];
    for (const day of days) day.values.forEach((value, i) => {
      const time = day.day * 86400 + i * day.step;
      if (time >= from && time <= to) points.push({time, value, step: day.step});
    });
    return points.sort((a, b) => a.time - b.time);
  }
  function segments(points) {
    const runs = []; let run = [], previous;
    for (const point of points) {
      if (point.value === null || (previous && point.time - previous.time > previous.step * 1.5)) {
        if (run.length) runs.push(run); run = [];
      }
      if (point.value !== null) run.push(point);
      previous = point;
    }
    if (run.length) runs.push(run);
    return runs;
  }
  function chartSvg(points, from, to) {
    const width = 1000, height = 320, left = 64, right = 20, top = 24, bottom = 48;
    const baseline = height - bottom, plotWidth = width - left - right;
    const maximum = niceMax(Math.max(0, ...points.map(p => p.value || 0)));
    const x = time => left + (time - from) / (to - from) * plotWidth;
    const y = value => baseline - value / maximum * (baseline - top);
    let markup = `<svg viewBox="0 0 ${width} ${height}" role="img" aria-label="Power production in watts over time"><defs><linearGradient id="powerFill" x1="0" y1="0" x2="0" y2="1"><stop offset="0%" stop-color="#47dfb0" stop-opacity=".28"/><stop offset="100%" stop-color="#47dfb0" stop-opacity=".02"/></linearGradient></defs>`;
    for (let tick = 0; tick <= 4; tick++) {
      const value = maximum * tick / 4, yy = y(value);
      markup += `<line x1="${left}" x2="${width-right}" y1="${yy}" y2="${yy}" stroke="#2a3a55" stroke-dasharray="3 5"/><text x="${left-12}" y="${yy+4}" text-anchor="end">${value >= 1000 ? (value/1000).toFixed(1)+'k' : Math.round(value)}</text>`;
    }
    const options = to-from <= 86400 ? {hour:'numeric',minute:'2-digit'} : {month:'short',day:'numeric',hour:'numeric'};
    for (let tick = 0; tick <= 4; tick++) {
      const time = from + (to-from) * tick/4;
      markup += `<text x="${x(time)}" y="${height-16}" text-anchor="${tick===0?'start':tick===4?'end':'middle'}">${new Date(time*1000).toLocaleString([],options)}</text>`;
    }
    markup += '<text x="12" y="15">W</text>';
    for (const run of segments(points)) {
      const line = run.map((p,i) => `${i?'L':'M'}${x(p.time).toFixed(2)},${y(p.value).toFixed(2)}`).join(' ');
      if (run.length > 1) markup += `<path d="${line} L${x(run[run.length-1].time)},${baseline} L${x(run[0].time)},${baseline} Z" fill="url(#powerFill)"/><path d="${line}" fill="none" stroke="#47dfb0" stroke-width="2.5" vector-effect="non-scaling-stroke"/>`;
      else markup += `<circle cx="${x(run[0].time)}" cy="${y(run[0].value)}" r="3" fill="#47dfb0"/>`;
    }
    return markup + '<line id="powerCursor" y1="24" y2="272" stroke="#c6f7e8" opacity="0" stroke-dasharray="4 4"/></svg>';
  }
  async function mount(root) {
    root.innerHTML = `<style>
      #powerHistory{overflow:hidden}#powerHistory .ph-top{display:flex;justify-content:space-between;gap:16px;align-items:center;flex-wrap:wrap}
      #powerHistory h2{margin:0}#powerHistory .ph-kicker{font-size:.74rem;letter-spacing:.12em;text-transform:uppercase;color:#47dfb0;margin-bottom:7px}
      #powerHistory .ph-controls{display:flex;gap:12px;flex-wrap:wrap;margin:22px 0 6px}#powerHistory .ph-controls label{font-size:.8rem;color:#9fb0c8}
      #powerHistory .ph-controls select{min-width:160px;margin-top:5px}#powerHistory .ph-plot{min-height:180px;position:relative}
      #powerHistory svg{width:100%;display:block;touch-action:auto}#powerHistory svg text{fill:#9fb0c8;font:12px system-ui}
      #powerHistory .ph-foot{display:flex;justify-content:space-between;gap:16px;flex-wrap:wrap;border-top:1px solid #2a3a55;padding-top:14px;color:#9fb0c8;font-size:.82rem}
      #powerHistory .ph-note{font-size:.85rem;color:#9fb0c8;margin:10px 0}#powerHistory .ph-tip{min-height:24px;font-variant-numeric:tabular-nums;color:#c6f7e8}
      #powerHistory .ph-empty{text-align:center;padding:35px 12px;color:#9fb0c8}#powerHistory button{white-space:normal}
      @media(max-width:600px){#powerHistory .ph-plot{overflow-x:auto}#powerHistory svg{min-width:660px}}
      </style><div class="ph-top"><div><div class="ph-kicker">Production detail</div><h2>Power over time</h2></div><button id="ph-toggle" class="secondary" type="button" disabled>Loading...</button><button id="ph-refresh" class="secondary" type="button">Refresh</button></div>
      <div id="ph-confirm" role="group" aria-label="Confirm clearing detailed history" hidden><p>Turn off detailed history and delete its saved power graphs? Daily energy totals and MQTT/Modbus reporting will be kept.</p><button id="ph-cancel" class="secondary" type="button">Cancel</button> <button id="ph-clear" class="danger" type="button">Turn off and clear</button></div>
      <p id="ph-message" class="ph-note" role="status">Reading history settings...</p>
      <div id="ph-enabled" hidden><div class="ph-controls"><label>Time range<select id="ph-range"><option value="1">Last 24 hours</option><option value="3" selected>Last 3 days</option><option value="10">Last 10 days</option><option value="30">Last 30 days</option></select></label><label>Production source<select id="ph-source"><option value="-1">All inverters</option></select></label></div>
      <div id="ph-tip" class="ph-tip" aria-live="polite"></div><div id="ph-plot" class="ph-plot"></div><div class="ph-foot"><span id="ph-resolution"></span><span id="ph-storage"></span></div>
      <p class="ph-note">Gaps mean no recorded reading, not zero production. Combined power requires readings from every inverter. Times use your browser's time zone.</p></div>
      <p class="ph-note">Optional local history: recent 3 days at polling resolution (minimum 1 minute), then at least 5 minutes through day 10 and 1 hour through day 30. Saved daily at 00:00 UTC; an unexpected restart loses the current unsaved day. Daily energy totals below are separate.</p>`;
    const find = id => root.querySelector('#ph-'+id);
    let enabled = false, busy = false, saving = false, confirming = false, timer, status, stopped = false;
    async function request(url, options, decode) {
      const controller = new AbortController(), timeout = setTimeout(() => controller.abort(),10000);
      try { return await decode(await fetch(url,{...options,signal:controller.signal})); }
      catch(error) {
        if (controller.signal.aborted) throw new Error('History request timed out. Refresh to check the current state.');
        throw error;
      } finally { clearTimeout(timeout); }
    }
    async function json(url) {
      return request(url,{cache:'no-store'},response => {
        if (!response.ok) throw new Error(response.status === 401 ? 'Sign in to read history.' : 'History request failed ('+response.status+').');
        return response.json();
      });
    }
    async function refresh() {
      if (busy || saving || confirming || stopped) return;
      busy = true; clearTimeout(timer);
      find('refresh').disabled = true;
      for (const id of ['source','range','toggle']) find(id).disabled = true;
      try {
        status = await json('/api/graph'); enabled = status.enabled;
        find('toggle').textContent = enabled ? 'Turn off & clear detailed history' : 'Enable detailed history';
        find('enabled').hidden = !enabled;
        find('message').textContent = status.problem || (status.pending ? 'Applying setting...' : enabled ? 'Recording locally. MQTT and Modbus continue unchanged.' : 'Off. Enable to collect detailed graphs; no history is backfilled. MQTT and Modbus need no local graph storage.');
        const selected = find('source').value;
        find('source').replaceChildren(new Option('All inverters','-1'), ...status.inverters.map((name,i) => new Option(name,String(i))));
        if ([...find('source').options].some(o=>o.value===selected)) find('source').value = selected;
        if (enabled) {
          const span = Number(find('range').value)*86400, to = status.utc, from = to-span;
          const days = []; let damaged = 0;
          // Sequential requests limit both ESP heap and simultaneous file handles.
          for (const day of status.days.filter(d => (d.day+1)*86400 > from && d.day*86400 <= to)) {
            try { days.push(await json('/api/graph/day?day='+day.day+'&inv='+find('source').value)); }
            catch(error) { damaged++; }
          }
          const points = pointsFromDays(days,from,to), present = points.filter(p=>p.value!==null);
          find('plot').innerHTML = present.length ? chartSvg(points,from,to) : '<div class="ph-empty">No readings in this range yet. History starts with the next successful poll.</div>';
          find('tip').textContent = present.length ? 'Point or touch the graph to inspect a reading.' : '';
          find('storage').textContent = (status.bytes/1024).toFixed(1)+' KiB stored | '+(status.ram_bytes/1024).toFixed(1)+' KiB RAM';
          const steps = [...new Set(days.map(d=>d.step/60))].sort((a,b)=>a-b);
          find('resolution').textContent = steps.length ? 'Intervals: '+steps.map(s=>s+' min').join(' / ')+' | '+present.length+' readings' : 'Waiting for readings';
          if (damaged) find('message').textContent = damaged+' history day(s) unavailable. Remaining data is shown. '+(status.problem||'');
          const svg = find('plot').querySelector('svg');
          if (svg) {
            const inspect = event => {
              const rect = svg.getBoundingClientRect(), xx = (event.clientX-rect.left)/rect.width*1000;
              const time = from + Math.max(0,Math.min(1,(xx-64)/916))*span;
              let closest = points[0];
              for (const point of points) if (Math.abs(point.time-time)<Math.abs(closest.time-time)) closest=point;
              const distance = Math.abs(closest.time-time);
              find('tip').textContent = distance > closest.step ? new Date(time*1000).toLocaleString()+' | No reading' : new Date(closest.time*1000).toLocaleString()+' | '+(closest.value===null?'No reading':closest.value.toLocaleString()+' W')+' | '+closest.step/60+' min interval';
              const cursor = svg.querySelector('#powerCursor'); cursor.setAttribute('x1',xx); cursor.setAttribute('x2',xx); cursor.setAttribute('opacity',xx>=64&&xx<=980?'1':'0');
            };
            svg.addEventListener('pointerdown', inspect, {passive:true});
            svg.addEventListener('pointermove', inspect, {passive:true});
          }
        }
      } catch(error) {
        status = undefined;
        find('message').textContent = error.message;
        find('toggle').textContent = 'History settings unavailable';
      }
      finally {
        busy = false; find('refresh').disabled = false;
        for (const id of ['source','range']) find(id).disabled = false;
        find('toggle').disabled = !status || !!status.pending;
        timer = setTimeout(refresh, status?.pending ? 1500 : Math.max(60,status?.poll_seconds||300)*1000);
      }
    }
    async function saveSetting() {
      if (busy || saving || !status || status.pending || stopped) return;
      confirming = false; find('confirm').hidden = true;
      saving = true; clearTimeout(timer);
      for (const id of ['toggle','refresh','source','range']) find(id).disabled = true;
      try {
        await request('/api/graph/settings',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'enabled='+(enabled?'0':'1')},response => {
          if (!response.ok) throw new Error(response.status===401?'Administrator sign-in is required.':'Could not save history settings. Refresh to check the current state.');
        });
        status = {...status,pending:true};
        find('message').textContent = 'Applying setting...';
        clearTimeout(timer); timer = setTimeout(refresh,1500);
      } catch(error) {
        status = undefined;
        find('message').textContent = error.message;
        timer = setTimeout(refresh,300000);
      } finally {
        saving = false;
        for (const id of ['refresh','source','range']) find(id).disabled = false;
        find('refresh').focus();
      }
    }
    find('toggle').addEventListener('click', () => {
      if (busy || saving || confirming || !status || status.pending || stopped) return;
      if (!enabled) return saveSetting();
      confirming = true; clearTimeout(timer);
      find('confirm').hidden = false;
      for (const id of ['toggle','refresh','source','range']) find(id).disabled = true;
      find('cancel').focus();
    });
    const cancelClear = () => {
      confirming = false; find('confirm').hidden = true;
      for (const id of ['toggle','refresh','source','range']) find(id).disabled = false;
      find('toggle').focus();
      timer = setTimeout(refresh,Math.max(60,status?.poll_seconds||300)*1000);
    };
    find('cancel').addEventListener('click',cancelClear);
    find('confirm').addEventListener('keydown',event => { if(event.key==='Escape') cancelClear(); });
    find('clear').addEventListener('click',() => { if(confirming) return saveSetting(); });
    for (const id of ['source','range']) find(id).addEventListener('change',refresh);
    find('refresh').addEventListener('click',refresh);
    window.addEventListener('pagehide',()=>{stopped=true;clearTimeout(timer);});
    await refresh();
  }
  return {niceMax,pointsFromDays,segments,chartSvg,mount};
})();
if (typeof module !== 'undefined') module.exports = PowerHistory;
if (typeof document !== 'undefined') PowerHistory.mount(document.getElementById('powerHistory'));
)graphjs";
