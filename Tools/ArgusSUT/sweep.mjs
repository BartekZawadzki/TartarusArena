// Readiness sweep (the builder's): runs every headless entry of the catalog once through the harness and prints,
// per check, whether the packaged game ended by itself and what its journal holds. It answers "does this run work
// in the packaged build?", not "is the game correct?" — that is for the tester's scenarios (docs/ARGUS.md).
//
//   node Tools/ArgusSUT/sweep.mjs [check ...]       default: every headless check (duellab as one pairing)

import { spawn } from 'node:child_process';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { CHECKS } from './checks.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const PORT = 19000 + Math.floor(Math.random() * 1000);
const TOKEN = crypto.randomBytes(24).toString('hex');
const DATA = path.join(HERE, '..', '..', 'Saved', `ArgusSUT-sweep-${Date.now()}`);
const BASE = `http://127.0.0.1:${PORT}`;
const PARAMS = { botmatch: { minutes: 3 }, duellab: { only: 'Sparrow-Greystone' } };
const list = process.argv.length > 2 ? process.argv.slice(2) : Object.keys(CHECKS).filter(k => !CHECKS[k].rendered);
const sleep = ms => new Promise(r => setTimeout(r, ms));
const call = async (method, p, body, id) => {
  const r = await fetch(BASE + p, { method, headers: { Authorization: `Bearer ${TOKEN}`, 'Content-Type': 'application/json', ...(id ? { 'X-Request-Id': id } : {}) },
    body: body ? JSON.stringify(body) : undefined });
  return { status: r.status, json: await r.json().catch(() => null) };
};

const server = spawn(process.execPath, [path.join(HERE, 'server.mjs')], {
  env: { ...process.env, SUT_PORT: String(PORT), SUT_TOKEN: TOKEN, SUT_DATA_DIR: DATA }, stdio: 'ignore', windowsHide: true,
});
const rows = [];
try {
  for (let i = 0; i < 50; i++) { try { await fetch(BASE + '/health'); break; } catch { await sleep(200); } }
  for (const check of list) {
    const id = `sweep-${check}-${Date.now()}`;
    let s = await call('POST', '/runs', { check, params: PARAMS[check] || {} }, id);
    while (s.status === 409 && s.json && s.json.error === 'operator_playing') { console.log('WAIT the operator is playing'); await sleep(60000); s = await call('POST', '/runs', { check, params: PARAMS[check] || {} }, id); }
    if (s.status !== 202) { console.log(`SWEEP ${check} not started: ${s.status} ${JSON.stringify(s.json)}`); rows.push({ check, state: `http ${s.status}` }); continue; }
    let run;
    do { await sleep(4000); run = (await call('GET', `/runs/${id}`)).json.run; } while (['starting', 'running', 'cancelling'].includes(run.state));
    const f = run.facts;
    const row = { check, state: run.state, exitCode: run.exitCode, durationS: run.durationS, lines: f.journalLines, closed: f.evidenceClose,
      lab: `${f.labChecks.pass}/${f.labChecks.fail}`, labSummary: f.summaries.LAB_SUMMARY || null, claims: f.claims,
      summaries: Object.keys(f.summaries).filter(k => k !== 'LAB_SUMMARY'), failed: f.failedLabChecks.slice(0, 8), settings: run.settings };
    rows.push(row);
    console.log(`SWEEP ${JSON.stringify(row)}`);
  }
} finally {
  server.kill();
  await sleep(500);
  fs.rmSync(DATA, { recursive: true, force: true });
  const ended = rows.filter(r => r.state === 'exited' && r.closed).length;
  console.log(`SWEEP_SUMMARY checks=${rows.length} ended_with_journal=${ended}`);
}
