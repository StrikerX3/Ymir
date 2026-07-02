#include "sandboxes.hpp"

#include <algorithm>

int main(int argc, char **argv) {
    // runVDP1PolygonSandbox();
    // runBUPSandbox();
    // runInputSandbox();
    // runVDP1AccuracySandbox(argc, argv);
    // runBinCueLoaderSandbox(argc, argv);
    // runCurlSandbox();
    // runSH2PerfSandbox();
    // runDiscInfoExtractor(argc, argv);
    // runDeadlockTest(argc, argv);
    // runCDDeviceSandbox(argc, argv);
    runCDDeviceEventsSandbox();

    return EXIT_SUCCESS;
}
