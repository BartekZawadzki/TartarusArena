// The catalog of runs the SUT harness can start: each entry maps validated parameters to the packaged game's own
// command-line switches (docs/TESTING.md). Every entry is headless (-nullrhi: no window, no GPU) unless `rendered`
// is set; rendered runs open a window on the operator's desktop, so the server refuses them unless the operator
// allows them (SUT_ALLOW_RENDERED=1).

const HEADLESS = ['-nullrhi', '-benchmark', '-fps=60', '-unattended', '-nosplash', '-nosound'];

const int = (min, max, def) => ({ type: 'int', min, max, default: def });
const oneOf = (values, def) => ({ type: 'enum', values, default: def });

// `map` is passed as -ArenaStartMap=<Map> (a Shipping build ignores a map given any other way); Arena is the default
function mapArgs(map) { return map && map !== 'Arena' ? [`-ArenaStartMap=${map}`] : []; }

export const CHECKS = {
  botmatch: {
    summary: 'ten bots play a match: ARENA_SUMMARY, ARENA_HERO, ARENA_BOTS, DEATH_AUDIT, CONQUEST_SUMMARY, VR-09',
    params: { map: oneOf(['Arena', 'Conquest'], 'Arena'), minutes: int(1, 20, 3), seed: int(0, 2147483647, 1),
              difficulty: int(0, 2, 2), conquest: oneOf(['on', 'off'], 'off') },
    args: p => [...mapArgs(p.map), '-ArenaBotMatch', `-Minutes=${p.minutes}`, `-Seed=${p.seed}`,
                `-Difficulty=${p.difficulty}`, ...(p.conquest === 'on' ? ['-ArenaConquest'] : [])],
    timeoutS: p => 240 + p.minutes * 60,
  },
  skilllab:    { summary: 'ranks, ultimates, XP, item passives, the shop against heroes.json', params: {}, args: () => ['-ArenaSkillLab'], timeoutS: () => 600 },
  mechlab:     { summary: 'ability indicators against the data; casts land where indicated; walls; melee contact', params: {}, args: () => ['-ArenaMechLab'], timeoutS: () => 900 },
  fxlab:       { summary: 'every ability of every hero against a dummy: damage, knockback, ramps, walls', params: {}, args: () => ['-ArenaFxLab'], timeoutS: () => 900 },
  baselab:     { summary: 'sustain (VR-23): recall and its interrupts, the fountain, potions', params: {}, args: () => ['-ArenaBaseLab'], timeoutS: () => 600 },
  aimlab:      { summary: 'aim assist through the real player controller, hit markers, melee turn and step-in', params: {}, args: () => ['-ArenaAimLab'], timeoutS: () => 600 },
  animlab:     { summary: 'every hero and minion: intro, hit, stun, knock, death, drop-in, victory', params: {}, args: () => ['-ArenaAnimLab'], timeoutS: () => 900 },
  skindeath:   { summary: 'every hero in every skin dies and is destroyed, then garbage collection runs', params: {}, args: () => ['-ArenaSkinDeathLab'], timeoutS: () => 900 },
  conquestlab: { summary: 'Conquest end to end: protection chain, towers, camps, the boss, the core', params: {}, args: () => ['-ArenaStartMap=Conquest', '-ArenaConquestLab'], timeoutS: () => 900 },
  locolab: {
    summary: 'locomotion per hero: idle, forward, strafe, back, stop, relax, a bot path',
    params: { hero: { type: 'id', default: '' } },
    args: p => ['-ArenaStartMap=Conquest', '-ArenaLocoLab', ...(p.hero ? [`-LocoHero=${p.hero}`] : [])],
    timeoutS: () => 1200,
  },
  duellab: {
    summary: 'ranged vs melee 1v1 balance (hard bots, level 9): DUEL, DUEL_SUMMARY',
    params: { only: { type: 'pair', default: '' } },
    args: p => ['-ArenaDuelLab', ...(p.only ? [`-DuelOnly=${p.only}`] : [])],
    timeoutS: p => (p.only ? 600 : 3600),
  },
  foliage:  { summary: 'every scattered plant, stone and tree traced against the ground: FOLIAGE_SUMMARY', params: { map: oneOf(['Arena', 'Conquest'], 'Arena') }, args: p => [...mapArgs(p.map), '-ArenaFoliageCheck'], timeoutS: () => 600 },
  // the nav check runs when a match starts, so it rides on a one-minute bot match (alone, the game waits in its menu)
  navcheck: {
    summary: 'paths base to base, to the orb and every lane point at match start (evt=navpath, evt=navcheck), in a 1-minute bot match',
    params: { map: oneOf(['Arena', 'Conquest'], 'Arena') },
    args: p => [...mapArgs(p.map), '-ArenaBotMatch', '-Minutes=1', '-Seed=1', '-ArenaNavCheck'],
    timeoutS: () => 360,
  },
  protolab: { summary: 'prototype 1: movement, combat, the bot', params: {}, args: () => ['-ArenaStartMap=Proto', '-ProtoLab'], timeoutS: () => 900 },
  hadeslab: { summary: 'prototype 2 (Hades controls)', params: {}, args: () => ['-ArenaStartMap=Proto', '-ProtoLab', '-ProtoHades'], timeoutS: () => 900 },
  uidemo: {
    summary: 'RENDERED: a tour of every screen of the front end and a match, plus the UI-PAUSE check',
    rendered: true, params: {}, args: () => ['-ArenaUIDemo'], timeoutS: () => 900,
  },
};

export function baseArgs(check) { return check.rendered ? ['-unattended', '-nosplash', '-windowed', '-ResX=1600', '-ResY=900'] : HEADLESS; }

// validates a request's params against the entry; returns { params } or { error }
export function validateParams(check, given) {
  if (given === undefined || given === null) given = {};
  if (typeof given !== 'object' || Array.isArray(given)) return { error: 'params must be an object' };
  const out = {};
  for (const k of Object.keys(given)) if (!(k in check.params)) return { error: `unknown param "${k}"` };
  for (const [k, spec] of Object.entries(check.params)) {
    const v = k in given ? given[k] : spec.default;
    if (spec.type === 'int') {
      if (!Number.isInteger(v) || v < spec.min || v > spec.max) return { error: `param "${k}" must be an integer ${spec.min}..${spec.max}` };
    } else if (spec.type === 'enum') {
      if (!spec.values.includes(v)) return { error: `param "${k}" must be one of ${spec.values.join(', ')}` };
    } else if (spec.type === 'id') {
      if (typeof v !== 'string' || !/^[A-Za-z0-9_]{0,32}$/.test(v)) return { error: `param "${k}" must be a hero id (letters, digits, _)` };
    } else if (spec.type === 'pair') {
      if (typeof v !== 'string' || !/^([A-Za-z0-9_]{1,32}-[A-Za-z0-9_]{1,32})?$/.test(v)) return { error: `param "${k}" must look like Ranged-Melee` };
    }
    out[k] = v;
  }
  return { params: out };
}

export function describe() {
  return Object.entries(CHECKS).map(([id, c]) => ({
    id, summary: c.summary, rendered: !!c.rendered,
    params: Object.fromEntries(Object.entries(c.params).map(([k, s]) => [k, { ...s }])),
  }));
}
