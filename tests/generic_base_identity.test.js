const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const root = path.resolve(__dirname, '..');
const files = [
    'build_ps4_port_base.bat',
    'clean.bat',
    'config/project.example.bat',
    'native/piglet_renderer/Makefile',
    'native/piglet_renderer/PROVENANCE.md',
    'native/piglet_renderer/piglet/build.bat',
].map((relativePath) => path.join(root, relativePath));

for (const file of files) {
    const contents = fs.readFileSync(file, 'utf8');
    assert.doesNotMatch(contents, /hmbtn|hmbt|harvest moon/i, `${file} must not identify a specific game`);
    assert.doesNotMatch(contents, /brew00093|pigletgpur/i, `${file} must not retain upstream sample metadata`);
}

const makefile = fs.readFileSync(path.join(root, 'native', 'piglet_renderer', 'Makefile'), 'utf8');
assert.match(makefile, /TITLE\s+\?=/);
assert.match(makefile, /TITLE_ID\s+\?=/);
assert.match(makefile, /CONTENT_ID\s+\?=/);

console.log('generic_base_identity.test.js: PASS');
