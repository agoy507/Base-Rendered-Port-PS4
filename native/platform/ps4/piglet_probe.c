#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <orbis/Pad.h>
#include <orbis/Pigletv2VSH.h>
#include <orbis/Sysmodule.h>
#include <orbis/SystemService.h>
#include <orbis/UserService.h>
#include <orbis/libkernel.h>

#include "port_base/core.h"

#define PORT_BASE_SPRITE_COUNT 2048u
#define PORT_BASE_VERTICES_PER_SPRITE 6u
#define PORT_BASE_FLOATS_PER_VERTEX 4u
#define PORT_BASE_PROFILE_WINDOW 300u
#define PORT_BASE_ATLAS_SIZE 1024u
#define PORT_BASE_RENDER_MODE_CLEAR_ONLY 1u

static FILE *g_native_log = NULL;

static void native_log(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);

    if (g_native_log != NULL) {
        va_start(args, format);
        vfprintf(g_native_log, format, args);
        va_end(args);
        fflush(g_native_log);
    }
}

static void initialize_logging(void) {
    mkdir("/data/Ps4PortBase", 0777);
    g_native_log = fopen("/data/Ps4PortBase/perf.log", "w");
}

static int compare_u64(const void *left, const void *right) {
    const uint64_t a = *(const uint64_t *)left;
    const uint64_t b = *(const uint64_t *)right;
    return (a > b) - (a < b);
}

static const char *safe_gl_string(GLenum name) {
    const GLubyte *value = glGetString(name);
    return value != NULL ? (const char *)value : "(unavailable)";
}

typedef struct PortBaseShaderBlob {
    char *ident;
    unsigned char hash[16];
    uint64_t len;
    unsigned char *code;
} PortBaseShaderBlob;

extern PortBaseShaderBlob scePrecompiledShaderEntries[];

typedef struct PortBasePigletState {
    int32_t piglet_module;
    int32_t shader_module;
    EGLDisplay display;
    EGLSurface surface;
    EGLContext context;
    GLuint program;
    GLuint texture;
    GLint model_uniform;
    GLint projection_uniform;
    GLint texture_matrix_uniform;
    GLint sampler_uniform;
    GLint opacity_uniform;
    GLint vertex_attribute;
    GLfloat *vertices;
    int32_t pad_handle;
    bool cheat_visible;
    bool cheat_latched;
    bool splash_hide_retried;
} PortBasePigletState;

static void identity_matrix(GLfloat matrix[16]) {
    memset(matrix, 0, sizeof(GLfloat) * 16u);
    matrix[0] = 1.0f;
    matrix[5] = 1.0f;
    matrix[10] = 1.0f;
    matrix[15] = 1.0f;
}

static void projection_1080p(GLfloat matrix[16]) {
    identity_matrix(matrix);
    matrix[0] = 2.0f / (GLfloat)PORT_BASE_OUTPUT_WIDTH;
    matrix[5] = -2.0f / (GLfloat)PORT_BASE_OUTPUT_HEIGHT;
    matrix[12] = -1.0f;
    matrix[13] = 1.0f;
}

static const PortBaseShaderBlob *find_shader(const char *name) {
    PortBaseShaderBlob *entry = scePrecompiledShaderEntries;
    while (entry->ident != NULL) {
        if (strcmp(entry->ident, name) == 0) {
            return entry;
        }
        ++entry;
    }
    return NULL;
}

static bool load_piglet_modules(PortBasePigletState *state) {
    const char *sandbox = sceKernelGetFsSandboxRandomWord();
    char module_path[256];
    int module_start_result = 0;
    if (sandbox == NULL) {
        return false;
    }

    snprintf(
        module_path,
        sizeof(module_path),
        "/%s/common/lib/libScePigletv2VSH.sprx",
        sandbox);
    state->piglet_module = (int32_t)sceKernelLoadStartModule(
        module_path, 0u, NULL, 0u, NULL, &module_start_result);
    if (state->piglet_module < 0) {
        native_log("[NATIVE] Piglet module failed: 0x%08X\n", state->piglet_module);
        return false;
    }

    snprintf(
        module_path,
        sizeof(module_path),
        "/%s/common/lib/libScePrecompiledShaders.sprx",
        sandbox);
    state->shader_module = (int32_t)sceKernelLoadStartModule(
        module_path, 0u, NULL, 0u, NULL, &module_start_result);
    if (state->shader_module < 0) {
        native_log("[NATIVE] Precompiled shader module failed: 0x%08X\n", state->shader_module);
        return false;
    }
    return true;
}

