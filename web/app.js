const $ = (id) => document.getElementById(id);

async function apiGet(url){
  const r = await fetch(url);
  if(!r.ok) throw new Error(url+" -> "+r.status);
  return await r.json();
}
async function apiPostForm(url, params){
  const body = new URLSearchParams(params);
  const r = await fetch(url, {method:"POST", headers:{"Content-Type":"application/x-www-form-urlencoded"}, body});
  if(!r.ok) throw new Error(url+" -> "+r.status);
  return await r.json();
}

const state = {
  tape: [],
  lastTradeId: 0,
  paused: false,
  windowSec: 60,
  hover: null,
  lastSnapTs: 0,
  lastPrice: null,
};

function setConn(ok, text){
  const dot = $("connDot");
  const t = $("connText");
  if(ok){
    dot.style.background = "#22c55e";
    dot.style.boxShadow = "0 0 0 4px rgba(34,197,94,.16)";
    t.textContent = text || "live";
  }else{
    dot.style.background = "#ef4444";
    dot.style.boxShadow = "0 0 0 4px rgba(239,68,68,.16)";
    t.textContent = text || "disconnected";
  }
}

function fmt(x){
  if(x==null) return "—";
  const n = Number(x);
  if(!isFinite(n)) return "—";
  return String(Math.trunc(n));
}

function updateHeader(snap){
  const bid = snap.best_bid;
  const ask = snap.best_ask;
  const last = snap.last_trade ?? null;

  $("hdrBid").textContent = fmt(bid);
  $("hdrAsk").textContent = fmt(ask);
  $("hdrLast").textContent = fmt(last);
  $("hdrTs").textContent = "ts=" + (snap.ts ?? "—");

  let spr = "—";
  if(bid!=null && ask!=null){
    spr = String(Number(ask) - Number(bid));
  }
  $("hdrSpr").textContent = spr;
}

function updateTape(recent){
  if(!Array.isArray(recent)) return;
  for(const t of recent){
    const id = Number(t.id ?? 0);
    if(id > state.lastTradeId){
      state.tape.push({
        id,
        ts: Number(t.ts ?? 0),
        price: Number(t.price ?? 0),
        qty: Number(t.qty ?? 0),
      });
      state.lastTradeId = id;
    }
  }
  const MAX = 200000;
  if(state.tape.length > MAX) state.tape = state.tape.slice(state.tape.length - MAX);
}

function setTradesTable(trades){
  const body = $("tapeBody");
  body.innerHTML = "";
  if(!Array.isArray(trades)) return;

  const xs = trades.slice().reverse().slice(0, 28);
  let prev = state.lastPrice;
  for(const t of xs){
    const px = Number(t.price ?? 0);
    const cls = (prev==null) ? "" : (px >= prev ? "tUp" : "tDown");
    prev = px;

    const tr = document.createElement("tr");
    tr.innerHTML = `
      <td class="mono">${t.ts}</td>
      <td class="mono ${cls}">${t.price}</td>
      <td class="mono">${t.qty}</td>
    `;
    body.appendChild(tr);
  }
  if(xs.length>0) state.lastPrice = Number(xs[0].price ?? state.lastPrice);
}

// ---------- Candles ----------
function autoIntervalSeconds(windowSeconds){
  if(windowSeconds <= 15) return 1;
  if(windowSeconds <= 60) return 2;
  if(windowSeconds <= 300) return 5;
  if(windowSeconds <= 900) return 15;
  if(windowSeconds <= 3600) return 60;
  if(windowSeconds <= 21600) return 300;
  return 900;
}

