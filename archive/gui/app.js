import { lineChart, barChart, pieChart } from './charts.js';

/* =================== Config =================== */
const POLL_MS = 300;                 // 200–500 ms
const REQ_TIMEOUT_MS = 1200;
const LEAK_THRESHOLD_MS = 30000;     // Debe coincidir con backend (por defecto 30s)
document.getElementById('leak-th-ms').textContent = LEAK_THRESHOLD_MS.toString();

/* =============== Helpers generales =============== */
const $ = sel => document.querySelector(sel);
const $$ = sel => Array.from(document.querySelectorAll(sel));

function bytesToMB(b){ return (b / (1024*1024)); }
function fmtMB(b){ return bytesToMB(b).toFixed(2) + ' MB'; }
function fmtPct(x){ return (x*100).toFixed(2) + '%'; }
function safe(obj, path, dflt){ try{ return path.split('.').reduce((o,k)=>o[k], obj) ?? dflt; }catch{ return dflt; } }

async function fetchJSON(path){
    const ctl = new AbortController();
    const t = setTimeout(()=>ctl.abort(), REQ_TIMEOUT_MS);
    try{
        const r = await fetch(path, {signal: ctl.signal, cache: 'no-store'});
        if (!r.ok) throw new Error(r.status + ' ' + r.statusText);
        return await r.json();
    } finally { clearTimeout(t); }
}

function setStatus(ok){
    const el = $('#status-badge');
    if (ok){ el.textContent = 'En línea'; el.className = 'badge badge-ok'; }
    else { el.textContent = 'Conectando…'; el.className = 'badge badge-warn'; }
}

/* =============== Tabs accesibles =============== */
function setupTabs(){
    const tabs = $$('.tab');
    const panes = $$('.tabpane');
    tabs.forEach(btn=>{
        btn.addEventListener('click', ()=>{
            tabs.forEach(b=>{ b.classList.remove('active'); b.setAttribute('aria-selected','false'); });
            panes.forEach(p=>p.classList.remove('active'));
            btn.classList.add('active'); btn.setAttribute('aria-selected','true');
            const pane = $('#'+btn.getAttribute('aria-controls'));
            pane.classList.add('active');

            // 🔁 Re-render del tab activo (por si estuvo oculto y los canvas tenían w=0)
            if (pane.id === 'tab-general') {
                renderTimeline(lastTimeline);
            } else if (pane.id === 'tab-archivo') {
                renderFileTable(lastFileStats);
            } else if (pane.id === 'tab-leaks') {
                renderLeaksCharts();
            }
            // (Mapa es tabla; no necesita redibujar canvas)
        });
    });
}

// Redibuja lo visible si el usuario cambia el tamaño de la ventana
window.addEventListener('resize', ()=>{
    const active = $('.tabpane.active')?.id;
    if (active === 'tab-general') renderTimeline(lastTimeline);
    else if (active === 'tab-archivo') renderFileTable(lastFileStats);
    else if (active === 'tab-leaks') renderLeaksCharts();
});

/* =============== Estado in-memory (UI) =============== */
let lastTimeline = []; // [{t_ns,current_bytes,leak_bytes}]
let lastBlocks = [];   // cache para edades/estados
let lastFileStats = {}; // {file: {alloc_count,alloc_bytes,live_count,live_bytes}}
let lastLeaks = {};     // KPIs para leaks

/* =============== Render: GENERAL =============== */
function renderKPIs(metrics){
    $('#kpi-current').textContent = fmtMB(metrics.current_bytes||0);
    $('#kpi-active').textContent  = (metrics.active_allocs||0).toString();
    $('#kpi-leaks').textContent   = fmtMB(metrics.leak_bytes||0);
    $('#kpi-peak').textContent    = fmtMB(metrics.peak_bytes||0);
    $('#kpi-total').textContent   = (metrics.total_allocs||0).toString();
}