static bool create_context(PortBasePigletState *state) {
    OrbisPglConfig configuration;
    OrbisPglWindow render_window = {
        0,
        PORT_BASE_OUTPUT_WIDTH,
        PORT_BASE_OUTPUT_HEIGHT,
        0,
    };
    EGLConfig config = NULL;
    EGLint config_count = 0;
    EGLint major = 0;
    EGLint minor = 0;
    const EGLint config_attributes[] = {
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 0,
        EGL_STENCIL_SIZE, 0,
        EGL_SAMPLE_BUFFERS, 0,
        EGL_SAMPLES, 0,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_NONE,
    };
    const EGLint context_attributes[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE,
    };
    const EGLint window_attributes[] = {
        EGL_RENDER_BUFFER, EGL_BACK_BUFFER,
        EGL_NONE,
    };

    memset(&configuration, 0, sizeof(configuration));
    configuration.size = sizeof(configuration);
    /* Keep the official OpenOrbis Piglet sample's exact 0x60 vendor bits.
       This value includes the 0x40 tiled-texture bit by design. */
    configuration.flags =
        ORBIS_PGL_FLAGS_USE_COMPOSITE_EXT |
        ORBIS_PGL_FLAGS_USE_FLEXIBLE_MEMORY |
        0x60u;
    configuration.processOrder = 1u;
    configuration.systemSharedMemorySize = 250u * 1024u * 1024u;
    configuration.videoSharedMemorySize = 512u * 1024u * 1024u;
    configuration.maxMappedFlexibleMemory = 170u * 1024u * 1024u;
    configuration.drawCommandBufferSize = 1u * 1024u * 1024u;
    configuration.lcueResourceBufferSize = 1u * 1024u * 1024u;
    configuration.dbgPosCmd_0x40 = PORT_BASE_OUTPUT_WIDTH;
    configuration.dbgPosCmd_0x44 = PORT_BASE_OUTPUT_HEIGHT;
    configuration.unk_0x5C = 2u;

    if (!scePigletSetConfigurationVSH(&configuration)) {
        native_log("[NATIVE INIT] scePigletSetConfigurationVSH failed\n");
        return false;
    }
    native_log("[NATIVE INIT] Piglet configuration accepted\n");

    state->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (state->display == EGL_NO_DISPLAY) {
        native_log(
            "[NATIVE INIT] eglGetDisplay failed: error=0x%08X\n",
            eglGetError());
        return false;
    }
    native_log("[NATIVE INIT] EGL display ready\n");

    if (!eglInitialize(state->display, &major, &minor)) {
        native_log(
            "[NATIVE INIT] eglInitialize failed: error=0x%08X\n",
            eglGetError());
        return false;
    }
    native_log("[NATIVE INIT] EGL initialized: %d.%d\n", major, minor);

    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        native_log(
            "[NATIVE INIT] eglBindAPI failed: error=0x%08X\n",
            eglGetError());
        return false;
    }
    native_log("[NATIVE INIT] OpenGL ES API bound\n");

    if (!eglSwapInterval(state->display, 0)) {
        native_log(
            "[NATIVE INIT] eglSwapInterval(0) unsupported, continuing: error=0x%08X\n",
            eglGetError());
    }

    const EGLBoolean choose_config_result = eglChooseConfig(
        state->display,
        config_attributes,
        &config,
        1,
        &config_count);
    if (!choose_config_result) {
        native_log(
            "[NATIVE INIT] eglChooseConfig failed: result=%u error=0x%08X configs=%d\n",
            (unsigned int)choose_config_result,
            eglGetError(),
            config_count);
        return false;
    }
    if (config_count < 1) {
        native_log(
            "[NATIVE INIT] eglChooseConfig returned no EGL configs: error=0x%08X\n",
            eglGetError());
        return false;
    }
    native_log("[NATIVE INIT] EGL config ready: configs=%d\n", config_count);

    state->surface = eglCreateWindowSurface(
        state->display, config, &render_window, window_attributes);
    state->context = eglCreateContext(
        state->display, config, EGL_NO_CONTEXT, context_attributes);
    if (state->surface == EGL_NO_SURFACE || state->context == EGL_NO_CONTEXT) {
        native_log(
            "[NATIVE INIT] EGL surface/context failed: error=0x%08X\n",
            eglGetError());
        return false;
    }
    if (!eglMakeCurrent(
            state->display,
            state->surface,
            state->surface,
            state->context)) {
        native_log(
            "[NATIVE INIT] eglMakeCurrent failed: error=0x%08X\n",
            eglGetError());
        return false;
    }

    native_log(
        "[NATIVE] Piglet EGL %d.%d, GL=%s, renderer=%s\n",
        major,
        minor,
        safe_gl_string(GL_VERSION),
        safe_gl_string(GL_RENDERER));
    return true;
}