function buildCandles(trades, nowNs, windowNs, intervalNs){
  const startNs = nowNs - windowNs;
  if(intervalNs <= 0) intervalNs = 1_000_000_000;

  const xs = [];
  for(const t of trades){
    if(t.ts >= startNs && t.ts <= nowNs) xs.push(t);
  }
  if(xs.length === 0) return [];

  xs.sort((a,b)=>a.ts-b.ts);

  const buckets = new Map();
  for(const t of xs){
    const k = Math.floor(t.ts / intervalNs) * intervalNs;
    let c = buckets.get(k);
    if(!c){
      c = { t:k, o:t.price, h:t.price, l:t.price, c:t.price, v:t.qty };
      buckets.set(k, c);
    }else{
      c.h = Math.max(c.h, t.price);
      c.l = Math.min(c.l, t.price);
      c.c = t.price;
      c.v += t.qty;
    }
  }

  const keys = Array.from(buckets.keys()).sort((a,b)=>a-b);
  const out = keys.map(k => buckets.get(k));

  // fill gaps with previous close
  const filled = [];
  let prev = out[0];
  filled.push(prev);
  for(let i=1;i<out.length;i++){
    const cur = out[i];
    let k = prev.t + intervalNs;
    while(k < cur.t){
      const p = prev.c;
      filled.push({ t:k, o:p, h:p, l:p, c:p, v:0 });
      k += intervalNs;
    }
    filled.push(cur);
    prev = cur;
  }
  return filled;
}

// ---------- Chart rendering (TradingView-ish) ----------
function cssVar(name){
  return getComputedStyle(document.documentElement).getPropertyValue(name).trim();
}

const chartState = {
  candles: [],
  padL: 62, padR: 78, padT: 14, padB: 34,
  volH: 90,
  lo: 0, hi: 1,
  xOf: (i)=>0,
  yOf: (p)=>0,
};

