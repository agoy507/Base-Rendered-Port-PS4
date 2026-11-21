const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const root = path.resolve(__dirname, '..');
const sourceRoot = path.join(root, 'native', 'piglet_renderer', 'piglet');
const rendererPath = path.join(sourceRoot, 'PigletApplication.cpp');
const mainPath = path.join(sourceRoot, 'main.cpp');
const telemetryHeaderPath = path.join(sourceRoot, 'PortBaseTelemetry.hpp');
const telemetrySourcePath = path.join(sourceRoot, 'PortBaseTelemetry.cpp');
const buildPath = path.join(sourceRoot, 'build.bat');
const wrapperPath = path.join(root, 'build_ps4_port_base.bat');

for (const requiredPath of [
    rendererPath,
    mainPath,
    telemetryHeaderPath,
    telemetrySourcePath,
    buildPath,
    wrapperPath,
]) {
    assert.ok(fs.existsSync(requiredPath), `Missing V2 telemetry file: ${requiredPath}`);
}

const renderer = fs.readFileSync(rendererPath, 'utf8');
const main = fs.readFileSync(mainPath, 'utf8');
const telemetry = fs.readFileSync(telemetrySourcePath, 'utf8');
const build = fs.readFileSync(buildPath, 'utf8');
const wrapper = fs.readFileSync(wrapperPath, 'utf8');

assert.match(renderer, /eglSwapInterval\(s_display, 0\)/);
assert.match(renderer, /eglSwapBuffers\(s_display, s_surface\)/);
assert.match(main, /#include "PortBaseTelemetry\.hpp"/);
assert.match(main, /port_baseTelemetryInitialize\(\)/);
assert.match(main, /port_baseTelemetryRecordFrame\(/);
assert.match(main, /AppRef\.Render\(\)/);
assert.ok(
    main.indexOf('port_baseTelemetryRecordFrame(') < main.indexOf('AppRef.Render()'),
    'frame start must be sampled before the unchanged upstream Render call',
);
assert.match(telemetry, /#define PORT_BASE_V2_PROFILE_WINDOW 300u/);
assert.match(telemetry, /#define PORT_BASE_V2_FLUSH_WINDOWS 10u/);
assert.match(telemetry, /sceKernelGetProcessTime\(\)/);
assert.match(telemetry, /frame_start - state\.previous_frame_start/);
assert.match(telemetry, /std::sort\(/);
assert.match(telemetry, /sorted_samples\[284u\]/);
assert.match(telemetry, /sorted_samples\[296u\]/);
assert.match(telemetry, /sorted_samples\[297u\][\s\S]*sorted_samples\[298u\][\s\S]*sorted_samples\[299u\]/);
assert.match(telemetry, /#define PORT_BASE_DATA_DIR "\/data\/Ps4PortBase"/);
assert.match(telemetry, /snprintf\(log_path, sizeof\(log_path\), "%s\/perf\.log", PORT_BASE_DATA_DIR\)/);
assert.match(telemetry, /fopen\(log_path, "w"\)/);
assert.match(telemetry, /\[PS4 PORT BASE\] template scene checkpoints/);
assert.match(telemetry, /\[PS4 PORT BASE PERF\]/);
assert.match(telemetry, /windows_since_flush >= PORT_BASE_V2_FLUSH_WINDOWS/);
assert.doesNotMatch(telemetry, /next\.previous_frame_start = 0u/);
assert.match(build, /set PKG_VERSION="%PORT_VERSION%"/);
assert.match(build, /PORT_BASE_DATA_DIR/);
assert.match(wrapper, /port_telemetry\.test\.js/);
assert.match(wrapper, /%PORT_TITLE_ID%\.pkg/);

console.log('port_telemetry.test.js: PASS');
