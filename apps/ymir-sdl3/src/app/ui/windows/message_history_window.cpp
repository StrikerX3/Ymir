#include "message_history_window.hpp"

#include <util/std_lib.hpp>

namespace app::ui {

MessageHistoryWindow::MessageHistoryWindow(SharedContext &context)
    : WindowBase(context) {

    m_windowConfig.name = "Message history";
}

void MessageHistoryWindow::PrepareWindow() {
    auto *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(400 * m_context.displayScale, 300 * m_context.displayScale),
                                        ImVec2(vp->Size.x, vp->Size.y));
}

void MessageHistoryWindow::DrawContents() {
    if (ImGui::BeginTable("msg_history", 2, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Timestamp");
        ImGui::TableSetupColumn("Message");
        ImGui::TableHeadersRow();

        std::unique_lock lock{m_context.locks.messages};
        const size_t count = m_context.messages.Count();
        for (size_t i = 0; i < count; i++) {
            const auto *msg = m_context.messages.Get(i);
            assert(msg != nullptr);

            auto localTime = util::to_local_time(msg->sysTime);
            auto fracTime =
                std::chrono::duration_cast<std::chrono::milliseconds>(msg->sysTime.time_since_epoch()).count() % 1000;
            // ISO 8601 + milliseconds
            auto timeStr = fmt::format("{0:%Y}/{0:%m}/{0:%d} {0:%H}:{0:%M}:{0:%S}.{1}", localTime, fracTime);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(timeStr.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(msg->message.c_str());
        }

        ImGui::EndTable();
    }
}

} // namespace app::ui
