#include "settings_view_base.hpp"

namespace app::ui {

SettingsViewBase::SettingsViewBase(SharedContext &context)
    : m_context(context) {}

void SettingsViewBase::MakeDirty() {
    GetSettings().MakeDirty();
}

bool SettingsViewBase::MakeDirty(bool value) {
    if (value) {
        MakeDirty();
    }
    return value;
}

Settings &SettingsViewBase::GetSettings() {
    return m_context.serviceLocator.GetRequired<Settings>();
}

const Settings &SettingsViewBase::GetSettings() const {
    return m_context.serviceLocator.GetRequired<Settings>();
}

} // namespace app::ui
