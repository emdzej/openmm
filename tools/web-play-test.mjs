#!/usr/bin/env node
// Headless Chrome test of the browser play page (docs/public/play/), through the page's own code: the
// floppy image is handed to its file input (CDP DOM.setFileInputFiles, as if picked), checked, and run
// without importing in gasm's Worker mode (?hashframes=N: virtual time, the page prints gasm-run
// --headless's hash line). The line must match gasm-run's for the same module, image, frames and keys.
// Also saves the page as Chrome renders it and the last frame.
//
//   docs/scripts/copy-wasm.sh && (cd docs && scripts/vendor-web.sh && pnpm build)
//   node tools/web-play-test.mjs <floppy image> [out dir]
// Needs Chrome (CHROME=<binary>) and gasm-run (GASM_RUN=<binary>, default
// .deps/gasm-runner-macos-universal/gasm-run). Serves docs/.vitepress/dist on 127.0.0.1:8794. Never opens
// a window. One run at a time (AGENTS.md): it runs the game twice, one after the other.
import { spawn, execFileSync } from 'node:child_process';
import { existsSync, mkdirSync, mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const repo = fileURLToPath(new URL('..', import.meta.url));
const IMG = process.argv[2] && resolve(process.argv[2]);
const OUT = resolve(process.argv[3] ?? '/tmp/openmm-play');
const DIST = join(repo, 'docs/.vitepress/dist');
const WASM = join(DIST, 'play/openmm.wasm');
const PORT = 8794, DEBUG = 9337;
const CHROME = process.env.CHROME ?? '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const RUN = process.env.GASM_RUN ?? join(repo, '.deps/gasm-runner-macos-universal/gasm-run');
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
if (!IMG || !existsSync(IMG)) { console.error('usage: node tools/web-play-test.mjs <floppy image> [out dir]'); process.exit(2); }
if (!existsSync(WASM)) { console.error('build the site first (docs/scripts/copy-wasm.sh, scripts/vendor-web.sh, pnpm build)'); process.exit(1); }
if (!existsSync(CHROME)) { console.error(`no Chrome at ${CHROME} (CHROME=<binary>)`); process.exit(1); }
mkdirSync(OUT, { recursive: true });

// The splash, GAME OPTIONS, Enter held a few frames, the data set and the title.
const CASE = { frames: 600, params: {}, input: '400-404:KEY(Enter)' };

function native() {
  const args = [WASM, '--headless', String(CASE.frames), '--asset', `disk=${IMG}`, '--input', CASE.input];
  for (const [k, v] of Object.entries(CASE.params)) args.push('--param', `${k}=${v}`);
  const out = execFileSync('nice', ['-n', '19', RUN, ...args], { stdio: ['ignore', 'pipe', 'ignore'] }).toString();
  return /video_fnv32=\S+ audio_fnv32=\S+/.exec(out)?.[0] ?? out.trim();
}

const PROFILE = mkdtempSync(join(tmpdir(), 'openmm-play-chrome-'));
const { createServer } = await import('node:http');
const { readFile } = await import('node:fs/promises');
const TYPES = { '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript', '.css': 'text/css', '.json': 'application/json',
  '.wasm': 'application/wasm', '.svg': 'image/svg+xml', '.png': 'image/png', '.jpg': 'image/jpeg', '.woff2': 'font/woff2' };
const server = createServer(async (req, res) => {
  let path = decodeURIComponent(new URL(req.url, 'http://x').pathname);
  if (path.endsWith('/')) path += 'index.html';
  const file = join(DIST, path);
  if (!file.startsWith(DIST)) { res.writeHead(403).end(); return; }
  try {
    const body = await readFile(file);
    res.writeHead(200, { 'content-type': TYPES[path.slice(path.lastIndexOf('.'))] ?? 'application/octet-stream' }).end(body);
  } catch { res.writeHead(404).end(); }
});
await new Promise((r) => server.listen(PORT, '127.0.0.1', r));
const chrome = spawn(CHROME, ['--headless=new', `--remote-debugging-port=${DEBUG}`, `--user-data-dir=${PROFILE}`,
  '--autoplay-policy=no-user-gesture-required', 'about:blank'], { stdio: 'ignore' });

async function cdp() {
  let target;
  for (let i = 0; i < 150 && !target; i++) {
    await sleep(200);
    target = await fetch(`http://127.0.0.1:${DEBUG}/json`).then((r) => r.json())
      .then((t) => t.find((x) => x.type === 'page' && x.url === 'about:blank')).catch(() => null);
  }
  if (!target) throw new Error(`Chrome's DevTools endpoint didn't come up on port ${DEBUG}`);
  const ws = new WebSocket(target.webSocketDebuggerUrl);
  await new Promise((r) => (ws.onopen = r));
  let id = 0; const pending = new Map(); const logs = [];
  ws.onmessage = (e) => {
    const m = JSON.parse(e.data);
    if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); }
    if (m.method === 'Runtime.consoleAPICalled') logs.push(m.params.args.map((a) => a.value ?? a.description).join(' '));
    if (m.method === 'Runtime.exceptionThrown') logs.push(`EXCEPTION ${m.params.exceptionDetails.exception?.description ?? m.params.exceptionDetails.text}`);
  };
  const send = (method, params = {}) => new Promise((r) => { const i = ++id; pending.set(i, r); ws.send(JSON.stringify({ id: i, method, params })); });
  await send('Runtime.enable'); await send('Page.enable'); await send('DOM.enable');
  const evaluate = async (expr) => (await send('Runtime.evaluate', { expression: expr, returnByValue: true, awaitPromise: true })).result.result?.value;
  const until = async (expr, ms) => {
    const t0 = Date.now();
    while (Date.now() - t0 < ms) { const v = await evaluate(expr); if (v) return v; await sleep(200); }
    throw new Error(`timeout waiting for ${expr}\n  ${logs.slice(-8).join('\n  ')}`);
  };
  const setFiles = async (selector, files) => {
    const doc = await send('DOM.getDocument');
    const node = await send('DOM.querySelector', { nodeId: doc.result.root.nodeId, selector });
    const r = await send('DOM.setFileInputFiles', { nodeId: node.result.nodeId, files });
    if (r.error) throw new Error(`setFileInputFiles: ${r.error.message}`);
  };
  const open = async (url) => {
    await send('Page.navigate', { url });
    await until('document.readyState === "complete" && !!globalThis.__openmmReady', 60000);
  };
  return { send, evaluate, until, setFiles, open, logs };
}