static bool create_program(PortBasePigletState *state) {
    const PortBaseShaderBlob *vertex_blob = find_shader("texmap/v_2.vert");
    const PortBaseShaderBlob *fragment_blob = find_shader("texmap/f_2.frag");
    if (vertex_blob == NULL || fragment_blob == NULL) {
        native_log("[NATIVE INIT] Precompiled shader blobs not found\n");
        return false;
    }

    const GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    const GLuint fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    if (vertex_shader == 0u || fragment_shader == 0u) {
        native_log(
            "[NATIVE INIT] Shader object creation failed: GL=0x%08X\n",
            (unsigned int)glGetError());
        return false;
    }

    glShaderBinary(
        1, &vertex_shader, 0, vertex_blob->code, (GLsizei)vertex_blob->len);
    glShaderBinary(
        1, &fragment_shader, 0, fragment_blob->code, (GLsizei)fragment_blob->len);

    const GLenum binary_error = glGetError();
    if (binary_error != GL_NO_ERROR) {
        native_log(
            "[NATIVE INIT] Shader binary load failed: GL=0x%08X\n",
            (unsigned int)binary_error);
        glDeleteShader(vertex_shader);
        glDeleteShader(fragment_shader);
        return false;
    }

    state->program = glCreateProgram();
    if (state->program == 0u) {
        native_log(
            "[NATIVE INIT] Program creation failed: GL=0x%08X\n",
            (unsigned int)glGetError());
        glDeleteShader(vertex_shader);
        glDeleteShader(fragment_shader);
        return false;
    }
    glAttachShader(state->program, vertex_shader);
    glAttachShader(state->program, fragment_shader);
    glLinkProgram(state->program);

    GLint link_status = GL_FALSE;
    glGetProgramiv(state->program, GL_LINK_STATUS, &link_status);
    if (link_status != GL_TRUE) {
        char info_log[512] = {0};
        GLsizei info_length = 0;
        glGetProgramInfoLog(
            state->program,
            (GLsizei)sizeof(info_log),
            &info_length,
            info_log);
        native_log(
            "[NATIVE INIT] Shader program link failed: GL=0x%08X log=%s\n",
            (unsigned int)glGetError(),
            info_length > 0 ? info_log : "(empty)");
        glDeleteShader(vertex_shader);
        glDeleteShader(fragment_shader);
        return false;
    }
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    state->model_uniform = glGetUniformLocation(state->program, "u_modelViewMatrix");
    state->projection_uniform =
        glGetUniformLocation(state->program, "u_projectionMatrix");
    state->texture_matrix_uniform =
        glGetUniformLocation(state->program, "u_textureSpaceMatrix");
    state->sampler_uniform = glGetUniformLocation(state->program, "s_sampler");
    state->opacity_uniform = glGetUniformLocation(state->program, "u_opacity");
    state->vertex_attribute = glGetAttribLocation(state->program, "a_vertex");

    const GLenum location_error = glGetError();
    if (location_error != GL_NO_ERROR) {
        native_log(
            "[NATIVE INIT] Shader location lookup failed: GL=0x%08X\n",
            (unsigned int)location_error);
        return false;
    }
    if (state->model_uniform < 0 ||
        state->projection_uniform < 0 ||
        state->texture_matrix_uniform < 0 ||
        state->sampler_uniform < 0 ||
        state->opacity_uniform < 0 ||
        state->vertex_attribute < 0) {
        native_log(
            "[NATIVE INIT] Required shader location missing: model=%d projection=%d texture=%d sampler=%d opacity=%d vertex=%d\n",
            state->model_uniform,
            state->projection_uniform,
            state->texture_matrix_uniform,
            state->sampler_uniform,
            state->opacity_uniform,
            state->vertex_attribute);
        return false;
    }
    return true;
}

