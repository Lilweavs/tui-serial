#ifndef SEND_VIEW_H
#define SEND_VIEW_H

#include <ftxui/component/component_base.hpp>
#include <string>
#include <ftxui/dom/elements.hpp>
#include <ftxui/component/component.hpp>

using namespace ftxui;

class SendView {
public:

    SendView() {}

    ~SendView() {}

    std::string getUserInput() {
        std::string toSend = mUserInput;
        if (mUpperOnSend) std::transform(toSend.begin(), toSend.end(), toSend.begin(), [](uint8_t c){ return std::toupper(c); });

        if (!mSendOnType) { toSend += sLineEndings[mLineEndingState]; }
        
        mUserInput.clear();
        return toSend;
    }
    
    Element getView() { 
        return hbox({
                text("send:"),
                mInput->Render(),
                separator(),
                text(getLineEnding())
            }
        ) | border;  
    }

    bool OnEvent(Event event) {
        return mInput->OnEvent(event);
    }

    void toggleUpperOnSend() { mUpperOnSend = !mUpperOnSend; }

    void toggleSendOnType() {
        mSendOnType = !mSendOnType;
        mUserInput.clear();
    }

    const bool sendOnType() const { return mSendOnType; }
        
    const bool upperOnSend() const { return mUpperOnSend; }
        
    std::string getLineEnding() {
        switch(mLineEndingState) {
            case 0: return "CRLF";
            case 1: return "  LF";
            case 2: return "  CR";
            case 3: return "NONE";
            default: std::abort();
        };
    }
    
    void cycleLineEnding() { mLineEndingState = (mLineEndingState + 1) % sLineEndings.size(); }

private:

    bool mUpperOnSend = false;
    bool mSendOnType = false;
    int mLineEndingState = 0; 
    
    static constexpr std::array<const char*,4> sLineEndings = {"\r\n", "\n", "\r", ""};

    std::string mUserInput;
    
    InputOption option{
        .content = &mUserInput,
        .transform = [](InputState state) {

            if (state.focused) {
                state.element |= color(Color::White);
            } else {
                state.element |= color(Color::GrayLight);
            }
        
            return state.element;
        },
        .multiline = false,
    };

    Component mInput = Input(option);

};


#endif // SEND_VIEW_H

