const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const root = path.resolve(__dirname, '..');
const sourcePath = path.join(root, 'native', 'platform', 'ps4', 'piglet_probe.c');
const packagingPath = path.join(root, 'native', 'packaging', 'param.sfx');

assert.ok(fs.existsSync(sourcePath), 'Piglet GPU probe source must exist');

const source = fs.readFileSync(sourcePath, 'utf8');
const packaging = fs.readFileSync(packagingPath, 'utf8');

assert.match(source, /#define HMBTN_SPRITE_COUNT 2048u/);
assert.match(source, /#define HMBTN_ATLAS_SIZE 1024u/);
assert.match(source, /OrbisPglWindow render_window\s*=\s*\{[\s\S]*?HMBTN_OUTPUT_WIDTH[\s\S]*?HMBTN_OUTPUT_HEIGHT[\s\S]*?\}/);
assert.match(source, /glViewport\(0, 0, HMBTN_OUTPUT_WIDTH, HMBTN_OUTPUT_HEIGHT\)/);
assert.match(source, /eglSwapInterval\(state->display, 0\)/);
assert.match(source, /eglSwapInterval\(0\) unsupported, continuing/);
assert.match(source, /ORBIS_PGL_FLAGS_USE_FLEXIBLE_MEMORY\s*\|\s*0x60u/);
assert.match(source, /0x60 vendor bits[\s\S]*includes the 0x40 tiled-texture bit by design/);
assert.match(source, /systemSharedMemorySize = 250u \* 1024u \* 1024u/);
assert.match(source, /maxMappedFlexibleMemory = 170u \* 1024u \* 1024u/);
assert.match(source, /drawCommandBufferSize = 1u \* 1024u \* 1024u/);
assert.match(source, /lcueResourceBufferSize = 1u \* 1024u \* 1024u/);
assert.ok(
    source.indexOf('eglSwapInterval(state->display, 0)') <
        source.indexOf('eglChooseConfig('),
    'Piglet sample requests swap interval before choosing the EGL config',
);
assert.match(source, /eglGetDisplay failed/);
assert.match(source, /eglInitialize failed/);
assert.match(source, /eglBindAPI failed/);
assert.match(source, /eglChooseConfig failed/);
assert.match(source, /eglChooseConfig returned no EGL configs/);
assert.match(source, /glGetProgramiv\(state->program, GL_LINK_STATUS/);
assert.match(source, /glGetProgramInfoLog\(/);
assert.match(source, /Shader program link failed/);
assert.match(source, /static const char \*safe_gl_string\(/);
assert.match(source, /Atlas upload failed/);
assert.match(source, /Render GL failed/);
assert.match(source, /glDrawArrays\(GL_TRIANGLES, 0, HMBTN_SPRITE_COUNT \* 6u\)/);
assert.match(source, /eglSwapBuffers\(state->display, state->surface\)/);
assert.match(source, /sceKernelGetProcessTime\(\)/);
assert.match(source, /\/data\/HarvestMoonBTNNative\/perf\.log/);
assert.match(source, /fopen\("\/data\/HarvestMoonBTNNative\/perf\.log", "w"\)/);
assert.match(source, /#define HMBTN_RENDER_MODE_CLEAR_ONLY 1u/);
assert.match(source, /\[NATIVE BUILD\] GPU probe 01\.04 clear-only diagnostics/);
assert.match(source, /System service module result=/);
assert.match(source, /Splash hide pre-render result=/);
assert.match(source, /Splash hide post-swap result=/);
const clearOnlyBranch = source.indexOf('if (HMBTN_RENDER_MODE_CLEAR_ONLY != 0u)');
const shaderUse = source.indexOf('glUseProgram(state->program)');
const drawCall = source.indexOf('glDrawArrays(');
assert.ok(clearOnlyBranch >= 0, 'clear-only branch must exist');
assert.ok(clearOnlyBranch < shaderUse, 'clear-only branch must run before shader use');
assert.ok(clearOnlyBranch < drawCall, 'clear-only branch must run before sprite draw');
assert.match(
    source.slice(clearOnlyBranch, shaderUse),
    /glClearColor\(0\.0f, 0\.25f, 1\.0f, 1\.0f\)[\s\S]*glClear\(GL_COLOR_BUFFER_BIT\)[\s\S]*present_frame\(state\)/,
);
assert.match(packaging, /<param key="APP_VER">01\.04<\/param>/);
assert.match(packaging, /<param key="CATEGORY">gde<\/param>/);
assert.match(packaging, /<param key="VERSION">01\.04<\/param>/);
assert.match(source, /p95=%lluus p99=%lluus/);
assert.match(source, /1%%low=%.2f/);
assert.match(source, /frame_start - previous_frame_start/);
assert.match(source, /frame_samples\[297u\][\s\S]*frame_samples\[298u\][\s\S]*frame_samples\[299u\]/);
assert.match(source, /ORBIS_PAD_BUTTON_L3/);
assert.match(source, /ORBIS_PAD_BUTTON_R3/);
assert.match(source, /\[NATIVE INIT\] context ready/);
assert.match(source, /if \(!initialize\(&state\)\) \{\s*return 0;/);
assert.doesNotMatch(source, /SDL_(?:CreateWindow|CreateRenderer|RenderCopy|RenderPresent)/);

console.log('native_ps4_probe.test.js: PASS');
