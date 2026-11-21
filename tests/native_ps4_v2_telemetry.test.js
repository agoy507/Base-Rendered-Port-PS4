const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const root = path.resolve(__dirname, '..');
const sourceRoot = path.join(root, 'native', 'piglet_upstream', 'piglet');
const rendererPath = path.join(sourceRoot, 'PigletApplication.cpp');
const mainPath = path.join(sourceRoot, 'main.cpp');
const telemetryHeaderPath = path.join(sourceRoot, 'HmbtnTelemetry.hpp');
const telemetrySourcePath = path.join(sourceRoot, 'HmbtnTelemetry.cpp');
const buildPath = path.join(sourceRoot, 'build.bat');
const wrapperPath = path.join(root, 'build_native_ps4_v2.bat');

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
assert.match(main, /#include "HmbtnTelemetry\.hpp"/);
assert.match(main, /hmbtnTelemetryInitialize\(\)/);
assert.match(main, /hmbtnTelemetryRecordFrame\(/);
assert.match(main, /AppRef\.Render\(\)/);
assert.ok(
    main.indexOf('hmbtnTelemetryRecordFrame(') < main.indexOf('AppRef.Render()'),
    'frame start must be sampled before the unchanged upstream Render call',
);
assert.match(telemetry, /#define HMBTN_V2_PROFILE_WINDOW 300u/);
assert.match(telemetry, /#define HMBTN_V2_FLUSH_WINDOWS 10u/);
assert.match(telemetry, /sceKernelGetProcessTime\(\)/);
assert.match(telemetry, /frame_start - state\.previous_frame_start/);
assert.match(telemetry, /std::sort\(/);
assert.match(telemetry, /sorted_samples\[284u\]/);
assert.match(telemetry, /sorted_samples\[296u\]/);
assert.match(telemetry, /sorted_samples\[297u\][\s\S]*sorted_samples\[298u\][\s\S]*sorted_samples\[299u\]/);
assert.match(telemetry, /\/data\/HarvestMoonBTNNativeV2\/perf\.log/);
assert.match(telemetry, /fopen\([^;]*perf\.log", "w"\)/);
assert.match(telemetry, /\[NATIVE V2 BUILD\] pad-module-fix 1\.06 playable-farm checkpoints/);
assert.match(telemetry, /\[NATIVE V2 PERF\]/);
assert.match(telemetry, /windows_since_flush >= HMBTN_V2_FLUSH_WINDOWS/);
assert.doesNotMatch(telemetry, /next\.previous_frame_start = 0u/);
assert.match(build, /set PKG_VERSION="1\.\d+"/);
assert.match(build, /set PKG_TITLE="Harvest Moon BTN Native V2 [^"]+"/);
assert.match(wrapper, /native_ps4_v2_telemetry\.test\.js/);
assert.match(wrapper, /HarvestMoonBTN-Native-V2-[A-Za-z-]+\.pkg/);

console.log('native_ps4_v2_telemetry.test.js: PASS');
