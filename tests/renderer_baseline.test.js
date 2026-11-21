const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const root = path.resolve(__dirname, '..');
const baselineRoot = path.join(root, 'native', 'piglet_renderer');
const sourceRoot = path.join(baselineRoot, 'piglet');
const buildPath = path.join(sourceRoot, 'build.bat');
const sourcePath = path.join(sourceRoot, 'PigletApplication.cpp');
const licensePath = path.join(baselineRoot, 'LICENSE.OpenOrbis-GPL-3.0.txt');
const provenancePath = path.join(baselineRoot, 'PROVENANCE.md');
const wrapperPath = path.join(root, 'build_ps4_port_base.bat');
const configPath = path.join(root, 'config', 'project.example.bat');

for (const requiredPath of [
    buildPath,
    sourcePath,
    licensePath,
    provenancePath,
    wrapperPath,
    configPath,
]) {
    assert.ok(fs.existsSync(requiredPath), `Missing upstream baseline file: ${requiredPath}`);
}

const source = fs.readFileSync(sourcePath, 'utf8');
const build = fs.readFileSync(buildPath, 'utf8');
const wrapper = fs.readFileSync(wrapperPath, 'utf8');
const provenance = fs.readFileSync(provenancePath, 'utf8');

assert.match(source, /scePigletSetConfigurationVSH/);
assert.match(source, /eglSwapInterval\(s_display, 0\)/);
assert.match(source, /eglSwapBuffers\(s_display, s_surface\)/);
assert.match(build, /call "%PROJECT_CONFIG%"/);
assert.match(build, /set PKG_TITLE="%PORT_TITLE%"/);
assert.match(build, /set PKG_TITLE_ID="%PORT_TITLE_ID%"/);
assert.match(build, /set PKG_CONTENT_ID="%PORT_CONTENT_ID%"/);
assert.match(build, /CATEGORY --type Utf8 --maxsize 4 --value "gde"/);
assert.match(build, /SYSTEM_VER --type Integer --maxsize 4 --value 1020/);
assert.match(build, /--paid 0x3800000000000035 --authinfo/);
assert.match(wrapper, /renderer_baseline\.test\.js/);
assert.match(wrapper, /set "ROOT=%~dp0"/);
assert.match(wrapper, /config\\project\.local\.bat/);
assert.match(wrapper, /PORT_CONTENT_ID/);
assert.match(wrapper, /DOTNET_ROLL_FORWARD=Major/);
assert.match(wrapper, /%PORT_TITLE_ID%\.pkg/);
assert.match(wrapper, /if exist "%SOURCE_PACKAGE%" del \/Q "%SOURCE_PACKAGE%"/);
assert.match(wrapper, /if exist "%SOURCE_PACKAGE%" goto :error/);
assert.match(wrapper, /if not exist "%SOURCE_PACKAGE%" goto :error/);
assert.match(provenance, /OpenOrbis PS4 Toolchain/);
assert.match(provenance, /GPL-3\.0/);
assert.match(provenance, /github\.com\/OpenOrbis\/OpenOrbis-PS4-Toolchain/);
assert.match(provenance, /b48489ae15a03afb8ad1781c115dc5e8bd20b64095ff04792c6a43ad81cb6f87/);
const config = fs.readFileSync(configPath, 'utf8');
assert.match(config, /PORT_TITLE=/);
assert.match(config, /PORT_TITLE_ID=/);
assert.match(config, /PORT_CONTENT_ID=/);

console.log('renderer_baseline.test.js: PASS');
