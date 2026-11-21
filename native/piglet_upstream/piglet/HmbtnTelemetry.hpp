#ifndef HMBTN_TELEMETRY_HPP
#define HMBTN_TELEMETRY_HPP 1

#include <array>
#include <cstdint>
#include <cstdio>

struct HmbtnTelemetryState {
    FILE *log;
    uint64_t previous_frame_start;
    uint64_t total_frame_us;
    uint64_t maximum_frame_us;
    uint32_t sample_count;
    uint32_t frames_over_budget;
    uint32_t frames_over_20ms;
    uint32_t windows_since_flush;
    std::array<uint64_t, 300u> samples;
};

HmbtnTelemetryState hmbtnTelemetryInitialize();
HmbtnTelemetryState hmbtnTelemetryRecordFrame(
    const HmbtnTelemetryState& state);
void hmbtnTelemetryCheckpoint(const char *checkpoint);
void hmbtnTelemetryClose(const HmbtnTelemetryState& state);

#endif
