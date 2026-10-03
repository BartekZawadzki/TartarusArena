// Tartarus Arena as a system under test (SUT): a small HTTP service that starts the packaged game in its headless
// check modes on request and reports what the game itself wrote. Made for Argus (OneDroid's end-to-end testing
// platform): a scenario triggers a run over HTTP with an X-Request-Id, and every log line the run produces — the
// harness's own and the game's evidence journal (Game/ArenaEvidence.h) — is JSON carrying that id as `requestId`.
// The harness reports facts, never verdicts: judging them is the tester's job. See docs/ARGUS.md.
//
//   node Tools/ArgusSUT/server.mjs            (Node 20+, no dependencies)
//
// Safety rules it enforces: one run at a time; no run while the operator's own game is running; windows on the
// operator's desktop (rendered runs) only with SUT_ALLOW_RENDERED=1; the operator's GameUserSettings.ini is restored
// byte for byte after every run; it only ever stops processes it started.

import http from 'node:http';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import crypto from 'node:crypto';
import { spawn, execFile } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { CHECKS, baseArgs, validateParams, describe } from './checks.mjs';

const VERSION = 'tartarus-sut/1.0.0';
const HERE = path.dirname(fileURLToPath(import.meta.url));
const REPO = path.resolve(HERE, '..', '..');
const ENV = process.env;
const LOCAL = ENV.LOCALAPPDATA || path.join(os.homedir(), 'AppData', 'Local');
const CFG = {
  host: ENV.SUT_HOST || '127.0.0.1',
  port: Number(ENV.SUT_PORT || 8787),
  exe: ENV.SUT_EXE || path.join(REPO, 'Build', 'Windows', 'ParagonArena', 'Binaries', 'Win64', 'ParagonArena-Win64-Shipping.exe'),
  gameSaved: ENV.SUT_GAME_SAVED || path.join(LOCAL, 'ParagonArena', 'Saved'),
  dataDir: ENV.SUT_DATA_DIR || path.join(REPO, 'Saved', 'ArgusSUT'),
  allowRendered: ENV.SUT_ALLOW_RENDERED === '1',
  ratePerMinute: Number(ENV.SUT_RATE_LIMIT || 20),
};
CFG.settingsIni = ENV.SUT_SETTINGS_INI || path.join(CFG.gameSaved, 'Config', 'Windows', 'GameUserSettings.ini');
CFG.evidenceDir = path.join(CFG.gameSaved, 'Evidence');
CFG.runsDir = path.join(CFG.dataDir, 'runs');
fs.mkdirSync(CFG.runsDir, { recursive: true });

const ID_RE = /^[A-Za-z0-9._-]{1,64}$/;
const SUMMARY_EVENTS = ['ARENA_SUMMARY', 'ARENA_PERF', 'ARENA_BOTS', 'DEATH_AUDIT', 'CONQUEST_SUMMARY', 'FOLIAGE_SUMMARY',
  'DUEL_SUMMARY', 'SKIN_DEATH_SUMMARY', 'LAB_SUMMARY', 'navcheck'];

// ---- logging: JSON lines on stdout and in <dataDir>/sut.jsonl --------------------------------------------------------
const logFile = fs.createWriteStream(path.join(CFG.dataDir, 'sut.jsonl'), { flags: 'a' });
function emit(line) { process.stdout.write(line + '\n'); logFile.write(line + '\n'); }
function log(fields) {
  emit(JSON.stringify({ ts: new Date().toISOString(), service: 'tartarus-sut', source: 'harness', level: 'info', event_type: 'log', ...fields }));
}
const saga = (requestId, step, fields = {}) => log({ requestId, event_type: 'saga', event: `run.${step}`, ...fields });

// ---- the bearer token: SUT_TOKEN, else <dataDir>/token (made once). Never logged. -----------------------------------
function loadToken() {
  if (ENV.SUT_TOKEN) return { token: ENV.SUT_TOKEN.trim(), from: 'the SUT_TOKEN variable' };
  const f = path.join(CFG.dataDir, 'token');
  if (fs.existsSync(f)) return { token: fs.readFileSync(f, 'utf8').trim(), from: f };
  const t = crypto.randomBytes(32).toString('hex');
  fs.writeFileSync(f, t + '\n', { mode: 0o600 });
  return { token: t, from: `${f} (new)` };
}
const TOKEN = loadToken();
const TOKEN_BUF = Buffer.from(TOKEN.token);
function authorize(req) {
  const h = req.headers.authorization || '';
  if (!h.startsWith('Bearer ')) return 401;
  const given = Buffer.from(h.slice(7).trim());
  return given.length === TOKEN_BUF.length && crypto.timingSafeEqual(given, TOKEN_BUF) ? 0 : 403;
}

