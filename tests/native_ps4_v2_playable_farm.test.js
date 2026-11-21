const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const root = path.resolve(__dirname, '..');
const sourceRoot = path.join(root, 'native', 'piglet_upstream');
const pigletRoot = path.join(sourceRoot, 'piglet');
const applicationPath = path.join(pigletRoot, 'PigletApplication.cpp');
const inputPath = path.join(pigletRoot, 'HmbtnPs4Input.cpp');
const inputHeaderPath = path.join(pigletRoot, 'HmbtnPs4Input.hpp');
const buildPath = path.join(pigletRoot, 'build.bat');
const telemetryPath = path.join(pigletRoot, 'HmbtnTelemetry.cpp');
const wrapperPath = path.join(root, 'build_native_ps4_v2.bat');
const statusPath = path.join(root, 'docs', 'Native-Rebuild-Status.md');

for (const requiredPath of [
    applicationPath,
    inputPath,
    inputHeaderPath,
    buildPath,
    telemetryPath,
    wrapperPath,
]) {
    assert.ok(fs.existsSync(requiredPath), `Missing playable-farm file: ${requiredPath}`);
}

const application = fs.readFileSync(applicationPath, 'utf8');
const input = fs.readFileSync(inputPath, 'utf8');
const build = fs.readFileSync(buildPath, 'utf8');
const telemetry = fs.readFileSync(telemetryPath, 'utf8');
const wrapper = fs.readFileSync(wrapperPath, 'utf8');
const status = fs.readFileSync(statusPath, 'utf8');
assert.match(application, /eglSwapInterval\(s_display, 0\)/);
assert.match(application, /eglSwapBuffers\(s_display, s_surface\)/);
assert.match(application, /hmbtn_fixed_clock_advance/);
assert.match(application, /hmbtn_game_step/);
assert.match(application, /hmbtn_camera_follow/);
assert.match(input, /scePadReadState/);
assert.match(input, /retry_frames_ = 120u/);
assert.match(input, /if \(!Initialize\(\)\)/);
assert.match(input, /ORBIS_PAD_BUTTON_UP/);
assert.match(input, /ORBIS_PAD_BUTTON_CIRCLE/);
assert.doesNotMatch(input, /scePadSetProcessFocus|scePadSetProcessPrivilege|scePadSetForceIntercepted/);
assert.match(build, /-lScePad/);
assert.match(build, /-lSceUserService/);
assert.match(build, /src\\game\.c/);
assert.match(build, /del \/Q "%intdir%\\\*\.o"/);
assert.match(build, /if errorlevel 1 exit \/b 1/);
assert.match(build, /set PKG_VERSION="1\.06"/);
assert.match(build, /set PKG_TITLE="Harvest Moon BTN Native V2 Pad Module Fix"/);
assert.match(build, /set PKG_TITLE_ID="HMBT00002"/);
assert.match(build, /set PKG_CONTENT_ID="UP9000-HMBT00002_00-HMBTNNATIVEV2000"/);
assert.match(telemetry, /\[NATIVE V2 BUILD\] pad-module-fix 1\.06 playable-farm checkpoints/);
assert.match(wrapper, /native_ps4_v2_playable_farm\.test\.js/);
assert.match(wrapper, /HarvestMoonBTN-Native-V2-Pad-Module-Fix\.pkg/);
assert.match(status, /Penyu[^\n]*`1\.4\.0`/i);

console.log('native_ps4_v2_playable_farm.test.js: PASS');
