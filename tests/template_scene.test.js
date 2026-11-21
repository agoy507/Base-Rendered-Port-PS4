const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const root = path.resolve(__dirname, '..');
const sourceRoot = path.join(root, 'native', 'piglet_renderer');
const pigletRoot = path.join(sourceRoot, 'piglet');
const applicationPath = path.join(pigletRoot, 'PigletApplication.cpp');
const inputPath = path.join(pigletRoot, 'PortBasePs4Input.cpp');
const inputHeaderPath = path.join(pigletRoot, 'PortBasePs4Input.hpp');
const buildPath = path.join(pigletRoot, 'build.bat');
const telemetryPath = path.join(pigletRoot, 'PortBaseTelemetry.cpp');
const wrapperPath = path.join(root, 'build_ps4_port_base.bat');


for (const requiredPath of [
    applicationPath,
    inputPath,
    inputHeaderPath,
    buildPath,
    telemetryPath,
    wrapperPath,
]) {
    assert.ok(fs.existsSync(requiredPath), `Missing template scene file: ${requiredPath}`);
}

const application = fs.readFileSync(applicationPath, 'utf8');
const input = fs.readFileSync(inputPath, 'utf8');
const build = fs.readFileSync(buildPath, 'utf8');
const telemetry = fs.readFileSync(telemetryPath, 'utf8');
const wrapper = fs.readFileSync(wrapperPath, 'utf8');

assert.match(application, /eglSwapInterval\(s_display, 0\)/);
assert.match(application, /eglSwapBuffers\(s_display, s_surface\)/);
assert.match(application, /port_base_fixed_clock_advance/);
assert.match(application, /port_base_game_step/);
assert.match(application, /port_base_camera_follow/);
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
assert.match(build, /set PKG_VERSION="%PORT_VERSION%"/);
assert.match(build, /set PKG_TITLE="%PORT_TITLE%"/);
assert.match(build, /set PKG_TITLE_ID="%PORT_TITLE_ID%"/);
assert.match(build, /set PKG_CONTENT_ID="%PORT_CONTENT_ID%"/);
assert.match(telemetry, /\[PS4 PORT BASE\] template scene checkpoints/);
assert.match(wrapper, /template_scene\.test\.js/);
assert.match(wrapper, /%PORT_TITLE_ID%\.pkg/);


console.log('template_scene.test.js: PASS');
