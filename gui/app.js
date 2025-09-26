// Config
const METRICS_URL = '/metrics';
const POLL_MS = 1000; // 1 s
const STALE_AMBER_MS = 3000;
const STALE_RED_MS = 10000;

let frozen = false;
let pollHandle = null;

// Intl formatters
const fmtInt = new Intl.NumberFormat(undefined, { maximumFractionDigits: 0 });
const fmtMB  = new Intl.NumberFormat(undefined, { minimumFractionDigits: 2, maximumFractionDigits: 2 });

// DOM refs
const elCurrentMB   = document.getElementById('currentMB');
const elActiveAll   = document.getElementById('activeAllocs');
const elLeaksMB     = document.getElementById('leaksMB');
const elLeaksBadge  = document.getElementById('leaksBadge');
const elPeakMB      = document.getElementById('peakMB');
const elTotalAll    = document.getElementById('totalAllocs');
const elUpdated     = document.getElementById('lastUpdated');
const elSource      = document.getElementById('sourceLabel');
const elStatusPill  = document.getElementById('status-pill');

const btnFreeze     = document.getElementById('btnFreeze');
const btnRefresh    = document.getElementById('btnRefresh');

// Utils
function toClock(msEpoch) {
    if (!Number.isFinite(msEpoch)) return '—:—:—';
    const d = new Date(msEpoch);
    const pad = n => String(n).padStart(2, '0');
    return `${pad(d.getHours())}:${pad(d.getMinutes())}:${pad(d.getSeconds())}`;
}

function setStatus(color, text) {
    elStatusPill.className = `status-pill status-${color}`;
    elStatusPill.textContent = text;
}

// Normaliza contratos posibles:
// a) prompt: {current_mb, peak_mb, …, timestamp_ms, ok, source?}
// b) alternativa: {currentMB, peakMB, …, updatedAtNs, ok}
function normalizeMetrics(j) {
    // Mapea nombres alternativos
    const currentMB = (typeof j.current_mb === 'number') ? j.current_mb
        : (typeof j.currentMB === 'number') ? j.currentMB
            : (typeof j.current_bytes === 'number') ? (j.current_bytes / (1024*1024))
                : NaN;

    const peakMB = (typeof j.peak_mb === 'number') ? j.peak_mb
        : (typeof j.peakMB === 'number') ? j.peakMB
            : (typeof j.peak_bytes === 'number') ? (j.peak_bytes / (1024*1024))
                : NaN;

    const leaksMB = (typeof j.leak_mb === 'number') ? j.leak_mb
        : (typeof j.leaksMB === 'number') ? j.leaksMB
            : (typeof j.leak_bytes === 'number') ? (j.leak_bytes / (1024*1024))
                : 0;

    const activeAllocs = Number.isFinite(j.active_allocs) ? j.active_allocs
        : Number.isFinite(j.activeAllocs) ? j.activeAllocs
            : NaN;

    const totalAllocs = Number.isFinite(j.total_allocs) ? j.total_allocs
        : Number.isFinite(j.totalAllocs) ? j.totalAllocs
            : NaN;

    // Timestamps
    let tsMs = Number.isFinite(j.timestamp_ms) ? j.timestamp_ms : NaN;
    if (!Number.isFinite(tsMs) && Number.isFinite(j.updatedAtNs)) {
        tsMs = Math.floor(j.updatedAtNs / 1e6);
    }

    const ok = (typeof j.ok === 'boolean') ? j.ok : true;
    const source = typeof j.source === 'string' ? j.source : '—';

    return { currentMB, peakMB, leaksMB, activeAllocs, totalAllocs, tsMs, ok, source };
}

function updateCards(m) {
    // Valores
    elCurrentMB.textContent = Number.isFinite(m.currentMB) ? fmtMB.format(m.currentMB) : '—';
    elPeakMB.textContent    = Number.isFinite(m.peakMB)    ? fmtMB.format(m.peakMB)    : '—';
    elLeaksMB.textContent   = Number.isFinite(m.leaksMB)   ? fmtMB.format(m.leaksMB)   : '—';

    elActiveAll.textContent = Number.isFinite(m.activeAllocs) ? fmtInt.format(m.activeAllocs) : '—';
    elTotalAll.textContent  = Number.isFinite(m.totalAllocs)  ? fmtInt.format(m.totalAllocs)  : '—';

    // Badge “estimado” si leaksMB == 0 pero ok == true (no hay cálculo real en backend)
    const showEstimated = m.ok && Number.isFinite(m.leaksMB) && Math.abs(m.leaksMB) < 1e-9;
    elLeaksBadge.hidden = !showEstimated;

    // Substatus
    elUpdated.textContent = Number.isFinite(m.tsMs) ? toClock(m.tsMs) : '—:—:—';
    elSource.textContent  = m.source;
}

function updateStatusFreshness(m) {
    const now = Date.now();
    const age = Number.isFinite(m.tsMs) ? (now - m.tsMs) : Infinity;

    if (!m.ok) {
        setStatus('red', 'Error de backend');
        return;
    }
    if (age < STALE_AMBER_MS) {
        setStatus('green', 'Conectado');
    } else if (age < STALE_RED_MS) {
        setStatus('amber', 'Conectado (latente)');
    } else {
        setStatus('red', 'Sin datos recientes');
    }
}

async function fetchWithTimeout(url, ms = 900) {
    const ctl = new AbortController();
    const t = setTimeout(() => ctl.abort(), ms);
    try {
        const resp = await fetch(url, { cache: 'no-store', signal: ctl.signal });
        clearTimeout(t);
        return resp;
    } catch (e) {
        clearTimeout(t);
        throw e;
    }
}

async function fetchMetrics() {
    if (frozen) return;

    try {
        const resp = await fetchWithTimeout(METRICS_URL, 900);
        if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
        const j = await resp.json();
        const m = normalizeMetrics(j);

        updateCards(m);
        updateStatusFreshness(m);
    } catch {
        setStatus('red', 'Sin conexión / Reintentando…');
        // No se actualizan cards en error, se mantienen los últimos valores
    }
}

function startPolling() {
    if (pollHandle) clearInterval(pollHandle);
    pollHandle = setInterval(fetchMetrics, POLL_MS);
    fetchMetrics(); // primera de inmediato
}

btnFreeze.addEventListener('click', () => {
    frozen = !frozen;
    btnFreeze.setAttribute('aria-pressed', String(frozen));
    btnFreeze.textContent = frozen ? 'Reanudar' : 'Congelar';
});
btnRefresh.addEventListener('click', () => {
    if (!frozen) fetchMetrics();
});

startPolling();