static bool create_texture(PortBasePigletState *state) {
    const size_t atlas_bytes =
        (size_t)PORT_BASE_ATLAS_SIZE * PORT_BASE_ATLAS_SIZE * 4u;
    uint8_t *pixels = (uint8_t *)malloc(atlas_bytes);
    if (pixels == NULL) {
        return false;
    }
    for (uint32_t y = 0u; y < PORT_BASE_ATLAS_SIZE; ++y) {
        for (uint32_t x = 0u; x < PORT_BASE_ATLAS_SIZE; ++x) {
            const size_t offset = ((size_t)y * PORT_BASE_ATLAS_SIZE + x) * 4u;
            const uint8_t checker = (uint8_t)(((x >> 5u) ^ (y >> 5u)) & 1u);
            pixels[offset + 0u] = checker != 0u ? 0x52u : 0xD9u;
            pixels[offset + 1u] = checker != 0u ? 0xB7u : 0xB4u;
            pixels[offset + 2u] = checker != 0u ? 0x48u : 0x4Au;
            pixels[offset + 3u] = (uint8_t)(96u + ((x + y) & 0x9Fu));
        }
    }

    glGenTextures(1, &state->texture);
    glBindTexture(GL_TEXTURE_2D, state->texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
        PORT_BASE_ATLAS_SIZE,
        PORT_BASE_ATLAS_SIZE,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    const GLenum upload_error = glGetError();
    free(pixels);
    if (upload_error != GL_NO_ERROR) {
        native_log(
            "[NATIVE INIT] Atlas upload failed: GL=0x%08X\n",
            (unsigned int)upload_error);
        return false;
    }
    return true;
}

static void write_vertex(
    GLfloat **cursor,
    GLfloat x,
    GLfloat y,
    GLfloat u,
    GLfloat v) {
    *(*cursor)++ = x;
    *(*cursor)++ = y;
    *(*cursor)++ = u;
    *(*cursor)++ = v;
}

static bool create_sprite_batch(PortBasePigletState *state) {
    const size_t float_count =
        PORT_BASE_SPRITE_COUNT * PORT_BASE_VERTICES_PER_SPRITE *
        PORT_BASE_FLOATS_PER_VERTEX;
    state->vertices = (GLfloat *)malloc(float_count * sizeof(GLfloat));
    if (state->vertices == NULL) {
        return false;
    }

    GLfloat *cursor = state->vertices;
    for (uint32_t index = 0u; index < PORT_BASE_SPRITE_COUNT; ++index) {
        const GLfloat x0 = (GLfloat)((index % 64u) * 30u);
        const GLfloat y0 = (GLfloat)((index / 64u) * 30u);
        const GLfloat x1 = x0 + 29.0f;
        const GLfloat y1 = y0 + 29.0f;
        write_vertex(&cursor, x0, y1, 0.0f, 1.0f);
        write_vertex(&cursor, x1, y0, 1.0f, 0.0f);
        write_vertex(&cursor, x0, y0, 0.0f, 0.0f);
        write_vertex(&cursor, x0, y1, 0.0f, 1.0f);
        write_vertex(&cursor, x1, y1, 1.0f, 1.0f);
        write_vertex(&cursor, x1, y0, 1.0f, 0.0f);
    }
    return true;
}

static void initialize_pad(PortBasePigletState *state) {
    int32_t user_id = -1;
    state->pad_handle = -1;
    sceUserServiceInitialize(NULL);
    if (sceUserServiceGetInitialUser(&user_id) < 0 || scePadInit() < 0) {
        return;
    }
    state->pad_handle = scePadOpen(user_id, 0, 0, NULL);
}

static void update_pad(PortBasePigletState *state) {
    OrbisPadData pad;
    if (state->pad_handle < 0 || scePadReadState(state->pad_handle, &pad) < 0) {
        return;
    }
    const uint32_t cheat_chord = ORBIS_PAD_BUTTON_L3 | ORBIS_PAD_BUTTON_R3;
    const bool chord_held = (pad.buttons & cheat_chord) == cheat_chord;
    if (chord_held && !state->cheat_latched) {
        state->cheat_visible = !state->cheat_visible;
        native_log("[NATIVE] Cheat menu probe: %s\n", state->cheat_visible ? "ON" : "OFF");
    }
    state->cheat_latched = chord_held;
}

static void initialize_system_output(void) {
    const int32_t module_result =
        sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_SYSTEM_SERVICE);
    native_log(
        "[NATIVE INIT] System service module result=0x%08X\n",
        (unsigned int)module_result);

    const int32_t hide_result = sceSystemServiceHideSplashScreen();
    native_log(
        "[NATIVE INIT] Splash hide pre-render result=0x%08X\n",
        (unsigned int)hide_result);
}

