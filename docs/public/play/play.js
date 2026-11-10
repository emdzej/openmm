// OpenMM in the browser: openmm.wasm in gasm's Worker mode. The worker runs the game and reads the
// floppy on demand, either from the site's OPFS copy (FileSystemSyncAccessHandle) or straight from the
// picked files (FileReaderSync). The game draws its 8-bit frames itself, as the original did (the VGA's
// 320 x 200 mode 13h, and 640 x 350 for the code card), and presents them as RGBA (gasm video_present);
// the page shows them through gasm's WebGL 2 presenter, scaled up with a filter. The page keeps input
// and audio.
//
// Test and debug query parameters:
//   hashframes=N   run N frames on virtual time as fast as possible, then print the same
//                  "frames=... video_fnv32=... audio_fnv32=..." line as gasm-run --headless
//                  (globalThis.__openmmResult); in-memory storage, like gasm-run's headless runs
//   batch=N        with hashframes: frames per worker batch (default 250)
//   input=SCRIPT   with hashframes: scripted input, gasm-run's --input syntax ("60:KEY(Enter),...")
//   filter=NAME    the presenter's upscaling filter (sharp, nearest, xbr, fsr, crt); integer: whole multiples
//   autoplay       start the imported data right away
//   any parameter the game reads (--param, e.g. codecard=1, splash=0) is passed through
import { BrowserInput, ProcExit, Resampler } from './vendor/gasm/gasm-host.js';
import { GasmWorker } from './vendor/gasm/gasm-worker.js';
import { InputScript } from './vendor/gasm/input-script.js';
import * as data from './data.js';
import { FILTERS, GlPresenter } from './vendor/gasm/gasm-present.js';

const $ = (id) => document.getElementById(id);
const query = new URLSearchParams(location.search);
const PAGE_PARAMS = new Set(['hashframes', 'batch', 'input', 'filter', 'integer', 'autoplay']);
const HASH_FRAMES = Number(query.get('hashframes') || 0);
const BATCH = Number(query.get('batch') || 250);   // hash runs: frames per worker batch
const STORAGE = 'openmm';                // gasm:storage namespace (IndexedDB): SETTINGS.DAT
const TITLE = 'Micro Machines';
const hasOpfs = !!navigator.storage?.getDirectory;

function message(text, kind = 'info') {
  const m = $('message');
  m.hidden = !text;
  m.textContent = text ?? '';
  m.className = `message ${kind}`;
  if (text) console.log(`[play] ${text}`);
}
const show = (id, on) => { $(id).hidden = !on; };

// ---- display ------------------------------------------------------------------------------
// gasm's presenter (WebGL 2): letterboxed at the canvas's device-pixel size with an upscaling filter
// (?filter= / the toolbar, kept in localStorage; default sharp = even pixels at any size), optionally whole
// multiples only (?integer). Without WebGL 2, a canvas 2D scaled up by CSS.
const canvas = $('screen');
const view = {
  filter: FILTERS.includes(query.get('filter')) ? query.get('filter') : localStorage.getItem('openmm.filter') ?? 'sharp',
  integerScale: query.has('integer') || localStorage.getItem('openmm.integer') === '1',
};
if (!FILTERS.includes(view.filter)) view.filter = 'sharp';
let presenter = null, ctx2d = null, shown = null;   // shown: the frame on screen [rgba, w, h]
function present(rgba, w, h) {
  shown = [rgba, w, h];
  if (!ctx2d && !presenter) {
    presenter = GlPresenter.create(canvas);
    if (presenter) canvas.classList.add('gl');
    else { ctx2d = canvas.getContext('2d'); show('view-controls', false); }
  }
  if (presenter) {
    const size = [Math.round(canvas.clientWidth * devicePixelRatio) || w, Math.round(canvas.clientHeight * devicePixelRatio) || h];
    return presenter.draw(rgba, w, h, size, view);
  }
  if (canvas.width !== w || canvas.height !== h) { canvas.width = w; canvas.height = h; }
  ctx2d.putImageData(new ImageData(rgba, w, h), 0, 0);
}
const redraw = () => { if (shown) present(...shown); };
const resized = new ResizeObserver(redraw);
resized.observe(canvas);