function renderTimeline(timeline){
    // Guardamos para la pestaña de leaks también
    lastTimeline = timeline || [];
    const curMB = lastTimeline.map(p=> bytesToMB(p.current_bytes||0));
    const lkMB  = lastTimeline.map(p=> bytesToMB(p.leak_bytes||0));
    const canvas = $('#chart-timeline');
    const ctx = canvas.getContext('2d'); ctx.clearRect(0,0,canvas.width,canvas.height);
    lineChart(canvas, null, [
        { name:'current', values: curMB },
        { name:'leaks',   values: lkMB  },
    ]);
}

function renderTop3(fileStats){
    // fileStats: obj -> array ordenada por live_bytes desc
    const rows = Object.entries(fileStats)
        .map(([file,st])=>({file, ...st}))
        .sort((a,b)=> (b.live_bytes||0)-(a.live_bytes||0))
        .slice(0,3);
    const tbody = $('#tbl-top3'); tbody.innerHTML = '';
    rows.forEach(r=>{
        const tr = document.createElement('tr');
        tr.innerHTML = `<td title="${r.file}">${r.file}</td>
                    <td>${r.alloc_count}</td>
                    <td>${bytesToMB(r.live_bytes).toFixed(2)}</td>`;
        tbody.appendChild(tr);
    });
}

/* =============== Render: MAPA =============== */
function classifyBlockAge(ts_ns){
    const now_ns = BigInt(Date.now()) * 1000000n;
    const age_ms = Number( (now_ns - BigInt(ts_ns||0)) / 1000000n );
    let state = 'normal';
    if (age_ms > LEAK_THRESHOLD_MS*2) state = 'posible leak';
    else if (age_ms > LEAK_THRESHOLD_MS) state = 'envejecido';
    return {age_ms, state};
}
function renderBlocks(blocks){
    lastBlocks = blocks || [];
    // Orden
    const sel = $('#blocks-sort').value;
    const arr = [...lastBlocks];
    if (sel==='ts_desc') arr.sort((a,b)=> (b.ts_ns||0)-(a.ts_ns||0));
    else if (sel==='size_desc') arr.sort((a,b)=> (b.size||0)-(a.size||0));
    else if (sel==='size_asc') arr.sort((a,b)=> (a.size||0)-(b.size||0));

    const tbody = $('#tbl-blocks'); tbody.innerHTML = '';
    const frag = document.createDocumentFragment();
    for (const b of arr){
        const {age_ms, state} = classifyBlockAge(b.ts_ns);
        const badgeCls = state==='posible leak' ? 'badge badge-danger'
            : state==='envejecido'   ? 'badge badge-warn'
                : 'badge';
        const tr = document.createElement('tr');
        tr.innerHTML = `
      <td>${b.ptr}</td>
      <td>${b.size}</td>
      <td>${bytesToMB(b.size).toFixed(2)}</td>
      <td>${b.file || ''}${b.line? ':'+b.line:''}</td>
      <td>${b.type || ''}</td>
      <td>${age_ms}</td>
      <td><span class="${badgeCls}">${state}</span></td>`;
        frag.appendChild(tr);
    }
    tbody.appendChild(frag);
}

/* =============== Render: POR ARCHIVO =============== */
function renderFileTable(fileStats){
    lastFileStats = fileStats || {};
    const rows = Object.entries(lastFileStats)
        .map(([file,st])=>({file, ...st}))
        .sort((a,b)=> (b.alloc_bytes||0)-(a.alloc_bytes||0));

    const tbody = $('#tbl-files'); tbody.innerHTML = '';
    const frag = document.createDocumentFragment();
    for (const r of rows){
        const tr = document.createElement('tr');
        tr.innerHTML = `<td title="${r.file}">${r.file}</td>
                    <td>${r.alloc_count}</td>
                    <td>${bytesToMB(r.alloc_bytes).toFixed(2)}</td>
                    <td>${r.live_count}</td>
                    <td>${bytesToMB(r.live_bytes).toFixed(2)}</td>`;
        frag.appendChild(tr);
    }
    tbody.appendChild(frag);

    // Chart (Top N)
    const N = Math.max(3, Math.min(50, parseInt($('#sel-topN').value||'10',10)));
    const top = rows.slice(0,N);
    const labels = top.map(r=>r.file.split('/').pop());
    const series = [
        { name:'Conteo', values: top.map(r=>r.alloc_count||0) },
        { name:'MB',     values: top.map(r=>bytesToMB(r.alloc_bytes||0)) },
    ];
    const canvas = $('#chart-files');
    const ctx = canvas.getContext('2d'); ctx.clearRect(0,0,canvas.width,canvas.height);
    barChart(canvas, labels, series, { stacked:false });
}