static bool present_frame(PortBasePigletState *state) {
    const GLenum render_error = glGetError();
    if (render_error != GL_NO_ERROR) {
        native_log(
            "[NATIVE] Render GL failed: GL=0x%08X\n",
            (unsigned int)render_error);
        return false;
    }
    if (!eglSwapBuffers(state->display, state->surface)) {
        native_log(
            "[NATIVE] eglSwapBuffers failed: EGL=0x%08X\n",
            (unsigned int)eglGetError());
        return false;
    }
    if (!state->splash_hide_retried) {
        const int32_t hide_result = sceSystemServiceHideSplashScreen();
        native_log(
            "[NATIVE INIT] Splash hide post-swap result=0x%08X\n",
            (unsigned int)hide_result);
        state->splash_hide_retried = true;
    }
    return true;
}

static bool render_frame(PortBasePigletState *state) {
    GLfloat identity[16];
    GLfloat projection[16];
    identity_matrix(identity);
    projection_1080p(projection);

    glViewport(0, 0, PORT_BASE_OUTPUT_WIDTH, PORT_BASE_OUTPUT_HEIGHT);
    if (PORT_BASE_RENDER_MODE_CLEAR_ONLY != 0u) {
        glDisable(GL_BLEND);
        glClearColor(0.0f, 0.25f, 1.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        return present_frame(state);
    }
    if (state->cheat_visible) {
        glClearColor(0.55f, 0.12f, 0.03f, 1.0f);
    } else {
        glClearColor(0.03f, 0.15f, 0.55f, 1.0f);
    }
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(state->program);
    glUniformMatrix4fv(state->model_uniform, 1, GL_FALSE, identity);
    glUniformMatrix4fv(state->projection_uniform, 1, GL_FALSE, projection);
    glUniformMatrix4fv(state->texture_matrix_uniform, 1, GL_FALSE, identity);
    glUniform1f(state->opacity_uniform, state->cheat_visible ? 0.72f : 1.0f);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, state->texture);
    glUniform1i(state->sampler_uniform, 0);
    glVertexAttribPointer(
        state->vertex_attribute,
        4,
        GL_FLOAT,
        GL_FALSE,
        PORT_BASE_FLOATS_PER_VERTEX * sizeof(GLfloat),
        state->vertices);
    glEnableVertexAttribArray(state->vertex_attribute);
    glDrawArrays(GL_TRIANGLES, 0, PORT_BASE_SPRITE_COUNT * 6u);
    return present_frame(state);
}