for (const f of FILTERS) $('filter').add(new Option(f, f));
$('filter').value = view.filter;
$('integer').checked = view.integerScale;
$('filter').onchange = () => { view.filter = $('filter').value; localStorage.setItem('openmm.filter', view.filter); redraw(); };
$('integer').onchange = () => {
  view.integerScale = $('integer').checked;
  keyboard.integerScale = view.integerScale;
  localStorage.setItem('openmm.integer', view.integerScale ? '1' : '0');
  redraw();
};
/** The frame on screen at its own size, as a PNG Blob (tests; independent of the filter). */
function framePng() {
  if (!shown) return Promise.resolve(null);
  const [rgba, w, h] = shown, c = document.createElement('canvas');
  c.width = w; c.height = h;
  c.getContext('2d').putImageData(new ImageData(rgba, w, h), 0, 0);
  return new Promise((r) => c.toBlob(r, 'image/png'));
}

// ---- keyboard and gamepads ------------------------------------------------------------------
// The game reads the raw keyboard itself (src/platform_gasm.c maps gasm's keys to the scan codes the
// original's keyboard handler reads), so the page only collects it. Gamepads are virtual pads 0-3, in
// connection order.
const typing = (e) => e.target instanceof HTMLInputElement || e.target instanceof HTMLTextAreaElement;
const keyboard = new BrowserInput(canvas, { ignore: (e) => !running || typing(e) }).attach();
keyboard.integerScale = view.integerScale;   // the pointer's frame position follows the letterbox
// While playing, keys shouldn't scroll the page or open the browser's menus (Alt, F10); browser shortcuts
// (Cmd/Ctrl + key) still work. A tap of Escape goes to the game; holding it for a second stops the game,
// like gasm-run.
const browserKey = (e) => e.metaKey || (e.ctrlKey && !/^(Control|Shift)/.test(e.code));
let escDown = 0;
addEventListener('keydown', (e) => {
  if (!running || typing(e)) return;
  resumeAudio();
  if (e.code === 'Escape') { if (!e.repeat) escDown = performance.now(); return; }
  if (!browserKey(e)) e.preventDefault();
});
addEventListener('keyup', (e) => { if (e.code === 'Escape') escDown = 0; });
addEventListener('blur', () => { escDown = 0; });

// W3C "standard" gamepad mapping -> gasm button bit (A east, B south, X north, Y west, like gasm's app.js).
const PAD = { 1: 0, 0: 1, 3: 2, 2: 3, 4: 4, 5: 5, 6: 4, 7: 5, 8: 6, 9: 7, 12: 8, 13: 9, 14: 10, 15: 11 };
function readPads() {
  const pads = [0, 0, 0, 0];
  let n = 0;
  for (const gp of navigator.getGamepads?.() ?? []) {
    if (!gp || n > 3) continue;
    let m = 0;
    for (const [btn, bit] of Object.entries(PAD)) if (gp.buttons[btn]?.pressed) m |= 1 << bit;
    const [x = 0, y = 0] = gp.axes;
    if (x < -0.5) m |= 1 << 10; if (x > 0.5) m |= 1 << 11;
    if (y < -0.5) m |= 1 << 8; if (y > 0.5) m |= 1 << 9;
    pads[n++] |= m;
  }
  return pads;
}

// ---- audio: the gasm web player's AudioWorklet queue ------------------------------------------
const WORKLET = `
class GasmOut extends AudioWorkletProcessor {
  constructor() {
    super();
    this.q = []; this.off = 0; this.len = 0; this.primed = false;
    this.target = Math.round(sampleRate * 0.06) * 2; this.max = Math.round(sampleRate * 0.2) * 2;
    this.port.onmessage = (e) => {
      this.q.push(e.data); this.len += e.data.length;
      while (this.len > this.max && this.q.length > 1) { this.len -= this.q[0].length - this.off; this.q.shift(); this.off = 0; }
    };
  }
  process(_, [out]) {
    const L = out[0], R = out[1] ?? out[0];
    if (!this.primed && this.len >= this.target) this.primed = true;
    for (let i = 0; i < L.length; i++) {
      if (!this.primed || this.len < 2) { this.primed = false; L[i] = R[i] = 0; continue; }
      const b = this.q[0];
      L[i] = b[this.off]; R[i] = b[this.off + 1];
      this.off += 2; this.len -= 2;
      if (this.off >= b.length) { this.q.shift(); this.off = 0; }
    }
    return true;
  }
}
registerProcessor('gasm-out', GasmOut);`;

