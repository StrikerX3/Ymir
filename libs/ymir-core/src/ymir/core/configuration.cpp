#include <ymir/core/configuration.hpp>

namespace ymir::core {

void Configuration::NotifyObservers() {
    system.preferredRegionOrder.Notify();
    system.videoStandard.Notify();
    system.emulateSH2Cache.Notify();
    system.sh2ClockFactor.Notify();

    rtc.mode.Notify();

    swRenderer.threadedVDP1.Notify();
    swRenderer.threadedVDP2.Notify();
    swRenderer.threadedDeinterlacer.Notify();

    audio.interpolation.Notify();
    audio.threadedSCSP.Notify();

    cdblock.readSpeedFactor.Notify();
    cdblock.useLLE.Notify();
}

} // namespace ymir::core