// ---- the game build ------------------------------------------------------------------------------------------------
let exeCache = null;
function exeInfo() {
  let st;
  try { st = fs.statSync(CFG.exe); } catch { return { present: false, name: path.basename(CFG.exe) }; }
  const key = `${st.size}:${st.mtimeMs}`;
  if (!exeCache || exeCache.key !== key) {
    const sha256 = crypto.createHash('sha256').update(fs.readFileSync(CFG.exe)).digest('hex');
    exeCache = { key, info: { present: true, name: path.basename(CFG.exe), bytes: st.size, modified: st.mtime.toISOString(), sha256 } };
  }
  return exeCache.info;
}

// the operator's own game: any running copy of the packaged exe that this harness did not start
function gamePids() {
  const names = ['ParagonArena-Win64-Shipping.exe', 'ParagonArena.exe'];
  return Promise.all(names.map(n => new Promise(res => {
    execFile('tasklist', ['/FI', `IMAGENAME eq ${n}`, '/FO', 'CSV', '/NH'], { windowsHide: true }, (err, out) => {
      if (err) return res([]);
      res(out.split(/\r?\n/).map(l => l.split('","')).filter(c => c.length > 1).map(c => Number(c[1])).filter(Boolean));
    });
  }))).then(a => a.flat());
}
async function operatorPlaying() {
  const mine = new Set(current && current.pid ? [current.pid] : []);
  return (await gamePids()).some(p => !mine.has(p));
}

// ---- the operator's settings: copied before a run, restored byte for byte after ----------------------------------------
function backupSettings() {
  try { return { existed: true, bytes: fs.readFileSync(CFG.settingsIni) }; } catch { return { existed: false }; }
}
function restoreSettings(b) {
  let now = null;
  try { now = fs.readFileSync(CFG.settingsIni); } catch { /* absent */ }
  if (b.existed) {
    if (now && now.equals(b.bytes)) return { changed: false };
    fs.mkdirSync(path.dirname(CFG.settingsIni), { recursive: true });
    fs.writeFileSync(CFG.settingsIni, b.bytes);
    return { changed: true, restored: fs.readFileSync(CFG.settingsIni).equals(b.bytes) };
  }
  if (now) { fs.unlinkSync(CFG.settingsIni); return { changed: true, removedCreated: true }; }
  return { changed: false };
}

// ---- runs ----------------------------------------------------------------------------------------------------------
const runs = new Map();
let current = null;
const recent = [];   // POST /runs timestamps, for the rate limit

for (const f of fs.readdirSync(CFG.runsDir)) {
  if (!f.endsWith('.json')) continue;
  try {
    const r = JSON.parse(fs.readFileSync(path.join(CFG.runsDir, f), 'utf8'));
    if (r.state === 'running' || r.state === 'starting') r.state = 'abandoned';
    r.journal = path.join(CFG.evidenceDir, `${r.id}.jsonl`);
    runs.set(r.id, r);
  } catch { /* a damaged record is skipped */ }
}
const persist = r => fs.writeFileSync(path.join(CFG.runsDir, `${r.id}.json`), JSON.stringify(r, null, 1));
const journalPath = id => path.join(CFG.evidenceDir, `${id}.jsonl`);

function view(r) {
  return {
    id: r.id, check: r.check, params: r.params, state: r.state, createdAt: r.createdAt, startedAt: r.startedAt,
    endedAt: r.endedAt, durationS: r.durationS, exitCode: r.exitCode, timeoutS: r.timeoutS, exe: r.exe,
    settings: r.settings, error: r.error, facts: r.facts,
    links: { self: `/runs/${r.id}`, evidence: `/runs/${r.id}/evidence` },
  };
}

function newFacts() {
  return { journalLines: 0, evidenceOpen: false, evidenceClose: false, errors: 0, warnings: 0,
           labChecks: { pass: 0, fail: 0 }, failedLabChecks: [], claims: {}, summaries: {} };
}

// key=value pairs of an evidence line; numbers become numbers
function pairs(msg) {
  const o = {};
  for (const m of msg.matchAll(/([A-Za-z_][\w.%-]*)=([^\s]+)/g)) o[m[1]] = /^-?\d+(\.\d+)?$/.test(m[2]) ? Number(m[2]) : m[2];
  return o;
}