let audioCtx = null, audioNode = null, gain = null, resampler = null, audioReady = null;
let muted = localStorage.getItem('openmm.muted') === '1';
/** Call from a click handler (browsers only start audio on a user gesture). */
function startAudio() {
  if (!audioCtx) {
    try {
      audioCtx = new AudioContext({ latencyHint: 'interactive' });
    } catch (e) { message(`No sound: ${e.message}`, 'warn'); return; }
    audioReady = audioCtx.audioWorklet.addModule(URL.createObjectURL(new Blob([WORKLET], { type: 'text/javascript' }))).then(() => {
      audioNode = new AudioWorkletNode(audioCtx, 'gasm-out', { outputChannelCount: [2] });
      gain = new GainNode(audioCtx, { gain: muted ? 0 : 1 });
      audioNode.connect(gain).connect(audioCtx.destination);
      resampler = new Resampler(audioCtx.sampleRate);
    }).catch((e) => { message(`No sound: ${e.message}`, 'warn'); audioCtx = null; });
  }
  audioCtx?.resume();
}
// A browser may suspend the context later (Safari "interrupts" it when another app or tab takes the
// audio device, or after the page was hidden): resume it when the page is shown again or on the next
// key or click, which count as the user gesture it needs.
function resumeAudio() { if (audioCtx && audioCtx.state !== 'running') audioCtx.resume().catch(() => {}); }
document.addEventListener('visibilitychange', () => { if (!document.hidden) resumeAudio(); });
addEventListener('pointerdown', resumeAudio);
function onAudio(samples, rate, channels) {
  if (!audioNode) return;
  const out = resampler.process(samples, rate, channels);
  audioNode.port.postMessage(out.slice());
}
function setMuted(m) {
  muted = m;
  localStorage.setItem('openmm.muted', m ? '1' : '0');
  if (gain) gain.gain.value = m ? 0 : 1;
  $('sound').textContent = m ? 'Sound: off' : 'Sound: on';
}
$('sound').onclick = () => { startAudio(); setMuted(!muted); };
setMuted(muted);

$('fullscreen').onclick = () => {
  if (document.fullscreenElement) document.exitFullscreen();
  else ($('stage').requestFullscreen ?? $('stage').webkitRequestFullscreen)?.call($('stage'));
};
canvas.ondblclick = () => $('fullscreen').onclick();
// gasm.set_fullscreen: honoured by gasm only shortly after a key press or click, as browsers require.
function gameFullscreen(on) {
  if (on && !document.fullscreenElement) $('stage').requestFullscreen?.().catch(() => {});
  else if (!on && document.fullscreenElement) document.exitFullscreen().catch(() => {});
}
document.addEventListener('fullscreenchange', () => { if (worker) worker.fullscreen = !!document.fullscreenElement; });

// ---- what the port adds (opt-in; the default is the original) --------------------------------------
// The Options card: each is a --param of the game (docs/guide/status.md). Kept in localStorage.
const OPTIONS = { codecard: ['0', '1'], splash: ['1', '0'] };   // [the default, the other]
for (const k of Object.keys(OPTIONS)) {
  const el = $(`opt-${k}`);
  el.checked = (localStorage.getItem(`openmm.${k}`) ?? OPTIONS[k][0]) === '1';
  el.onchange = () => localStorage.setItem(`openmm.${k}`, el.checked ? '1' : '0');
}
/** the game's parameters for the options that differ from the default (the page's query wins) */
function options() {
  const p = {};
  for (const [k, [def]] of Object.entries(OPTIONS)) { const v = $(`opt-${k}`).checked ? '1' : '0'; if (v !== def) p[k] = v; }
  return p;
}

