/* Pequeño helper para <canvas> sin librerías externas.
   Funciones: lineChart(ctx, dataX[], series[], opts), barChart(ctx, labels[], series[], opts), pieChart(ctx, values[], labels[], opts)
   - No estilos de color fijos; usa colores por defecto del contexto (configurados por caller).
*/

function fitRange(vals) {
    if (!vals.length) return {min:0, max:1};
    let mn = Math.min(...vals), mx = Math.max(...vals);
    if (mn === mx) { mn -= 1; mx += 1; }
    return {min: mn, max: mx};
}
function mapY(v, min, max, h, pad){ return h - pad - ((v - min) / (max - min)) * (h - 2*pad); }
function mapX(i, n, w, pad){ if (n<=1) return pad; return pad + i*( (w-2*pad)/(n-1) ); }

// ---------- helper de tamaño robusto (tab oculto, etc.) ----------
function sizeCanvas(canvas) {
    const parent = canvas.parentElement;
    const measuredW = canvas.clientWidth || (parent ? parent.clientWidth : 0) || canvas.width || 300;
    const measuredH = canvas.clientHeight || canvas.height || 200;
    canvas.width  = measuredW;
    canvas.height = measuredH;
    return { w: canvas.width, h: canvas.height };
}

export function lineChart(canvas, dataX, series, opts={}){
    const ctx = canvas.getContext('2d');

    // Tamaño robusto
    const { w, h } = sizeCanvas(canvas);
    const pad = 28;

    // X normalizado (indices) y Y en MB
    const yVals = [];
    for (const s of series) for (const v of s.values) yVals.push(v);
    const {min, max} = fitRange(yVals);

    // Limpia y Grid
    ctx.clearRect(0,0,w,h);
    ctx.strokeStyle = '#e5e7eb'; ctx.lineWidth = 1;
    for (let gy=0; gy<5; ++gy){
        const y = pad + gy*((h-2*pad)/4);
        ctx.beginPath(); ctx.moveTo(pad, y); ctx.lineTo(w-pad, y); ctx.stroke();
    }

    // Ejes
    ctx.strokeStyle = '#9ca3af';
    ctx.beginPath(); ctx.moveTo(pad, pad); ctx.lineTo(pad, h-pad); ctx.lineTo(w-pad, h-pad); ctx.stroke();

    // Series
    const colors = opts.colors || ['#3b82f6','#ef4444','#10b981','#f59e0b'];
    series.forEach((s, si) => {
        ctx.strokeStyle = colors[si % colors.length];
        ctx.lineWidth = 2; ctx.beginPath();
        s.values.forEach((v, i) => {
            const x = mapX(i, s.values.length, w, pad);
            const y = mapY(v, min, max, h, pad);
            if (i===0) ctx.moveTo(x,y); else ctx.lineTo(x,y);
        });
        ctx.stroke();
    });
}

export function barChart(canvas, labels, series, opts={}){
    const ctx = canvas.getContext('2d');

    // Tamaño robusto
    const { w, h } = sizeCanvas(canvas);
    const pad = 28; const catW = (w - 2*pad) / Math.max(1, labels.length);
    const colors = opts.colors || ['#3b82f6','#10b981','#ef4444','#f59e0b'];
    const stacked = !!opts.stacked;

    // calcular rango
    const sums = labels.map((_,i)=> series.reduce((a,s)=>a+(s.values[i]||0),0));
    const yVals = stacked ? sums : series.flatMap(s=>s.values);
    const {min, max} = fitRange(yVals);

    // Limpia y Grid
    ctx.clearRect(0,0,w,h);
    ctx.strokeStyle = '#e5e7eb'; ctx.lineWidth = 1;
    for (let gy=0; gy<5; ++gy){
        const y = pad + gy*((h-2*pad)/4);
        ctx.beginPath(); ctx.moveTo(pad, y); ctx.lineTo(w-pad, y); ctx.stroke();
    }
    ctx.strokeStyle = '#9ca3af';
    ctx.beginPath(); ctx.moveTo(pad, pad); ctx.lineTo(pad, h-pad); ctx.lineTo(w-pad, h-pad); ctx.stroke();

    // Barras
    labels.forEach((_, i) => {
        if (stacked){
            let acc = 0;
            series.forEach((s, si) => {
                const val = s.values[i]||0;
                const y0 = mapY(acc, min, max, h, pad);
                const y1 = mapY(acc+val, min, max, h, pad);
                const x = pad + i*catW + catW*0.1;
                const bw = catW*0.8;
                ctx.fillStyle = colors[si % colors.length];
                ctx.fillRect(x, y1, bw, y0 - y1);
                acc += val;
            });
        } else {
            const bw = (catW*0.8) / Math.max(1, series.length);
            series.forEach((s, si) => {
                const val = s.values[i]||0;
                const y0 = mapY(0, min, max, h, pad);
                const y1 = mapY(val, min, max, h, pad);
                const x = pad + i*catW + catW*0.1 + si*bw;
                ctx.fillStyle = colors[si % colors.length];
                ctx.fillRect(x, y1, bw, y0 - y1);
            });
        }
    });
}

export function pieChart(canvas, values, labels, opts={}){
    const ctx = canvas.getContext('2d');

    // Tamaño robusto
    const { w, h } = sizeCanvas(canvas);
    ctx.clearRect(0,0,w,h);

    const colors = opts.colors || ['#3b82f6','#10b981','#ef4444','#f59e0b','#8b5cf6','#06b6d4','#84cc16','#f97316'];
    const sum = values.reduce((a,b)=>a+b,0) || 1;
    const cx = w/2, cy = h/2, r = Math.min(w,h)*0.38;
    let start = -Math.PI/2;
    values.forEach((v, i) => {
        const ang = (v/sum)*Math.PI*2;
        ctx.beginPath(); ctx.moveTo(cx,cy);
        ctx.fillStyle = colors[i % colors.length];
        ctx.arc(cx,cy,r,start,start+ang,false); ctx.closePath(); ctx.fill();
        start += ang;
    });
    // anillo
    ctx.strokeStyle = '#fff'; ctx.lineWidth = 2; ctx.beginPath();
    ctx.arc(cx,cy,r,0,Math.PI*2); ctx.stroke();
}
