// The player's Micro Machines floppy: picking it, checking it, and importing it into the site's origin
// private file system (OPFS). Nothing here is game data; everything comes from the user's own copy and
// stays on their device.
//
// The game (openmm.wasm, src/platform_gasm.c) reads:
//   the floppy   the asset "disk": an image of the 720 KB disk (FAT12, .img or .ima), read through the
//                port's own FAT reader; or
//   its files    from a folder (the floppy's files copied off, or the installed game): assets named by
//                their paths (MICRO.COM, MICRO.EXE, SM.EXE, GAME1/...).
// The game checks MICRO.EXE's size and CRC-32 (the 1994-09-14 floppy release) and reads its tables at
// run time. A source is { kind: 'image' | 'folder', name, entries: [[asset or path, File], ...] }.
import { directoryHandleEntries, fileListEntries } from './vendor/gasm/gasm-host.js';
import { clearNamespace, opfsFileSystem, persist } from '@emdzej/csfs-opfs';

/** OPFS directory the data is imported into. */
export const NAMESPACE = 'openmm-data';
/** Written last: an import without it is incomplete. Hidden, so the game never sees it. */
const MARKER = '.openmm-import.json';
/** MICRO.EXE of the floppy release (src/exe.c checks the size and CRC-32 8e84eb22). */
export const EXE = 'MICRO.EXE';
export const EXE_SIZE = 28955;
/** What MICRO.COM runs (docs/re/launcher.md): without them it stops with "cannot find". */
const NEEDED = ['MICRO.COM', 'SM.EXE'];
/** Floppy images the port reads: 360 KB to 2.88 MB, in 512-byte sectors. */
const IMAGE_MIN = 360 * 1024, IMAGE_MAX = 2880 * 1024;

const upper = (s) => s.replace(/[a-z]/g, (c) => c.toUpperCase());
export const mb = (n) => (n < 1048576 ? `${Math.max(1, Math.round(n / 1024))} KB` : `${(n / 1048576).toFixed(1)} MB`);

// ---- picking --------------------------------------------------------------------------

/** The folder from showDirectoryPicker() (Chromium). */
export async function folderFromHandle(handle) {
  return checkFolder(handle.name, await directoryHandleEntries(handle));
}

/** The folder from <input type="file" webkitdirectory> (all browsers). */
export function folderFromFileList(files) {
  const name = files[0]?.webkitRelativePath.split('/')[0] || 'folder';
  return checkFolder(name, fileListEntries(files));
}

/**
 * The floppy's files (or the installed game's, or the folder one above them): everything from where
 * MICRO.EXE is. Returns { source, problems, files, bytes }.
 */
export function checkFolder(name, entries) {
  const problems = [];
  const exePath = entries.map(([p]) => p).filter((p) => upper(p.split('/').pop()) === EXE && p.split('/').length <= 2)
    .sort((a, b) => a.length - b.length)[0];
  const root = exePath && exePath.includes('/') ? exePath.slice(0, exePath.lastIndexOf('/') + 1) : '';
  const kept = [];
  if (!exePath) problems.push(`no ${EXE} in this folder: choose the folder with the floppy's files`);
  else {
    for (const [path, file] of entries) if (path.startsWith(root)) kept.push([path.slice(root.length), file]);
    const byName = new Map(kept.map(([p, f]) => [upper(p), f]));
    const exe = byName.get(EXE);
    if (exe && exe.size !== EXE_SIZE) problems.push(`${EXE} is ${exe.size.toLocaleString('en')} bytes, not ${EXE_SIZE.toLocaleString('en')}: OpenMM needs the 1994 floppy release`);
    for (const n of NEEDED) if (!byName.has(n)) problems.push(`${n} is missing`);
    if (![...byName.keys()].some((p) => p.startsWith('GAME1/'))) problems.push('the GAME1 folder (the tracks) is missing');
  }
  const bytes = kept.reduce((n, [, f]) => n + f.size, 0);
  const shown = root ? `${name}/${root.slice(0, -1)}` : name;
  return { source: { kind: 'folder', name: shown, entries: kept }, problems, files: kept.length, bytes };
}

/** A FAT boot sector: the 55 AA signature and 512-byte sectors. */
async function isFat(file) {
  const b = new Uint8Array(await file.slice(0, 512).arrayBuffer());
  return b.length === 512 && b[510] === 0x55 && b[511] === 0xaa && (b[11] | b[12] << 8) === 512;
}

/** A picked floppy image. Returns { source, problems, files, bytes }. */
export async function imageFromFiles(files) {
  const problems = [];
  const f = files[0];
  if (files.length > 1) problems.push('choose one image: the game is on a single floppy');
  if (f.size < IMAGE_MIN || f.size > IMAGE_MAX || f.size % 512) problems.push(`${f.name} is ${f.size.toLocaleString('en')} bytes: not a floppy image (Micro Machines' is 737,280)`);
  else if (!(await isFat(f))) problems.push(`${f.name}: no FAT file system in this image`);
  return { source: { kind: 'image', name: f.name, entries: [['disk', f]] }, problems, files: 1, bytes: f.size };
}

// ---- OPFS import ----------------------------------------------------------------------

/** The completed import's description ({ kind, name, files, bytes, date }), or null. */
export async function existingImport() {
  try {
    const root = await navigator.storage.getDirectory();
    const dir = await root.getDirectoryHandle(NAMESPACE);
    const f = await (await dir.getFileHandle(MARKER)).getFile();
    return JSON.parse(await f.text());
  } catch {
    return null;
  }
}

export async function removeImport() {
  await clearNamespace(NAMESPACE);
  try { await (await navigator.storage.getDirectory()).removeEntry(NAMESPACE, { recursive: true }); } catch {}
}

/** Ask the browser to keep the data under storage pressure. Firefox may never answer a prompt. */
export async function requestPersist() {
  return persist({ signal: AbortSignal.timeout(10000) }).catch(() => false);
}

export async function storageInfo() {
  const est = await navigator.storage?.estimate?.().catch(() => null);
  const persisted = await navigator.storage?.persisted?.().catch(() => false);
  return { usage: est?.usage, quota: est?.quota, persisted: !!persisted };
}

/** Copy a checked source into OPFS; the marker goes last. onProgress({ bytes, total, file, files, done }). */
export async function importSource(source, onProgress = () => {}) {
  const items = source.entries;
  const total = items.reduce((n, [, f]) => n + f.size, 0);
  await persist({ signal: AbortSignal.timeout(3000) }).catch(() => false);
  await removeImport();
  const fs = await opfsFileSystem({ namespace: NAMESPACE });
  let bytes = 0;
  for (let i = 0; i < items.length; i++) {
    const [path, file] = items[i];
    await fs.write(`/${path}`, file.stream());
    bytes += file.size;
    onProgress({ bytes, total, file: path, files: items.length, done: i + 1 });
  }
  const info = { kind: source.kind, name: source.name, files: items.length, bytes: total, date: new Date().toISOString() };
  await fs.write(`/${MARKER}`, new TextEncoder().encode(JSON.stringify(info)));
  return info;
}

// ---- running --------------------------------------------------------------------------

/** gasm Worker-mode asset specs for a source ('opfs' = the imported data). */
export function assetSpecs(source) {
  if (source === 'opfs') return [{ kind: 'opfs', dir: NAMESPACE }];
  return [{ kind: 'files', entries: source.entries }];
}