// ---- running the game ---------------------------------------------------------------------
let worker = null, running = false, inflight = false, rafId = 0, lastLog = '';
const wasmCache = {};
let acc = 0, last = 0, fpsN = 0, fpsT = 0;

async function loadWasm(name) {
  if (!wasmCache[name]) {
    const r = await fetch(new URL(name, import.meta.url));
    if (!r.ok) throw new Error(`${name}: HTTP ${r.status}`);
    wasmCache[name] = await r.arrayBuffer();
  }
  return wasmCache[name].slice(0); // start() transfers its copy to the worker
}

function onLog(msg) {
  console.log(msg);
  if (/^\[guest\]/.test(msg)) lastLog = msg.replace(/^\[guest\]\s*/, '');
}

async function stopGame() {
  cancelAnimationFrame(rafId);
  running = false; inflight = false; escDown = 0;
  const w = worker; worker = null;
  await w?.exit();          // flushes the options, releases the OPFS handles
}

function stopped(e) {
  running = false;
  if (e instanceof ProcExit) message('The game has ended. Press Play to start again.');
  else message(`The game stopped: ${e.message}${lastLog ? ` (${lastLog})` : ''}`, 'error');
  stopGame();
  endGameView();
}

function endGameView() {
  cancelAnimationFrame(rafId);
  show('game', false);
  show('setup', true);
  refreshImported();
}

/** Start the game from 'opfs' (the imported data) or a checked source (read in place). */
async function play(source) {
  startAudio();                        // still inside the click's user gesture
  message('');
  await stopGame();
  lastLog = '';
  if (typeof Worker === 'undefined' || typeof WebAssembly === 'undefined') {
    message('This browser can\'t run OpenMM: it needs WebAssembly and Web Workers (a recent Chrome, Edge, Firefox or Safari).', 'error');
    return;
  }
  show('game', true);
  show('setup', HASH_FRAMES > 0);      // keep the page visible in test runs
  $('status').textContent = 'Starting...';
  const asked = Object.fromEntries([...query].filter(([k]) => !PAGE_PARAMS.has(k)));
  const params = { ...options(), ...asked };
  try {
    await audioReady;
    worker = await GasmWorker.start({
      wasm: await loadWasm('openmm.wasm'), assets: data.assetSpecs(source), params,
      storage: HASH_FRAMES > 0 ? null : STORAGE,     // hash runs: in memory, like gasm-run --headless
      hashing: HASH_FRAMES > 0, virtualTime: HASH_FRAMES > 0, onLog, onAudio, keyboard: true,
      onTitle: (t) => { document.title = `${t ?? TITLE} | OpenMM`; },
      onFullscreen: gameFullscreen,
    });
  } catch (e) {
    worker = null;
    const busy = /NoModificationAllowed|InvalidState|lock|access handle/i.test(`${e.name} ${e.message}`);
    if (busy) message('The imported floppy is in use by another OpenMM tab or window. Close it and try again.', 'error');
    else if (e instanceof ProcExit) message(`The game could not start${lastLog ? `: ${lastLog}` : ''}. OpenMM needs the 1994 floppy release of Micro Machines.`, 'error');
    else message(`The game could not start: ${e.message}`, 'error');
    endGameView();
    return;
  }
  document.title = `${TITLE} | OpenMM`;
  if (HASH_FRAMES > 0) return hashRun(HASH_FRAMES);
  canvas.focus?.();
  acc = 0; last = performance.now(); running = true;
  rafId = requestAnimationFrame(tick);
}

