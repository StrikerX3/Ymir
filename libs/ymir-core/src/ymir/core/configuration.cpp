#include <ymir/core/configuration.hpp>

namespace ymir::core {

void Configuration::NotifyObservers() {
    system.preferredRegionOrder.Notify();

    video.threadedVDP2.Notify();

    audio.interpolation.Notify();
    audio.threadedSCSP.Notify();
}

} // namespace ymir::core