function drawChart(){
  const canvas = $("chart");
  const ctx = canvas.getContext("2d");

  const w = canvas.width = canvas.clientWidth;
  const h = canvas.height = canvas.clientHeight;

  const bg = cssVar("--bg");
  const grid = cssVar("--grid");
  const text = cssVar("--muted2");
  const muted = cssVar("--muted");
  const up = cssVar("--up");
  const down = cssVar("--down");

  const padL = chartState.padL, padR = chartState.padR, padT = chartState.padT, padB = chartState.padB;
  const volH = chartState.volH;
  const plotH = h - padT - padB;
  const priceH = Math.max(60, plotH - volH);

  ctx.fillStyle = bg;
  ctx.fillRect(0,0,w,h);

  // grid
  ctx.strokeStyle = grid;
  ctx.lineWidth = 1;
  const gx = 7, gy = 6;
  for(let i=0;i<=gx;i++){
    const x = padL + (w-padL-padR)*i/gx;
    ctx.beginPath(); ctx.moveTo(x, padT); ctx.lineTo(x, h-padB); ctx.stroke();
  }
  for(let j=0;j<=gy;j++){
    const y = padT + (h-padT-padB)*j/gy;
    ctx.beginPath(); ctx.moveTo(padL, y); ctx.lineTo(w-padR, y); ctx.stroke();
  }

  ctx.font = "12px ui-monospace, SFMono-Regular, Menlo, Consolas, monospace";
  ctx.fillStyle = text;

  const candles = chartState.candles;
  if(!candles || candles.length === 0){
    ctx.fillText("waiting for trades…", padL, padT+20);
    return;
  }

  // range
  let lo = Infinity, hi = -Infinity;
  let vmax = 1;
  for(const c of candles){
    lo = Math.min(lo, c.l);
    hi = Math.max(hi, c.h);
    vmax = Math.max(vmax, c.v);
  }
  if(!isFinite(lo) || !isFinite(hi) || hi <= lo){ lo -= 1; hi += 1; }
  const pad = (hi-lo)*0.06;
  lo -= pad; hi += pad;

  chartState.lo = lo;
  chartState.hi = hi;

  const xOf = (i)=> padL + (w-padL-padR) * (i / Math.max(1, candles.length-1));
  const yOf = (p)=> padT + priceH * (1 - (p - lo) / (hi - lo));

  chartState.xOf = xOf;
  chartState.yOf = yOf;

  // y-axis labels
  ctx.fillStyle = muted;
  const ticks = 6;
  for(let j=0;j<=ticks;j++){
    const p = lo + (hi-lo)*j/ticks;
    const y = yOf(p);
    ctx.fillText(p.toFixed(2), 8, y+4);
  }

  // candles
  const innerW = (w-padL-padR);
  const step = innerW / Math.max(1, candles.length);
  const bodyW = Math.max(3, Math.floor(step * 0.66));

  for(let i=0;i<candles.length;i++){
    const c = candles[i];
    const x = xOf(i);
    const yO = yOf(c.o), yC = yOf(c.c), yH = yOf(c.h), yL = yOf(c.l);

    // wick
    ctx.strokeStyle = grid;
    ctx.beginPath(); ctx.moveTo(x, yH); ctx.lineTo(x, yL); ctx.stroke();

    const isUp = c.c >= c.o;
    ctx.fillStyle = isUp ? up : down;

    const top = Math.min(yO, yC);
    const bot = Math.max(yO, yC);
    const height = Math.max(1, bot - top);
    ctx.fillRect(Math.floor(x - bodyW/2), Math.floor(top), bodyW, Math.floor(height));
  }

  // volume (bottom area)
  const volTop = padT + priceH + 10;
  const volBottom = h - padB - 8;
  for(let i=0;i<candles.length;i++){
    const c = candles[i];
    const x = xOf(i);
    const isUp = c.c >= c.o;
    ctx.fillStyle = isUp ? up : down;
    const vh = (c.v / vmax) * (volBottom - volTop);
    ctx.globalAlpha = 0.22;
    ctx.fillRect(Math.floor(x - bodyW/2), Math.floor(volBottom - vh), bodyW, Math.floor(vh));
    ctx.globalAlpha = 1.0;
  }

  // last price line
  const last = candles[candles.length-1].c;
  const yLast = yOf(last);
  ctx.strokeStyle = cssVar("--muted");
  ctx.setLineDash([4,4]);
  ctx.beginPath(); ctx.moveTo(padL, yLast); ctx.lineTo(w-padR, yLast); ctx.stroke();
  ctx.setLineDash([]);

  // label box right
  ctx.fillStyle = "rgba(0,0,0,0.45)";
  ctx.fillRect(w-padR+8, yLast-10, padR-16, 20);
  ctx.strokeStyle = cssVar("--border");
  ctx.strokeRect(w-padR+8, yLast-10, padR-16, 20);
  ctx.fillStyle = text;
  ctx.fillText(last.toFixed(0), w-padR+14, yLast+5);

  // x-axis labels
  ctx.fillStyle = muted;
  const labels = 5;
  for(let i=0;i<=labels;i++){
    const idx = Math.floor((candles.length-1) * i/labels);
    const t = candles[idx].t;
    const x = xOf(idx);
    const sec = (t/1e9).toFixed(0);
    ctx.fillText(sec+"s", x-12, h-10);
  }

  // crosshair + tooltip
  if(state.hover && state.hover.idx != null){
    const idx = state.hover.idx;
    const c = candles[idx];
    const x = xOf(idx);

    ctx.strokeStyle = "rgba(203,213,225,0.35)";
    ctx.beginPath(); ctx.moveTo(x, padT); ctx.lineTo(x, h-padB); ctx.stroke();

    const y = state.hover.y;
    if(y!=null){
      ctx.beginPath(); ctx.moveTo(padL, y); ctx.lineTo(w-padR, y); ctx.stroke();
    }

    const tip = $("chartTip");
    tip.style.display = "block";
    tip.innerHTML =
      `t=${(c.t/1e9).toFixed(0)}s<br>` +
      `O ${c.o} H ${c.h} L ${c.l} C ${c.c}<br>` +
      `V ${c.v}`;

    // place tooltip near cursor
    const tx = Math.min(w-220, Math.max(10, state.hover.mx + 12));
    const ty = Math.min(h-90, Math.max(10, state.hover.my + 12));
    tip.style.left = tx + "px";
    tip.style.top = ty + "px";
  }else{
    $("chartTip").style.display = "none";
  }
}

