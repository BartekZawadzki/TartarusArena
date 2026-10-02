// the response file for UnrealPak: every extracted file back to its place under the base pak's mount point "../../../"
const fs = require('fs');
const path = require('path');
const root = path.join(__dirname, 'x');
const out = [];
(function walk(d) {
  for (const e of fs.readdirSync(d, { withFileTypes: true })) {
    const p = path.join(d, e.name);
    if (e.isDirectory()) { walk(p); continue; }
    const rel = path.relative(root, p).split(path.sep).join('/');
    out.push(`"${p}" "../../../${rel}"`);
  }
})(root);
fs.writeFileSync(path.join(__dirname, 'resp.txt'), out.join('\r\n') + '\r\n');
console.log('RESP files=' + out.length);