function tick(now) {
  rafId = requestAnimationFrame(tick);
  if (!running || !worker) return;
  if (escDown && now - escDown >= 1000) {   // Escape held: stop (the tap already went to the game)
    $('stop').onclick();
    return;
  }
  // The game sets its frame rate (gasm.set_frame_rate): the worker reports it.
  const period = 1000 / worker.frameRate;
  acc += Math.min(now - last, 100);    // clamp after tab switches
  last = now;
  // Fixed timestep; when catching up (at most 4 frames), only the last one is shown.
  const due = Math.min(4, Math.floor(acc / period));
  if (!inflight && due > 0) {          // one batch in flight: the worker sets the pace
    const pads = readPads();
    inflight = true;
    acc -= due * period;
    const steps = Array.from({ length: due }, (_, k) => ({ pads, input: keyboard.frame(k === 0) }));
    worker.frames(steps, true).then((r) => {
      inflight = false;
      fpsN += due;
      if (r.frame) present(r.frame.rgba, r.frame.width, r.frame.height);
      keyboard.setMode(worker?.inputMode ?? 0);
    }, stopped);
  }
  if (acc > period * 4) acc = 0;       // fell far behind: resync
  if (now - fpsT >= 1000) { $('status').textContent = `${fpsN} frames/s`; fpsN = 0; fpsT = now; }
}

const hex = (h) => (h >>> 0).toString(16).padStart(8, '0');
/** gasm-run --headless N [--input SCRIPT]: the same frames, the same input, the same hash line. */
async function hashRun(n) {
  let script;
  try { script = new InputScript(query.get('input') ?? ''); } catch (e) { message(`input: ${e.message}`, 'error'); return; }
  const st = {};
  let done = 0;
  try {
    while (done < n) {
      const k = Math.min(BATCH, n - done);
      const steps = Array.from({ length: k }, (_, i) => {
        const f = done + i;
        return { pads: [script.pad(f), 0, 0, 0], text: script.textAt(f), input: script.raw(f, st, [320, 200], worker.inputMode) };
      });
      const r = await worker.frames(steps, done + k === n);
      if (r.frame) present(r.frame.rgba, r.frame.width, r.frame.height);
      done += k;
    }
  } catch (e) { if (!(e instanceof ProcExit)) { message(`hash run failed: ${e.message}`, 'error'); throw e; } }
  const s = worker.stats;   // the same line as gasm-run --headless
  const out = `frames=${s.frames} presented=${s.presented} size=${s.width}x${s.height} ` +
              `video_fnv32=${hex(s.videoHash)} audio_fnv32=${hex(s.audioHash)} audio_frames=${s.audioFrames}`;
  $('status').textContent = out;
  console.log(out);
  globalThis.__openmmPng = await framePng();
  globalThis.__openmmResult = out;
}

$('stop').onclick = async () => { await stopGame(); endGameView(); };
addEventListener('pagehide', () => { worker?.exit(); });

// ---- choosing and importing the data --------------------------------------------------------
let checked = null; // result of data.folderFrom...

function showCheck(result) {
  checked = result;
  const list = $('check-list');
  list.textContent = '';
  const item = (text, ok) => {
    const li = document.createElement('li');
    li.className = ok ? 'ok' : 'bad';
    li.textContent = text;
    list.append(li);
  };
  const src = result.source;
  if (src.kind === 'image') item(`floppy image "${src.name}": ${mbText(result.bytes)}`, true);
  else item(`folder "${src.name}": ${result.files} files, ${mbText(result.bytes)}`, true);
  if (result.problems.length) {
    for (const p of result.problems) item(p, false);
  } else if (src.kind === 'image') {
    item('a FAT floppy: the game reads the image itself and checks MICRO.EXE', true);
  } else {
    item('MICRO.COM, MICRO.EXE, SM.EXE and the tracks found', true);
  }
  show('check', true);
  show('check-actions', !result.problems.length);
  $('import').hidden = !hasOpfs;
  globalThis.__openmmChecked = { problems: result.problems, files: result.files, bytes: result.bytes, kind: src.kind };
}
const mbText = data.mb;