function pickCandleIndex(mx){
  const candles = chartState.candles;
  if(!candles || candles.length === 0) return null;
  let best = 0;
  let bestDist = Infinity;
  for(let i=0;i<candles.length;i++){
    const x = chartState.xOf(i);
    const d = Math.abs(mx - x);
    if(d < bestDist){ bestDist = d; best = i; }
  }
  return best;
}

// ---------- Ladder ----------
function renderLadder(depth, snap){
  const body = $("ladderBody");
  body.innerHTML = "";

  const bids = Array.isArray(depth.bids) ? depth.bids : [];
  const asks = Array.isArray(depth.asks) ? depth.asks : [];
  const bestBid = snap.best_bid;
  const bestAsk = snap.best_ask;

  if(bids.length === 0 && asks.length === 0){
    body.innerHTML = `<div class="mono muted" style="padding:10px;">no depth yet</div>`;
    return;
  }

  // cum (best -> deeper)
  const askCum = [];
  let acc = 0;
  for(let i=0;i<asks.length;i++){ acc += Number(asks[i].qty||0); askCum.push(acc); }

  const bidCum = [];
  acc = 0;
  for(let i=0;i<bids.length;i++){ acc += Number(bids[i].qty||0); bidCum.push(acc); }

  const maxCum = Math.max(1, ...askCum, ...bidCum);

  // asks (top)
  for(let i=0;i<asks.length;i++){
    const a = asks[i];
    const px = a.price, qty = a.qty, cum = askCum[i];
    const isBest = (bestAsk!=null && px === bestAsk);
    const w = Math.max(0, Math.min(1, cum / maxCum));

    const row = document.createElement("div");
    row.className = "ladderRow mono" + (isBest ? " bestRow" : "");
    row.innerHTML = `
      <div class="cell r muted"></div>
      <div class="cell r muted"></div>
      <div class="cell r muted"></div>

      <div class="cell pxAsk">${px}</div>
      <div class="cell">
        <div class="bar ask" style="width:${Math.floor(w*100)}%;"></div>
        ${qty}
      </div>
      <div class="cell muted">${cum}</div>
    `;
    body.appendChild(row);
  }

  // spread row
  {
    const row = document.createElement("div");
    row.className = "ladderRow spreadRow mono";
    const bb = (bestBid==null) ? null : Number(bestBid);
    const ba = (bestAsk==null) ? null : Number(bestAsk);
    let spread = null;
    if(bb!=null && ba!=null) spread = ba - bb;

    row.innerHTML = `
      <div class="cell r centerNote"></div>
      <div class="cell r centerNote"></div>
      <div class="cell r centerNote">${bb==null? "—" : bb}</div>

      <div class="cell centerNote">${ba==null? "—" : ba}</div>
      <div class="cell centerNote">spr ${spread==null? "—" : spread}</div>
      <div class="cell centerNote">mid ${snap.mid==null? "—" : snap.mid}</div>
    `;
    body.appendChild(row);
  }

  // bids (below)
  for(let i=0;i<bids.length;i++){
    const b = bids[i];
    const px = b.price, qty = b.qty, cum = bidCum[i];
    const isBest = (bestBid!=null && px === bestBid);
    const w = Math.max(0, Math.min(1, cum / maxCum));

    const row = document.createElement("div");
    row.className = "ladderRow mono" + (isBest ? " bestRow" : "");
    row.innerHTML = `
      <div class="cell r muted">${cum}</div>
      <div class="cell r">
        <div class="bar bid" style="width:${Math.floor(w*100)}%;"></div>
        ${qty}
      </div>
      <div class="cell r pxBid">${px}</div>

      <div class="cell muted"></div>
      <div class="cell muted"></div>
      <div class="cell muted"></div>
    `;
    body.appendChild(row);
  }
}

