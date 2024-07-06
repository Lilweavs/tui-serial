#ifndef PREVIOUS_COMMANDS_VIEW
#define PREVIOUS_COMMANDS_VIEW

#include <ftxui/component/component_base.hpp>
#include <ftxui/dom/elements.hpp>
#include <vector>
#include <string>
#include "ftxui/component/component.hpp"

using namespace ftxui;

class PreviousCommandsView {
public:

    void addToHistory(const std::string& str) {
        mPreviousCommands.push_back(str);
    }

    void toggleView() {
        mEnableView = !mEnableView;
    }

    bool enabled() const { return mEnableView; }

    Element getView() {
        return window(text("Send History"), mHistoryComponent->Render()) | clear_under;
    }

    void removeFromHistory() {
        if (mPreviousCommands.empty()) return;
        mPreviousCommands.erase(mPreviousCommands.begin() + mSelectionIdx);
        if (mSelectionIdx > mPreviousCommands.size()) {
            mSelectionIdx--;
        }
    }

    std::string getSendFromHistory() { 
        if (mPreviousCommands.empty()) return "";
        return mPreviousCommands.at(mSelectionIdx); }

    bool OnEvent(Event event) {
        return mHistoryComponent->OnEvent(event);
    }

private:

    bool mEnableView = false;
    std::vector<std::string> mPreviousCommands;
    int mSelectionIdx = 0;

    Component mHistoryComponent = Menu(&mPreviousCommands, &mSelectionIdx);
    
};

#endif // PREVIOUS_COMMANDS_VIEW
