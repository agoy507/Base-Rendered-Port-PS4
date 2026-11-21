const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const root = path.resolve(__dirname, '..');
const piglet = path.join(root, 'native', 'piglet_upstream', 'piglet');
const telemetryHeader = fs.readFileSync(path.join(piglet, 'HmbtnTelemetry.hpp'), 'utf8');
const telemetrySource = fs.readFileSync(path.join(piglet, 'HmbtnTelemetry.cpp'), 'utf8');
const main = fs.readFileSync(path.join(piglet, 'main.cpp'), 'utf8');
const application = fs.readFileSync(path.join(piglet, 'PigletApplication.cpp'), 'utf8');
const input = fs.readFileSync(path.join(piglet, 'HmbtnPs4Input.cpp'), 'utf8');
const build = fs.readFileSync(path.join(piglet, 'build.bat'), 'utf8');
const wrapper = fs.readFileSync(path.join(root, 'build_native_ps4_v2.bat'), 'utf8');

assert.match(telemetryHeader, /hmbtnTelemetryCheckpoint\(const char \*checkpoint\)/);
assert.match(telemetrySource, /\[NATIVE V2 CHECKPOINT\] %s/);
assert.match(telemetrySource, /fflush\(g_checkpoint_log\)/);
assert.match(main, /main-before-app-init/);
assert.match(main, /app-init-ok/);
assert.match(main, /main-before-first-logic/);
assert.match(main, /main-after-first-logic/);
assert.match(main, /main-before-first-render/);
assert.match(main, /main-after-first-render/);
assert.match(application, /logic-before-pad-init/);
assert.match(application, /logic-after-pad-init/);
assert.match(application, /first-render-before-scene/);
assert.match(application, /first-render-after-scene/);
assert.match(application, /first-render-before-swap/);
assert.match(application, /first-render-after-swap/);
assert.ok(
    input.indexOf('ORBIS_SYSMODULE_INTERNAL_PAD') < input.indexOf('scePadInit()'),
    'Pad sysmodule must load before scePadInit',
);
assert.ok(
    input.indexOf('ORBIS_SYSMODULE_INTERNAL_USER_SERVICE') <
        input.indexOf('sceUserServiceInitialize(&params)'),
    'UserService sysmodule must load before initialization',
);
assert.match(input, /sceSysmoduleLoadModuleInternal/);
assert.match(input, /user-service-module-load-start/);
assert.match(input, /user-service-module-load-return/);
assert.match(input, /pad-module-load-start/);
assert.match(input, /pad-module-load-return/);
assert.match(input, /module-load-failed/);
assert.match(input, /OrbisUserServiceInitializeParams params\{\}/);
assert.match(input, /params\.priority = ORBIS_KERNEL_PRIO_FIFO_LOWEST/);
assert.match(input, /pad-init-start/);
assert.match(input, /pad-init-ok/);
assert.match(input, /user-service-init-start/);
assert.match(input, /user-service-init-return/);
assert.match(input, /initial-user-return/);
assert.match(input, /pad-open-return/);
assert.match(telemetrySource, /\[NATIVE V2 BUILD\] pad-module-fix 1\.06/);
assert.match(build, /set PKG_VERSION="1\.06"/);
assert.match(build, /set PKG_TITLE="Harvest Moon BTN Native V2 Pad Module Fix"/);
assert.match(wrapper, /native_ps4_v2_crash_diag\.test\.js/);
assert.match(wrapper, /HarvestMoonBTN-Native-V2-Pad-Module-Fix\.pkg/);

console.log('native_ps4_v2_crash_diag.test.js: PASS');