$('pick-folder').onclick = async () => {
  message('');
  if (!window.showDirectoryPicker) return $('folder-input').click(); // Firefox, Safari
  let handle;
  try { handle = await showDirectoryPicker({ id: 'openmm-data', mode: 'read' }); } catch (e) {
    if (e.name !== 'AbortError') message(e.message, 'error');
    return;
  }
  message('Reading the folder...');
  try { showCheck(await data.folderFromHandle(handle)); message(''); } catch (e) { message(`Could not read the folder: ${e.message}`, 'error'); }
};
$('pick-image').onclick = () => { message(''); $('image-input').click(); };
$('image-input').onchange = async (e) => {
  const files = [...e.target.files];
  e.target.value = '';
  if (!files.length) return;
  try { showCheck(await data.imageFromFiles(files)); } catch (err) { message(`Could not read the image: ${err.message}`, 'error'); }
};
$('folder-input').onchange = (e) => {
  if (e.target.files.length) showCheck(data.folderFromFileList(e.target.files));
  e.target.value = '';
};

$('import').onclick = async () => {
  if (!checked) return;
  startAudio();
  const source = checked.source;
  show('check', false);
  show('progress', true);
  $('pick-folder').disabled = $('pick-image').disabled = true;
  const t0 = performance.now();
  try {
    await stopGame();
    const info = await data.importSource(source, (p) => {
      $('progress-bar').value = p.total ? p.bytes / p.total : 1;
      $('progress-text').textContent = `Copying ${p.file}: ${mbText(p.bytes)} of ${mbText(p.total)} (${p.done} of ${p.files} files)`;
    });
    $('progress-text').textContent = `Imported ${info.files} files, ${mbText(info.bytes)} in ${((performance.now() - t0) / 1000).toFixed(0)} s.`;
    globalThis.__openmmImported = info;
    await refreshImported();
    show('progress', false);
    await play('opfs');
  } catch (e) {
    show('progress', false);
    const full = /quota/i.test(`${e.name} ${e.message}`);
    message(full ? `Not enough browser storage for the floppy (${mbText(checked.bytes)}). Free some space, or play without importing.`
                 : `Import failed: ${e.message}. You can still play without importing.`, 'error');
    show('check', true);
    await data.removeImport().catch(() => {});
    refreshImported();
  } finally {
    $('pick-folder').disabled = $('pick-image').disabled = false;
  }
};
$('play-direct').onclick = () => { if (checked) play(checked.source); };
$('play-opfs').onclick = () => play('opfs');
$('remove').onclick = async () => {
  await stopGame();
  show('game', false);
  try { await data.removeImport(); message('The imported floppy was removed from this browser.'); } catch (e) { message(`Could not remove it: ${e.message}`, 'error'); }
  refreshImported();
};
$('persist').onclick = async () => {
  const ok = await data.requestPersist();
  message(ok ? 'The browser will keep the imported data.' : 'The browser did not agree to keep the data; it stays until space runs low.', ok ? 'info' : 'warn');
  refreshImported();
};

async function refreshImported() {
  const info = hasOpfs ? await data.existingImport() : null;
  show('imported', !!info);
  $('choose-title').textContent = info ? 'Your floppy' : 'Choose your floppy';
  if (info) {
    $('imported-desc').textContent = info.kind === 'image' ? `floppy image "${info.name}", ${mbText(info.bytes)}`
      : `folder "${info.name}", ${info.files} files, ${mbText(info.bytes)}`;
    const s = await data.storageInfo();
    $('storage').textContent = s.usage !== undefined
      ? `This site uses ${mbText(s.usage)} of the ${mbText(s.quota ?? 0)} the browser allows${s.persisted ? '; kept even when space runs low' : ''}.`
      : '';
    show('persist-row', !s.persisted && !!navigator.storage?.persist);
  }
  return info;
}

if (!hasOpfs) message('This browser has no private file storage (OPFS), so the floppy can\'t be imported. Play without importing works.', 'warn');
if (typeof Worker === 'undefined' || typeof WebAssembly === 'undefined') message('This browser can\'t run OpenMM (it needs WebAssembly and Web Workers).', 'error');
const ready = refreshImported();
ready.then((info) => {
  globalThis.__openmmReady = true;
  if (info && query.has('autoplay')) play('opfs');
});
globalThis.openmmPlay = { play, stopGame, refreshImported, framePng, data };