function absorb(r, raw) {
  let j;
  try { j = JSON.parse(raw); } catch {
    log({ requestId: r.id, level: 'warn', event: 'journal.unparsed', msg: raw.slice(0, 500) });
    return;
  }
  emit(raw);   // the game's own line, verbatim: it already carries requestId
  const f = r.facts, msg = String(j.msg || '');
  f.journalLines++;
  if (j.event === 'EVIDENCE_OPEN') f.evidenceOpen = true;
  if (j.event === 'EVIDENCE_CLOSE') f.evidenceClose = true;
  if (j.level === 'error') f.errors++;
  if (j.level === 'warn') f.warnings++;
  let m;
  if ((m = msg.match(/^LAB (PASS|FAIL) (.*)$/))) {
    if (m[1] === 'PASS') f.labChecks.pass++;
    else { f.labChecks.fail++; if (f.failedLabChecks.length < 50) f.failedLabChecks.push(m[2].slice(0, 200)); }
  }
  if ((m = msg.match(/^(PASS|FAIL) (VR-\d+)/))) f.claims[m[2]] = m[1];
  if (SUMMARY_EVENTS.includes(j.event)) f.summaries[j.event] = pairs(msg);
}

function tail(r) {
  let st;
  try { st = fs.statSync(r.journal); } catch { return; }
  if (st.size <= r.offset) return;
  const fd = fs.openSync(r.journal, 'r');
  try {
    const buf = Buffer.alloc(st.size - r.offset);
    fs.readSync(fd, buf, 0, buf.length, r.offset);
    r.offset = st.size;
    const text = r.partial + buf.toString('utf8');
    const lines = text.split('\n');
    r.partial = lines.pop();
    for (const l of lines) if (l.trim()) absorb(r, l.trim());
  } finally { fs.closeSync(fd); }
}

function killTree(pid) {
  return new Promise(res => execFile('taskkill', ['/PID', String(pid), '/T', '/F'], { windowsHide: true }, () => res()));
}

function start(r, check) {
  const args = [...baseArgs(check), ...check.args(r.params), `-EvidenceJournal=${r.id}`];
  try { fs.unlinkSync(r.journal); } catch { /* none: the usual case */ }
  r.settingsBackup = backupSettings();
  r.state = 'running';
  r.startedAt = new Date().toISOString();
  r.exe = exeInfo();
  const child = spawn(CFG.exe, args, { windowsHide: true, stdio: 'ignore' });
  r.pid = child.pid;
  current = r;
  saga(r.id, 'launched', { check: r.check, params: r.params, pid: child.pid, exe_sha256: r.exe.sha256, args: args.join(' ') });
  persist(view(r));
  const poll = setInterval(() => tail(r), 500);
  const timer = setTimeout(async () => {
    r.state = 'timeout';
    log({ requestId: r.id, level: 'error', event: 'run.timeout', timeoutS: r.timeoutS });
    if (r.pid) await killTree(r.pid);
  }, r.timeoutS * 1000);
  let finished = false;
  const finish = (exitCode, error) => {
    if (finished) return;
    finished = true;
    clearTimeout(timer);
    setTimeout(() => {
      clearInterval(poll);
      tail(r);
      if (r.partial.trim()) { absorb(r, r.partial.trim()); r.partial = ''; }
      r.settings = restoreSettings(r.settingsBackup);
      delete r.settingsBackup;
      r.endedAt = new Date().toISOString();
      r.durationS = Math.round((Date.parse(r.endedAt) - Date.parse(r.startedAt)) / 100) / 10;
      r.exitCode = exitCode;
      if (error) { r.state = 'launch_error'; r.error = error; }
      else if (r.state === 'running' || r.state === 'cancelling') r.state = r.state === 'cancelling' ? 'cancelled' : 'exited';
      saga(r.id, 'settings_restored', r.settings);
      saga(r.id, 'exited', { state: r.state, exitCode, durationS: r.durationS });
      saga(r.id, 'collected', { journalLines: r.facts.journalLines, labChecks: r.facts.labChecks, evidenceClose: r.facts.evidenceClose });
      persist(view(r));
      current = null;
    }, 400);
  };
  child.on('error', e => finish(null, e.message));
  child.on('exit', code => finish(code, null));
}