/* =============== Render: LEAKS =============== */
function renderLeaksKPIs(leaks){
    lastLeaks = leaks || {};
    $('#kpi-lk-total').textContent = fmtMB(leaks.total_leak_bytes||0);
    const largest = leaks.largest || {};
    $('#kpi-lk-largest').textContent = largest.size ? `${largest.ptr || '—'} (${largest.file || '—'}) · ${fmtMB(largest.size)}` : '—';
    const topf = leaks.top_file_by_leaks || {};
    $('#kpi-lk-topfile').textContent = topf.file ? `${topf.file} · ${topf.count||0} · ${fmtMB(topf.bytes||0)}` : '—';
    $('#kpi-lk-rate').textContent = fmtPct(leaks.leak_rate || 0);
}
function renderLeaksCharts(){
    // Usamos file-stats para estimar "fugas por archivo" a partir de bloques vivos envejecidos
    const leakPerFile = new Map(); // file -> bytes (solo envejecidos)
    for (const b of (lastBlocks||[])) {
        const {age_ms} = classifyBlockAge(b.ts_ns);
        if (age_ms > LEAK_THRESHOLD_MS) {
            leakPerFile.set(b.file || '(desconocido)', (leakPerFile.get(b.file || '(desconocido)')||0) + (b.size||0));
        }
    }
    const entries = Array.from(leakPerFile.entries()).sort((a,b)=>b[1]-a[1]);
    const labels = entries.map(e=> e[0].split('/').pop());
    const valuesB = entries.map(e=> e[1]);

    // Barras (bytes)
    const bars = $('#chart-leaks-bars');
    bars.getContext('2d').clearRect(0,0,bars.width,bars.height);
    barChart(bars, labels, [{name:'bytes', values: valuesB}], { stacked:false });

    // Pie
    const pie = $('#chart-leaks-pie');
    pie.getContext('2d').clearRect(0,0,pie.width,pie.height);
    pieChart(pie, valuesB, labels, {});

    // Temporal: leak_bytes de timeline
    const line = $('#chart-leaks-time');
    line.getContext('2d').clearRect(0,0,line.width,line.height);
    const lkMB = (lastTimeline||[]).map(p=> bytesToMB(p.leak_bytes||0));
    lineChart(line, null, [{name:'leaks', values: lkMB}]);
}

/* =============== Polling & loop =============== */
let pollTimer = null;
let consecutiveErrors = 0;

async function pollOnce(){
    try{
        const [metrics, timeline, blocks, fileStats, leaks] = await Promise.all([
            fetchJSON('/metrics'),
            fetchJSON('/timeline'),
            fetchJSON('/blocks'),
            fetchJSON('/file-stats'),
            fetchJSON('/leaks')
        ]);

        setStatus(true); consecutiveErrors = 0;

        // GENERAL
        renderKPIs(metrics);
        renderTimeline(timeline);
        renderTop3(fileStats);

        // MAPA
        renderBlocks(blocks);

        // POR ARCHIVO
        renderFileTable(fileStats);

        // LEAKS
        renderLeaksKPIs(leaks);
        renderLeaksCharts();

    } catch(e){
        consecutiveErrors++;
        if (consecutiveErrors >= 2) setStatus(false);
        // backoff leve opcional
    }
}

function startPolling(){
    if (pollTimer) clearInterval(pollTimer);
    pollTimer = setInterval(pollOnce, POLL_MS);
}

/* =============== Arranque =============== */
window.addEventListener('load', ()=>{
    setupTabs();
    $('#blocks-sort').addEventListener('change', ()=> renderBlocks(lastBlocks));
    $('#sel-topN').addEventListener('change', ()=> renderFileTable(lastFileStats));
    startPolling();
    pollOnce();
});
