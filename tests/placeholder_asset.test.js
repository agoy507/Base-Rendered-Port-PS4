const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const root = path.resolve(__dirname, '..');
const sourceRoot = path.join(root, 'native', 'piglet_renderer');
const packagedAssetPath = path.join(sourceRoot, 'assets', 'images', 'rgba_texture.png');
const rendererPath = path.join(sourceRoot, 'piglet', 'PigletApplication.cpp');
const telemetryPath = path.join(sourceRoot, 'piglet', 'PortBaseTelemetry.cpp');
const buildPath = path.join(sourceRoot, 'piglet', 'build.bat');
const wrapperPath = path.join(root, 'build_ps4_port_base.bat');
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
assert.match(telemetry, /\[PS4 PORT BASE\] template scene checkpoints/);
assert.match(build, /set PKG_VERSION="%PORT_VERSION%"/);
assert.match(build, /set PKG_TITLE="%PORT_TITLE%"/);
assert.match(wrapper, /placeholder_asset\.test\.js/);
assert.match(wrapper, /%PORT_TITLE_ID%\.pkg/);

console.log('placeholder_asset.test.js: PASS');