// ---- HTTP ----------------------------------------------------------------------------------------------------------
function send(res, status, body, headers = {}) {
  const text = typeof body === 'string' ? body : JSON.stringify(body, null, 1);
  res.writeHead(status, { 'Content-Type': typeof body === 'string' ? 'application/x-ndjson; charset=utf-8' : 'application/json; charset=utf-8', ...headers });
  res.end(text);
}
const fail = (res, status, error, detail, headers) => send(res, status, { error, detail }, headers);

function readBody(req, limit = 16 * 1024) {
  return new Promise((resolve, reject) => {
    let size = 0; const parts = [];
    req.on('data', c => { size += c.length; if (size > limit) { reject(Object.assign(new Error('too large'), { status: 413 })); req.destroy(); } else parts.push(c); });
    req.on('end', () => resolve(Buffer.concat(parts).toString('utf8')));
    req.on('error', reject);
  });
}

async function postRun(req, res, requestId, headerId) {
  let body;
  try { body = await readBody(req); } catch (e) { return fail(res, e.status || 400, 'body_too_large', 'the body is limited to 16 KB'); }
  let j;
  try { j = body ? JSON.parse(body) : {}; } catch { return fail(res, 400, 'invalid_json', 'the body must be a JSON object: {"check": "...", "params": {...}}'); }
  if (!j || typeof j !== 'object' || Array.isArray(j)) return fail(res, 400, 'invalid_json', 'the body must be a JSON object');
  for (const k of Object.keys(j)) if (!['check', 'params'].includes(k)) return fail(res, 400, 'unknown_field', `unknown field "${k}"`);
  const check = CHECKS[j.check];
  if (!check) return fail(res, 400, 'unknown_check', `"check" must be one of: ${Object.keys(CHECKS).join(', ')}`);
  const v = validateParams(check, j.params);
  if (v.error) return fail(res, 400, 'invalid_params', v.error);
  if (headerId !== undefined && !ID_RE.test(headerId)) return fail(res, 400, 'invalid_request_id', 'X-Request-Id: 1-64 of A-Z a-z 0-9 . _ -');

  const existing = runs.get(requestId);
  if (existing) {
    const same = existing.check === j.check && JSON.stringify(existing.params) === JSON.stringify(v.params);
    if (!same) return fail(res, 409, 'request_id_reused', 'this X-Request-Id already started a different run');
    return send(res, 200, { replayed: true, run: view(existing) });
  }
  const now = Date.now();
  while (recent.length && now - recent[0] > 60000) recent.shift();
  if (recent.length >= CFG.ratePerMinute) {
    return fail(res, 429, 'rate_limited', `at most ${CFG.ratePerMinute} new runs a minute`, { 'Retry-After': String(Math.ceil((60000 - (now - recent[0])) / 1000)) });
  }
  if (check.rendered && !CFG.allowRendered) {
    return fail(res, 403, 'rendered_not_allowed', 'a rendered run opens a window on the operator\'s desktop; the operator must start the harness with SUT_ALLOW_RENDERED=1');
  }
  if (!exeInfo().present) return fail(res, 503, 'game_not_built', 'the packaged game is not at SUT_EXE');
  if (current) return fail(res, 409, 'busy', `run ${current.id} is in progress; one run at a time`);
  if (await operatorPlaying()) return fail(res, 409, 'operator_playing', 'the operator\'s own game is running; try again later');
  recent.push(now);

  const r = {
    id: requestId, check: j.check, params: v.params, state: 'starting', createdAt: new Date().toISOString(),
    timeoutS: check.timeoutS(v.params), journal: journalPath(requestId), offset: 0, partial: '', facts: newFacts(),
  };
  runs.set(r.id, r);
  saga(r.id, 'accepted', { check: r.check, params: r.params });
  start(r, check);
  send(res, 202, { run: view(r) }, { Location: `/runs/${r.id}` });
}

function getEvidence(res, r, url) {
  let text = '';
  try { text = fs.readFileSync(r.journal, 'utf8'); } catch { return send(res, 200, ''); }
  const events = url.searchParams.get('event');
  const level = url.searchParams.get('level');
  const limit = Math.min(Number(url.searchParams.get('limit') || 5000), 20000);
  const want = events ? new Set(events.split(',')) : null;
  const out = [];
  for (const l of text.split('\n')) {
    if (!l.trim()) continue;
    if (want || level) {
      let j; try { j = JSON.parse(l); } catch { continue; }
      if (want && !want.has(j.event)) continue;
      if (level && j.level !== level) continue;
    }
    out.push(l);
    if (out.length >= limit) break;
  }
  send(res, 200, out.length ? out.join('\n') + '\n' : '');
}

