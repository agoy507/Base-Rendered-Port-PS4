/* OpenOrbis Piglet app example.
 */

#include "PigletApplication.hpp"
#include "HmbtnTelemetry.hpp"

int main() {
    static nik::PigletApplication& AppRef{ nik::GetPigletApplication() };
    HmbtnTelemetryState telemetry = hmbtnTelemetryInitialize();

    // the shutdown of the app is handled in a destructor so no Quit() methods etc

    hmbtnTelemetryCheckpoint("main-before-app-init");
	if (!AppRef.Init()) {
        hmbtnTelemetryCheckpoint("app-init-failed");
        // the class will print the error message to std::cerr.
        hmbtnTelemetryClose(telemetry);
        return EXIT_FAILURE;
    }
    hmbtnTelemetryCheckpoint("app-init-ok");

    bool first_frame = true;
    while (true) {
        if (first_frame) {
            hmbtnTelemetryCheckpoint("main-before-first-logic");
        }
        if (!AppRef.Logic()) {
            break;
        }
        if (first_frame) {
            hmbtnTelemetryCheckpoint("main-after-first-logic");
        }
        telemetry = hmbtnTelemetryRecordFrame(telemetry);
        if (first_frame) {
            hmbtnTelemetryCheckpoint("main-before-first-render");
        }
        AppRef.Render();
        if (first_frame) {
            hmbtnTelemetryCheckpoint("main-after-first-render");
            first_frame = false;
        }
    }

    hmbtnTelemetryClose(telemetry);
    return EXIT_SUCCESS;
}
