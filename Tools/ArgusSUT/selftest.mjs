// The builder's smoke test of the harness's HTTP contract. This is not an Argus scenario set: the scenarios belong to
// the tester and stay out of this repository (the holdout, docs/ARGUS.md). It starts the harness on a free port with
// a throwaway token and data folder, checks the refusals (auth, validation, method, rendered runs), then one real
// headless run end to end: idempotent replay, busy refusal, the journal, every line's requestId, the saga events and
// the operator's settings file untouched.
//
//   node Tools/ArgusSUT/selftest.mjs [check] [params-json]      default: botmatch '{"minutes":1}'

import { spawn } from 'node:child_process';
import crypto from 'node:crypto';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const CHECK = process.argv[2] || 'botmatch';
const PARAMS = JSON.parse(process.argv[3] || (CHECK === 'botmatch' ? '{"minutes":1}' : '{}'));
const PORT = 18000 + Math.floor(Math.random() * 1000);
const TOKEN = crypto.randomBytes(24).toString('hex');
const DATA = path.join(HERE, '..', '..', 'Saved', `ArgusSUT-selftest-${Date.now()}`);
const BASE = `http://127.0.0.1:${PORT}`;
const INI = path.join(process.env.LOCALAPPDATA || '', 'ParagonArena', 'Saved', 'Config', 'Windows', 'GameUserSettings.ini');
const sha = f => { try { return crypto.createHash('sha256').update(fs.readFileSync(f)).digest('hex'); } catch { return 'absent'; } };

let pass = 0, fails = 0;
function check(ok, what) { ok ? pass++ : fails++; console.log(`${ok ? 'PASS' : 'FAIL'} ${what}`); }
async function call(method, p, { body, token = TOKEN, id, raw } = {}) {
  const headers = {};
  if (token) headers.Authorization = `Bearer ${token}`;
  if (id) headers['X-Request-Id'] = id;
  if (body !== undefined) headers['Content-Type'] = 'application/json';
  const r = await fetch(BASE + p, { method, headers, body: body === undefined ? undefined : raw ? body : JSON.stringify(body) });
  const text = await r.text();
  let json = null; try { json = JSON.parse(text); } catch { /* ndjson or empty */ }
  return { status: r.status, json, text, headers: r.headers };
}
const sleep = ms => new Promise(r => setTimeout(r, ms));