const server = http.createServer(async (req, res) => {
  const t0 = Date.now();
  const headerId = req.headers['x-request-id'];
  const requestId = headerId && ID_RE.test(headerId) ? headerId : `sut-${crypto.randomBytes(6).toString('hex')}`;
  res.setHeader('X-Request-Id', requestId);
  res.on('finish', () => log({ requestId, event: 'http', method: req.method, path: req.url, status: res.statusCode, ms: Date.now() - t0,
    level: res.statusCode >= 500 ? 'error' : res.statusCode >= 400 ? 'warn' : 'info' }));
  try {
    const url = new URL(req.url, 'http://sut');
    const parts = url.pathname.split('/').filter(Boolean);
    const method = req.method;
    const allow = (m, list) => (list.includes(m) ? null : fail(res, 405, 'method_not_allowed', `allowed: ${list.join(', ')}`, { Allow: list.join(', ') }));

    if (url.pathname === '/health') {
      if (allow(method, ['GET'])) return;
      const playing = await operatorPlaying();
      return send(res, 200, { status: 'ok', version: VERSION, game: exeInfo(), busy: !!current, current: current ? current.id : null,
        operatorPlaying: playing, renderedAllowed: CFG.allowRendered });
    }
    const known = parts[0] === 'catalog' || parts[0] === 'runs';
    if (!known) return fail(res, 404, 'not_found', 'see GET /health, /catalog, /runs');
    const auth = authorize(req);
    if (auth === 401) return fail(res, 401, 'unauthorized', 'Authorization: Bearer <token> is required', { 'WWW-Authenticate': 'Bearer' });
    if (auth === 403) return fail(res, 403, 'forbidden', 'the bearer token is not valid');

    if (parts[0] === 'catalog' && parts.length === 1) {
      if (allow(method, ['GET'])) return;
      return send(res, 200, { checks: describe() });
    }
    if (parts[0] === 'runs' && parts.length === 1) {
      if (allow(method, ['GET', 'POST'])) return;
      if (method === 'POST') return postRun(req, res, requestId, headerId);
      const list = [...runs.values()].sort((a, b) => (a.createdAt < b.createdAt ? 1 : -1)).slice(0, 50).map(view);
      return send(res, 200, { runs: list });
    }
    if (parts[0] === 'runs' && parts.length >= 2) {
      const r = runs.get(parts[1]);
      if (!r) return fail(res, 404, 'unknown_run', `no run "${parts[1]}"`);
      if (parts.length === 2) { if (allow(method, ['GET'])) return; return send(res, 200, { run: view(r) }); }
      if (parts.length === 3 && parts[2] === 'evidence') { if (allow(method, ['GET'])) return; return getEvidence(res, r, url); }
      if (parts.length === 3 && parts[2] === 'cancel') {
        if (allow(method, ['POST'])) return;
        if (current !== r || r.state !== 'running') return fail(res, 409, 'not_running', `run is ${r.state}`);
        r.state = 'cancelling';
        saga(r.id, 'cancel_requested', { by: requestId });
        await killTree(r.pid);
        return send(res, 202, { run: view(r) });
      }
    }
    return fail(res, 404, 'not_found', 'see GET /health, /catalog, /runs');
  } catch (e) {
    log({ requestId, level: 'error', event: 'http.exception', msg: String(e && e.stack || e).slice(0, 2000) });
    if (!res.headersSent) fail(res, 500, 'internal', 'the harness failed; see its log');
  }
});

async function shutdown() {
  if (current && current.pid) { log({ requestId: current.id, level: 'warn', event: 'run.stopped_by_shutdown' }); await killTree(current.pid); }
  setTimeout(() => process.exit(0), 800);
}
process.on('SIGINT', shutdown);
process.on('SIGTERM', shutdown);

server.listen(CFG.port, CFG.host, () => {
  const e = exeInfo();
  log({ event: 'sut.start', version: VERSION, listen: `${CFG.host}:${CFG.port}`, game: e.present ? e.name : 'MISSING', token: TOKEN.from,
    renderedAllowed: CFG.allowRendered });
  if (!['127.0.0.1', '::1', 'localhost'].includes(CFG.host)) log({ level: 'warn', event: 'sut.exposed', msg: `listening on ${CFG.host}: reachable from the network` });
});