let failed = false;
const report = (ok, label, detail) => { failed ||= !ok; console.log(`${ok ? 'PASS' : 'FAIL'}  ${label}${detail ? `\n      ${detail}` : ''}`); };
const base = `http://127.0.0.1:${PORT}/play/`;
const images = [IMG];
try {
  const page = await cdp();
  await page.send('Emulation.setDeviceMetricsOverride', { width: 1280, height: 1000, deviceScaleFactor: 1, mobile: false });
  await page.open(base);
  await sleep(1500);
  const shot = await page.send('Page.captureScreenshot', { format: 'png' });
  writeFileSync(join(OUT, 'page.png'), Buffer.from(shot.result.data, 'base64'));

  // the check: the image picked
  await page.setFiles('#image-input', images);
  const checked = await page.until('globalThis.__openmmChecked', 20000);
  report(!checked.problems.length && checked.files === images.length, 'the floppy image is checked', JSON.stringify(checked));

  // a hash run without importing, against gasm-run
  const nat = native();
  console.log(`      gasm-run: ${nat}`);
  const q = new URLSearchParams({ hashframes: String(CASE.frames), input: CASE.input, ...CASE.params });
  await page.open(`${base}?${q}`);
  await page.setFiles('#image-input', images);
  await page.until('globalThis.__openmmChecked', 20000);
  await page.evaluate('document.getElementById("play-direct").click(), 0');
  const t0 = Date.now();
  let line = null;
  while (!line) {
    line = await page.evaluate('globalThis.__openmmResult');
    const err = await page.evaluate('document.getElementById("message").hidden ? "" : document.getElementById("message").textContent');
    if (/could not|stopped|failed/i.test(err)) throw new Error(`page: ${err}\n  ${page.logs.slice(-8).join('\n  ')}`);
    if (Date.now() - t0 > 600000) throw new Error(`timed out\n  ${page.logs.slice(-8).join('\n  ')}`);
    await sleep(300);
  }
  const hash = /video_fnv32=\S+ audio_fnv32=\S+/.exec(line)?.[0];
  console.log(`      page:     ${line} (${((Date.now() - t0) / 1000).toFixed(1)} s)`);
  report(hash === nat, 'the page runs the frames gasm-run runs');
  const png = await page.evaluate('globalThis.__openmmPng ? new Promise((r) => { const f = new FileReader(); f.onload = () => r(f.result); f.readAsDataURL(globalThis.__openmmPng); }) : null');
  if (png) writeFileSync(join(OUT, 'frame.png'), Buffer.from(png.split(',')[1], 'base64'));

} catch (e) {
  report(false, 'the test ran', e.message);
} finally {
  chrome.kill();
  server.close();
  await sleep(500);
  rmSync(PROFILE, { recursive: true, force: true });
}
console.log(failed ? 'FAILED' : `ok (page.png and frame.png in ${OUT})`);
process.exit(failed ? 1 : 0);