static bool initialize(PortBasePigletState *state) {
    memset(state, 0, sizeof(*state));
    state->piglet_module = -1;
    state->shader_module = -1;
    state->display = EGL_NO_DISPLAY;
    state->surface = EGL_NO_SURFACE;
    state->context = EGL_NO_CONTEXT;
    state->pad_handle = -1;

    if (!load_piglet_modules(state)) {
        native_log("[NATIVE INIT] module loading failed\n");
        return false;
    }
    native_log("[NATIVE INIT] modules ready\n");

    if (!create_context(state)) {
        native_log("[NATIVE INIT] context failed\n");
        return false;
    }
    native_log("[NATIVE INIT] context ready\n");

    if (!create_program(state)) {
        native_log("[NATIVE INIT] shader program failed\n");
        return false;
    }
    native_log("[NATIVE INIT] shader program ready\n");

    if (!create_texture(state)) {
        native_log("[NATIVE INIT] atlas failed\n");
        return false;
    }
    native_log("[NATIVE INIT] atlas ready\n");

    if (!create_sprite_batch(state)) {
        native_log("[NATIVE INIT] sprite batch allocation failed\n");
        return false;
    }
    native_log("[NATIVE INIT] sprite batch ready\n");
    initialize_system_output();
    initialize_pad(state);
    native_log("[NATIVE] 1080p GPU probe ready: %u batched sprites, target=%u FPS\n",
           PORT_BASE_SPRITE_COUNT, PORT_BASE_TARGET_FPS);
    return true;
}

int main(void) {
    PortBasePigletState state;
    uint64_t frame_samples[PORT_BASE_PROFILE_WINDOW];
    uint64_t previous_frame_start = 0u;
    uint64_t total_frame_us = 0u;
    uint64_t maximum_frame_us = 0u;
    uint32_t profiled_frames = 0u;
    uint32_t frames_over_20ms = 0u;
    uint32_t frames_over_budget = 0u;

    initialize_logging();
    native_log("[NATIVE BUILD] GPU probe 01.04 clear-only diagnostics\n");
    if (!initialize(&state)) {
        return 0;
    }

    for (;;) {
        const uint64_t frame_start = sceKernelGetProcessTime();
        const uint64_t frame_interval_us =
            previous_frame_start > 0u ? frame_start - previous_frame_start : 0u;
        previous_frame_start = frame_start;
        update_pad(&state);
        if (!render_frame(&state)) {
            native_log("[NATIVE] Render loop stopped after backend failure\n");
            return 2;
        }

        if (frame_interval_us == 0u) {
            continue;
        }

        frame_samples[profiled_frames] = frame_interval_us;
        total_frame_us += frame_interval_us;
        maximum_frame_us =
            frame_interval_us > maximum_frame_us ? frame_interval_us : maximum_frame_us;
        frames_over_20ms +=
            frame_interval_us > PORT_BASE_MAX_FRAME_TIME_US ? 1u : 0u;
        frames_over_budget +=
            frame_interval_us > PORT_BASE_SIMULATION_STEP_US ? 1u : 0u;
        ++profiled_frames;

        if (profiled_frames == PORT_BASE_PROFILE_WINDOW) {
            qsort(
                frame_samples,
                PORT_BASE_PROFILE_WINDOW,
                sizeof(frame_samples[0]),
                compare_u64);
            const uint64_t average_us = total_frame_us / PORT_BASE_PROFILE_WINDOW;
            const uint64_t p95_us = frame_samples[284u];
            const uint64_t p99_us = frame_samples[296u];
            const uint64_t slowest_one_percent_us =
                (frame_samples[297u] + frame_samples[298u] + frame_samples[299u]) / 3u;
            const double fps = average_us > 0u ? 1000000.0 / (double)average_us : 0.0;
            const double one_percent_low =
                slowest_one_percent_us > 0u
                    ? 1000000.0 / (double)slowest_one_percent_us
                    : 0.0;
            native_log(
                "[NATIVE PERF] 1080p sprites=%u avg=%lluus p95=%lluus p99=%lluus max=%lluus fps=%.2f 1%%low=%.2f missed=%u/%u over20ms=%u/%u\n",
                PORT_BASE_SPRITE_COUNT,
                (unsigned long long)average_us,
                (unsigned long long)p95_us,
                (unsigned long long)p99_us,
                (unsigned long long)maximum_frame_us,
                fps,
                one_percent_low,
                frames_over_budget,
                PORT_BASE_PROFILE_WINDOW,
                frames_over_20ms,
                PORT_BASE_PROFILE_WINDOW);
            total_frame_us = 0u;
            maximum_frame_us = 0u;
            profiled_frames = 0u;
            frames_over_20ms = 0u;
            frames_over_budget = 0u;
        }
    }
}
