const assert = require('assert');
const fs = require('fs');
const path = require('path');

const root = path.resolve(__dirname, '..');
const generatorPath = path.join(root, 'build_ps4', 'gen_loader.js');
const generator = fs.readFileSync(generatorPath, 'utf8');
const generatedLoader = fs.readFileSync(path.join(root, 'src', 'ps4_loader.c'), 'utf8');
const settings = fs.readFileSync(path.join(root, 'pkg_staging', 'settings.ini'), 'utf8');
const rebuildScript = fs.readFileSync(path.join(root, 'update_rebuild.bat'), 'utf8');
const ps4VideoBackend = fs.readFileSync(
    path.join(root, 'vendor', 'SDL-PS4', 'src', 'video', 'ps4', 'SDL_ps4video.c'),
    'utf8',
);

function functionBody(name) {
    const marker = `${name}(`;
    const start = generator.indexOf(marker);
    assert.notStrictEqual(start, -1, `missing function ${name}`);
    const bodyStart = generator.indexOf('{', start);
    let depth = 0;
    for (let index = bodyStart; index < generator.length; index += 1) {
        if (generator[index] === '{') depth += 1;
        if (generator[index] === '}') {
            depth -= 1;
            if (depth === 0) return generator.slice(bodyStart, index + 1);
        }
    }
    throw new Error(`unterminated function ${name}`);
}

