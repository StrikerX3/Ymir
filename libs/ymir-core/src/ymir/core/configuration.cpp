#include <ymir/core/configuration.hpp>

namespace ymir::core {

void Configuration::NotifyObservers() {
    system.preferredRegionOrder.Notify();
    system.videoStandard.Notify();
    system.emulateSH2Cache.Notify();

    rtc.mode.Notify();

    video.threadedVDP1.Notify();
    video.threadedVDP2.Notify();
    video.threadedDeinterlacer.Notify();

    audio.interpolation.Notify();
    audio.threadedSCSP.Notify();

    cdblock.readSpeedFactor.Notify();
    cdblock.useLLE.Notify();
}

} // namespace ymir::core
