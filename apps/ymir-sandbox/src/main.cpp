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
    // runHostCDSandbox();
    // runD3D12Sandbox();
    // return runVulkanSandbox();
    return runYmirGPUSandbox();

    return EXIT_SUCCESS;
}
