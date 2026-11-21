const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const root = path.resolve(__dirname, '..');
const sourceRoot = path.join(root, 'native', 'piglet_upstream');
const packagedAssetPath = path.join(sourceRoot, 'assets', 'images', 'rgba_texture.png');
const rendererPath = path.join(sourceRoot, 'piglet', 'PigletApplication.cpp');
const telemetryPath = path.join(sourceRoot, 'piglet', 'HmbtnTelemetry.cpp');
const buildPath = path.join(sourceRoot, 'piglet', 'build.bat');
const wrapperPath = path.join(root, 'build_native_ps4_v2.bat');
const packagedAsset = fs.readFileSync(packagedAssetPath);
assert.ok(packagedAsset.length > 8, 'Packaged texture must not be empty');
assert.deepEqual(
    [...packagedAsset.subarray(0, 8)],
    [0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a],
    'Packaged texture must be a PNG placeholder supplied by the port base',
);

const telemetry = fs.readFileSync(telemetryPath, 'utf8');
const build = fs.readFileSync(buildPath, 'utf8');
const wrapper = fs.readFileSync(wrapperPath, 'utf8');
assert.match(telemetry, /\[NATIVE V2 BUILD\] pad-module-fix 1\.06/);
assert.match(build, /set PKG_VERSION="1\.06"/);
assert.match(build, /set PKG_TITLE="Harvest Moon BTN Native V2 Pad Module Fix"/);
assert.match(wrapper, /native_ps4_v2_project_asset\.test\.js/);
assert.match(wrapper, /HarvestMoonBTN-Native-V2-Pad-Module-Fix\.pkg/);

console.log('native_ps4_v2_project_asset.test.js: PASS');