// ---------- Refresh loop ----------
async function refresh(){
  try{
    if(state.paused){
      setConn(true, "paused");
      return;
    }

    const snap = await apiGet("/api/snapshot");
    state.lastSnapTs = Number(snap.ts ?? state.lastSnapTs);

    updateHeader(snap);
    updateTape(snap.recent_trades);
    setTradesTable(snap.recent_trades);

    const winNs = Math.floor(state.windowSec * 1e9);
    const cs = $("candleSel").value;
    const intervalS = (cs === "auto") ? autoIntervalSeconds(state.windowSec) : Number(cs);
    const intervalNs = Math.floor(intervalS * 1e9);

    chartState.candles = buildCandles(state.tape, Number(snap.ts), winNs, intervalNs);
    drawChart();

    const levels = Number($("depthSel").value || 5);
    const depth = await apiGet("/api/depth?levels=" + encodeURIComponent(levels));
    renderLadder(depth, snap);
    $("ladderMeta").textContent =
      `levels=${levels}  asks=${(depth.asks||[]).length}  bids=${(depth.bids||[]).length}  max_cum=${depth.max_cum ?? "—"}`;

    setConn(true, "live");
  }catch(e){
    setConn(false, "error");
    console.error(e);
  }
}

// ---------- UI events ----------
$("sendBtn").addEventListener("click", async ()=>{
  try{
    const side = $("side").value;
    const type = $("type").value;
    const tif  = $("tif").value;
    const id   = $("oid").value;
    const price= $("price").value;
    const qty  = $("qty").value;

    const res = await apiPostForm("/api/order", {side, type, tif, id, price, qty});
    alert(JSON.stringify(res));
  }catch(e){
    alert("error: " + e);
  }
});

$("cancelBtn").addEventListener("click", async ()=>{
  try{
    const id = $("oid").value;
    const res = await apiPostForm("/api/cancel", {id});
    alert(JSON.stringify(res));
  }catch(e){
    alert("error: " + e);
  }
});

$("modifyBtn").addEventListener("click", async ()=>{
  try{
    const id = $("oid").value;
    const qty = $("newQty").value;
    const res = await apiPostForm("/api/modify", {id, qty});
    alert(JSON.stringify(res));
  }catch(e){
    alert("error: " + e);
  }
});

$("pauseBtn").addEventListener("click", ()=>{
  state.paused = !state.paused;
  $("pauseBtn").textContent = state.paused ? "Resume" : "Pause";
});

$("clearBtn").addEventListener("click", ()=>{
  state.tape = [];
  state.lastTradeId = 0;
  state.lastPrice = null;
});

$("candleSel").addEventListener("change", ()=>{ refresh(); });
$("depthSel").addEventListener("change", ()=>{ refresh(); });

document.querySelectorAll("#winSeg button").forEach(btn=>{
  btn.addEventListener("click", ()=>{
    document.querySelectorAll("#winSeg button").forEach(b=>b.classList.remove("is-active"));
    btn.classList.add("is-active");
    state.windowSec = Number(btn.dataset.win || 60);
    refresh();
  });
});

// chart crosshair
const canvas = $("chart");
canvas.addEventListener("mousemove", (ev)=>{
  const rect = canvas.getBoundingClientRect();
  const mx = ev.clientX - rect.left;
  const my = ev.clientY - rect.top;
  const idx = pickCandleIndex(mx);
  state.hover = { idx, mx, my, y: my };
  drawChart();
});
canvas.addEventListener("mouseleave", ()=>{
  state.hover = null;
  drawChart();
});

// start
setConn(false, "connecting…");
refresh();
setInterval(refresh, 300);
