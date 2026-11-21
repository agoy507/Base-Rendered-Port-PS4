/*
 * Harvest Moon BTN Native V2 telemetry.
 * This project component is distributed under GPL-3.0 with the upstream
 * OpenOrbis Piglet baseline; see ../LICENSE.OpenOrbis-GPL-3.0.txt.
 */

#include "HmbtnTelemetry.hpp"

#include <algorithm>
#include <sys/stat.h>

#include <orbis/libkernel.h>

#define HMBTN_V2_PROFILE_WINDOW 300u
#define HMBTN_V2_FLUSH_WINDOWS 10u

static constexpr uint64_t HMBTN_FRAME_BUDGET_US = 16667u;
static constexpr uint64_t HMBTN_MAX_FRAME_TIME_US = 20000u;
static FILE *g_checkpoint_log = nullptr;

HmbtnTelemetryState hmbtnTelemetryInitialize() {
    HmbtnTelemetryState state{};
    mkdir("/data/HarvestMoonBTNNativeV2", 0777);
    state.log = fopen("/data/HarvestMoonBTNNativeV2/perf.log", "w");
    g_checkpoint_log = state.log;
    if (state.log != nullptr) {
        fprintf(
            state.log,
            "[NATIVE V2 BUILD] pad-module-fix 1.06 playable-farm checkpoints\n");
        fflush(state.log);
    }
    return state;
}

void hmbtnTelemetryCheckpoint(const char *checkpoint) {
    if (g_checkpoint_log == nullptr || checkpoint == nullptr) {
        return;
    }
    fprintf(g_checkpoint_log, "[NATIVE V2 CHECKPOINT] %s\n", checkpoint);
    fflush(g_checkpoint_log);
}

HmbtnTelemetryState hmbtnTelemetryRecordFrame(
    const HmbtnTelemetryState& state) {
    const uint64_t frame_start = sceKernelGetProcessTime();
    HmbtnTelemetryState next = state;
    if (state.previous_frame_start == 0u) {
        next.previous_frame_start = frame_start;
        return next;
    }

    const uint64_t frame_interval_us =
        frame_start - state.previous_frame_start;
    next.previous_frame_start = frame_start;
    next.samples[state.sample_count] = frame_interval_us;
    next.total_frame_us += frame_interval_us;
    next.maximum_frame_us =
        std::max(state.maximum_frame_us, frame_interval_us);
    next.frames_over_budget +=
        frame_interval_us > HMBTN_FRAME_BUDGET_US ? 1u : 0u;
    next.frames_over_20ms +=
        frame_interval_us > HMBTN_MAX_FRAME_TIME_US ? 1u : 0u;
    next.sample_count += 1u;

    if (next.sample_count < HMBTN_V2_PROFILE_WINDOW) {
        return next;
    }

    std::array<uint64_t, HMBTN_V2_PROFILE_WINDOW> sorted_samples =
        next.samples;
    std::sort(sorted_samples.begin(), sorted_samples.end());
    const uint64_t average_us =
        next.total_frame_us / HMBTN_V2_PROFILE_WINDOW;
    const uint64_t p95_us = sorted_samples[284u];
    const uint64_t p99_us = sorted_samples[296u];
    const uint64_t slowest_one_percent_us =
        (sorted_samples[297u] +
         sorted_samples[298u] +
         sorted_samples[299u]) / 3u;
    const double fps =
        average_us > 0u ? 1000000.0 / static_cast<double>(average_us) : 0.0;
    const double one_percent_low =
        slowest_one_percent_us > 0u
            ? 1000000.0 / static_cast<double>(slowest_one_percent_us)
            : 0.0;

    if (next.log != nullptr) {
        fprintf(
            next.log,
            "[NATIVE V2 PERF] samples=%u avg=%lluus p95=%lluus p99=%lluus max=%lluus fps=%.2f 1%%low=%.2f missed=%u over20ms=%u\n",
            HMBTN_V2_PROFILE_WINDOW,
            static_cast<unsigned long long>(average_us),
            static_cast<unsigned long long>(p95_us),
            static_cast<unsigned long long>(p99_us),
            static_cast<unsigned long long>(next.maximum_frame_us),
            fps,
            one_percent_low,
            next.frames_over_budget,
            next.frames_over_20ms);
    }

    next.windows_since_flush += 1u;
    if (next.log != nullptr &&
        next.windows_since_flush >= HMBTN_V2_FLUSH_WINDOWS) {
        fflush(next.log);
        next.windows_since_flush = 0u;
    }
    next.total_frame_us = 0u;
    next.maximum_frame_us = 0u;
    next.sample_count = 0u;
    next.frames_over_budget = 0u;
    next.frames_over_20ms = 0u;
    return next;
}

void hmbtnTelemetryClose(const HmbtnTelemetryState& state) {
    if (state.log != nullptr) {
        fclose(state.log);
    }
    if (g_checkpoint_log == state.log) {
        g_checkpoint_log = nullptr;
    }
}
