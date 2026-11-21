/* OpenOrbis Piglet app example.
 */

#include "PigletApplication.hpp"
#include "PortBaseTelemetry.hpp"

int main() {
    static nik::PigletApplication& AppRef{ nik::GetPigletApplication() };
    PortBaseTelemetryState telemetry = port_baseTelemetryInitialize();

    // the shutdown of the app is handled in a destructor so no Quit() methods etc

    port_baseTelemetryCheckpoint("main-before-app-init");
	if (!AppRef.Init()) {
        port_baseTelemetryCheckpoint("app-init-failed");
        // the class will print the error message to std::cerr.
        port_baseTelemetryClose(telemetry);
        return EXIT_FAILURE;
    }
    port_baseTelemetryCheckpoint("app-init-ok");

    bool first_frame = true;
    while (true) {
        if (first_frame) {
            port_baseTelemetryCheckpoint("main-before-first-logic");
        }
        if (!AppRef.Logic()) {
            break;
        }
        if (first_frame) {
            port_baseTelemetryCheckpoint("main-after-first-logic");
        }
        telemetry = port_baseTelemetryRecordFrame(telemetry);
        if (first_frame) {
            port_baseTelemetryCheckpoint("main-before-first-render");
        }
        AppRef.Render();
        if (first_frame) {
            port_baseTelemetryCheckpoint("main-after-first-render");
            first_frame = false;
        }
    }

    port_baseTelemetryClose(telemetry);
    return EXIT_SUCCESS;
}