// Circle is a gameplay cancel button. Escape/F1 are host-menu triggers and
// must only be synthesized by the dedicated L3/R3 bindings.
assert.match(
    generator,
    /ORBIS_PAD_BUTTON_CIRCLE[^\n]+SDL_CONTROLLER_BUTTON_B[^\n]+SDLK_UNKNOWN/,
    'Circle must not synthesize a keyboard host-menu key',
);
assert.match(rebuildScript, /libSDL2_optimized\.a/);
assert.match(rebuildScript, /SDL_render_sw\.c[^\r\n]+-o[^\r\n]+SDL_render_sw\.c\.o/);
assert.match(rebuildScript, /SDL_ps4video\.c[^\r\n]+-o[^\r\n]+SDL_ps4video\.c\.o/);
assert.match(rebuildScript, /-O3/);
assert.match(
    generator,
    /ORBIS_PAD_BUTTON_L3[^\n]+SDLK_F1[^\n]+SDL_SCANCODE_F1/,
    'L3 must synthesize F1 for the host menu',
);
assert.match(
    generator,
    /ORBIS_PAD_BUTTON_R3[^\n]+SDLK_F1[^\n]+SDL_SCANCODE_F1/,
    'R3 must synthesize F1 for the host menu',
);
assert.doesNotMatch(
    functionBody('hook_SDL_PollEvent'),
    /SDL_CONTROLLER_BUTTON_B\b[\s\S]{0,160}SDLK_(?:ESCAPE|F1)|jbutton\.button == 1\b[\s\S]{0,160}SDLK_(?:ESCAPE|F1)/,
    'Circle/B paths must never enqueue Escape or F1',
);
assert.match(
    generator,
    /if \(bmaps\[i\]\.sym != SDLK_UNKNOWN\)\s*\{\s*push_keyboard_event/,
    'Buttons without a keyboard binding must not enqueue a keyboard event',
);
const pollEventBody = functionBody('hook_SDL_PollEvent');
assert.match(
    pollEventBody,
    /SDL_CONTROLLER_BUTTON_LEFTSTICK[\s\S]{0,120}SDL_CONTROLLER_BUTTON_RIGHTSTICK[\s\S]{0,240}update_cheat_menu_key\([^;]+, 1\)/,
    'Native L3/R3 controller down events must update the shared F1 state',
);
assert.match(
    pollEventBody,
    /SDL_CONTROLLER_BUTTON_LEFTSTICK[\s\S]{0,120}SDL_CONTROLLER_BUTTON_RIGHTSTICK[\s\S]{0,240}update_cheat_menu_key\([^;]+, 0\)/,
    'Native L3/R3 controller up events must update the shared F1 state',
);
assert.match(
    pollEventBody,
    /SDL_JOYBUTTONDOWN[\s\S]+jbutton\.button == 11[\s\S]{0,100}jbutton\.button == 12[\s\S]{0,240}update_cheat_menu_key\([^;]+, 1\)/,
    'Raw L3/R3 joystick down events must update the shared F1 state',
);
assert.match(
    pollEventBody,
    /SDL_JOYBUTTONUP[\s\S]+jbutton\.button == 11[\s\S]{0,100}jbutton\.button == 12[\s\S]{0,240}update_cheat_menu_key\([^;]+, 0\)/,
    'Raw L3/R3 joystick up events must update the shared F1 state',
);
const cheatStateBody = functionBody('update_cheat_menu_key');
assert.match(cheatStateBody, /previous_sources == 0 && s_cheat_sources != 0/);
assert.match(cheatStateBody, /previous_sources != 0 && s_cheat_sources == 0/);
assert.match(cheatStateBody, /push_keyboard_event\(SDL_KEYDOWN, SDLK_F1, SDL_SCANCODE_F1\)/);
assert.match(cheatStateBody, /push_keyboard_event\(SDL_KEYUP, SDLK_F1, SDL_SCANCODE_F1\)/);
assert.match(generator, /ORBIS_PAD_BUTTON_CROSS[^\n]+SDL_CONTROLLER_BUTTON_A/);
assert.match(generator, /ORBIS_PAD_BUTTON_OPTIONS[^\n]+SDL_CONTROLLER_BUTTON_START/);
assert.match(generator, /ORBIS_PAD_BUTTON_(?:UP|DOWN|LEFT|RIGHT)[^\n]+SDL_CONTROLLER_BUTTON_DPAD_/);

// The OpenOrbis renderer is software-only. Keep the sharper scale-3 source,
// but bypass the generic software copy/convert path for full-frame presents.
assert.match(generator, /SDL_HINT_RENDER_SCALE_QUALITY, "nearest"/);
assert.match(generator, /SDL_RenderSetIntegerScale\(renderer, SDL_FALSE\)/);
assert.doesNotMatch(functionBody('hook_SDL_RenderPresent'), /SDL_Delay\s*\(/);
assert.match(generator, /migrate_ps4_performance_settings\s*\(/);
assert.ok(
    generator.lastIndexOf('migrate_ps4_performance_settings();') <
        generator.lastIndexOf('pSDL_main(4, game_argv)'),
    'Performance migration must run before SDL_main',
);
assert.match(generator, /\.performance-profile-v3/);
assert.match(generator, /fputs\("resolution=3\\\\n", dst\)/);
assert.match(settings, /^resolution=3$/m);
assert.match(settings, /^smooth=0$/m);
const directBlitBody = functionBody('direct_blit_argb8888_to_bgr888');
assert.match(directBlitBody, /SDL_PIXELFORMAT_BGR888/);
assert.match(directBlitBody, /SDL_UpdateWindowSurface\(g_ps4_window\)/);
assert.match(directBlitBody, /x_step[\s\S]+x_acc[\s\S]+>> 32/);
assert.match(
    directBlitBody,
    /\(pixel & 0x000000FFu\) << 16[\s\S]+pixel & 0x0000FF00u[\s\S]+\(pixel & 0x00FF0000u\) >> 16/,
    'ARGB8888 to BGR888 conversion must preserve red and blue channels',
);
assert.match(
    functionBody('hook_SDL_UpdateTexture'),
    /SDL_UpdateTexture\(texture, rect, pixels, pitch\)[\s\S]+s_fast_pixels = pixels/,
    'Fast path must retain the SDL upload so unsupported draws can fall back safely',
);
assert.match(
    functionBody('hook_SDL_RenderCopy'),
    /srcrect == NULL && dstrect == NULL[\s\S]+s_fast_copy_requested = 1/,
    'Only a full-frame copy may use the direct path',
);
assert.match(
    functionBody('hook_SDL_RenderPresent'),
    /direct_blit_argb8888_to_bgr888\(\)[\s\S]+SDL_RenderPresent\(renderer\)/,
    'Present must use the direct path with the original SDL path as fallback',
);
assert.doesNotMatch(
    ps4VideoBackend,
    /memset\(pDst,\s*0,\s*BufferSize\(_this\)\)/,
    'Fullscreen present must not clear a framebuffer that is overwritten immediately',
);
assert.match(
    generator,
    /0xE8,\s*0x83,\s*0xFF,\s*0x2A,\s*0x00[\s\S]{0,500}memset\(mapped_base \+ 0x8660, 0x90, 5\)/,
    'The exact post-present SDL_Delay(16) call must be signature-checked and removed',
);
assert.match(
    functionBody('hook_SDL_RenderPresent'),
    /update=[^\\n]+copy=[^\\n]+present=/,
    'Performance log must split update/copy/present costs',
);

// The cheat-menu language option persists through lang.txt. A hard-coded
// environment override would make the menu appear to work but lose on restart.
assert.match(generator, /load_language_preference\s*\(/);
assert.doesNotMatch(generator, /setenv\("HMBTN_LANG",\s*"id"/);
assert.match(generator, /strcmp\(name, "HMBTN_LANG"\)/);
assert.match(generator, /HM_DATA_ROOT "data\/lang\.txt"/);
assert.match(generator, /"--lang"[\s\S]{0,120}\(char\*\)selected_language/);
assert.match(generator, /int ret = pSDL_main\(4, game_argv\)/);
assert.match(
    functionBody('hook___getmainargs'),
    /"--lang"[\s\S]{0,160}load_language_preference\(\)[\s\S]{0,80}\*argc = 4/,
);

// The checked-in generated C must contain the same critical behavior as its
// canonical generator so a stale loader cannot silently enter a package.
for (const marker of [
    'migrate_ps4_performance_settings();',
    'Language preference from cheat menu',
    'SDL_CONTROLLER_BUTTON_LEFTSTICK,     SDLK_F1',
    'SDL_CONTROLLER_BUTTON_B,             SDLK_UNKNOWN',
    'update_cheat_menu_key',
    'Removed redundant post-present SDL_Delay(16)',
]) {
    assert.ok(generatedLoader.includes(marker), `generated loader is missing: ${marker}`);
}

console.log('PS4 loader regression checks passed.');