const iniBefore = sha(INI);
const server = spawn(process.execPath, [path.join(HERE, 'server.mjs')], {
  env: { ...process.env, SUT_PORT: String(PORT), SUT_TOKEN: TOKEN, SUT_DATA_DIR: DATA }, stdio: ['ignore', 'pipe', 'inherit'], windowsHide: true,
});
let serverOut = '';
server.stdout.on('data', d => { serverOut += d; });
try {
  for (let i = 0; i < 50; i++) { try { await fetch(BASE + '/health'); break; } catch { await sleep(200); } }

  const h = await call('GET', '/health', { token: null });
  check(h.status === 200 && h.json.status === 'ok', `health 200 ok (${h.status})`);
  check(h.json && h.json.game && h.json.game.present === true, `health: the packaged game is present (${h.json && h.json.game && h.json.game.sha256 && h.json.game.sha256.slice(0, 12)})`);
  check((await call('GET', '/catalog', { token: null })).status === 401, 'catalog without a token: 401');
  check((await call('GET', '/catalog', { token: 'wrong' })).status === 403, 'catalog with a wrong token: 403');
  const cat = await call('GET', '/catalog');
  check(cat.status === 200 && cat.json.checks.some(c => c.id === CHECK), `catalog lists "${CHECK}"`);
  check((await call('POST', '/runs', { body: '{not json', raw: true })).status === 400, 'invalid JSON: 400');
  check((await call('POST', '/runs', { body: { check: 'nope' } })).status === 400, 'unknown check: 400');
  check((await call('POST', '/runs', { body: { check: 'botmatch', params: { minutes: 99 } } })).status === 400, 'minutes out of range: 400');
  check((await call('POST', '/runs', { body: { check: 'botmatch', params: { colour: 'red' } } })).status === 400, 'unknown param: 400');
  check((await call('POST', '/runs', { body: { check: 'botmatch', extra: 1 } })).status === 400, 'unknown field: 400');
  check((await call('POST', '/runs', { body: { check: 'botmatch' }, id: 'bad id!' })).status === 400, 'invalid X-Request-Id: 400');
  check((await call('POST', '/runs', { body: { check: 'uidemo' } })).status === 403, 'rendered run without the operator\'s permission: 403');
  check((await call('DELETE', '/runs')).status === 405, 'DELETE /runs: 405');
  check((await call('GET', '/nowhere')).status === 404, 'unknown path: 404');
  check((await call('GET', '/runs/unknown-run')).status === 404, 'unknown run: 404');

  const id = `selftest-${Date.now()}`;
  const started = await call('POST', '/runs', { body: { check: CHECK, params: PARAMS }, id });
  check(started.status === 202 && started.json.run.id === id, `run accepted: 202, id = X-Request-Id (${started.status} ${started.json && started.json.error || ''})`);
  check(started.headers.get('x-request-id') === id, 'the response echoes X-Request-Id');
  const again = await call('POST', '/runs', { body: { check: CHECK, params: PARAMS }, id });
  check(again.status === 200 && again.json.replayed === true, 'the same request again: 200 replayed, no second run');
  check((await call('POST', '/runs', { body: { check: 'protolab' }, id })).status === 409, 'the same id for another run: 409');
  check((await call('POST', '/runs', { body: { check: 'protolab' }, id: `${id}-2` })).status === 409, 'a second run while one is running: 409 busy');

  let run;
  const deadline = Date.now() + (started.json ? started.json.run.timeoutS + 60 : 600) * 1000;
  do { await sleep(3000); run = (await call('GET', `/runs/${id}`, { id })).json.run; } while (['running', 'starting', 'cancelling'].includes(run.state) && Date.now() < deadline);
  console.log(`RUN ${id} state=${run.state} exit=${run.exitCode} duration=${run.durationS}s lines=${run.facts.journalLines} lab=${JSON.stringify(run.facts.labChecks)} claims=${JSON.stringify(run.facts.claims)}`);
  for (const [k, v] of Object.entries(run.facts.summaries)) console.log(`  ${k} ${JSON.stringify(v).slice(0, 300)}`);
  check(run.state === 'exited', `the run ended on its own (state ${run.state})`);
  check(run.facts.evidenceOpen && run.facts.evidenceClose, 'the journal was opened and closed by the game');
  check(run.facts.journalLines > 2, `the game wrote evidence lines (${run.facts.journalLines})`);
  check(run.settings && (run.settings.changed === false || run.settings.restored === true), `the operator's settings were kept (${JSON.stringify(run.settings)})`);
  const ev = await call('GET', `/runs/${id}/evidence`);
  const lines = ev.text.split('\n').filter(Boolean).map(l => JSON.parse(l));
  check(lines.length === run.facts.journalLines && lines.every(l => l.requestId === id), `every journal line carries requestId=${id} (${lines.length})`);
  const filtered = await call('GET', `/runs/${id}/evidence?event=EVIDENCE_OPEN,EVIDENCE_CLOSE`);
  check(filtered.text.split('\n').filter(Boolean).length === 2, 'the evidence filter by event works');
  const sagaSteps = serverOut.split('\n').filter(Boolean).map(l => { try { return JSON.parse(l); } catch { return {}; } })
    .filter(l => l.requestId === id && l.event_type === 'saga').map(l => l.event);
  check(['run.accepted', 'run.launched', 'EVIDENCE_OPEN', 'EVIDENCE_CLOSE', 'run.exited', 'run.collected'].every(s => sagaSteps.includes(s)),
    `the run's saga events are in the harness log (${sagaSteps.join(' > ')})`);
} finally {
  server.kill();
  await sleep(500);
  const iniAfter = sha(INI);
  check(iniBefore === iniAfter, `GameUserSettings.ini byte-identical (${iniBefore.slice(0, 12)})`);
  fs.rmSync(DATA, { recursive: true, force: true });
  console.log(`SELFTEST ${fails ? 'FAIL' : 'PASS'} pass=${pass} fail=${fails}`);
  process.exitCode = fails ? 1 : 0;
}
