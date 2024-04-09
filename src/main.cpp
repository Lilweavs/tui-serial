#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cassert>
#include <ftxui/component/component.hpp> // for Input, Renderer, ResizableSplitLeft
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/loop.hpp>
#include <ftxui/component/screen_interactive.hpp> // for ScreenInteractive
#include <ftxui/dom/elements.hpp> // for operator|, separator, text, Element, flex, vbox, border
#include <ftxui/component/event.hpp>
#include <ftxui/dom/linear_gradient.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/screen/color.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/screen/terminal.hpp>

#include <algorithm>
#include <ftxui/util/ref.hpp>
#include <memory> // for allocator, __shared_ptr_access, shared_ptr
#include <string> // for string
#include <thread>
#include <format>
#include <deque>
#include <optional>

#include "serial_windows.hpp"
#include <chrono>

constexpr size_t fps = 1000 / 60;
size_t maxRows = 1024;

using namespace ftxui;

std::array<uint8_t, 8192> buf;

std::deque<std::string> textRows;
std::deque<std::chrono::time_point<std::chrono::system_clock>> timeStamps;

void parseBytesToRows(const uint8_t* buffer, const size_t size, const size_t width);

size_t viewableTextRows = 0;
size_t viewableCharsInRow = 0;

size_t bytesRead = 0;

Serial serial;
bool running = true;

std::string inputStr;

enum class TuiState {
    VIEW,
    SEND,
    CONFIG
};

template<size_t N>
class Circulator {
public:
    Circulator(size_t idx = 0) : idx(idx) { }

    // prefix increment
    Circulator& operator++() {
        idx = (idx + 1) % N;
        return *this;
    }
 
    // postfix increment
    Circulator operator++(int) {
        Circulator<N> old = *this;
        operator++();
        return old;
    }

    operator size_t() { return size_t(idx); }
        
private:
    std::size_t idx;  
};

// std::array<LineEnding,4> lineEndings = {LineEnding::CRLF, LineEnding::LF, LineEnding::CR, LineEnding::NONE};
std::array<std::string,4> lineEndings = {"\r\n", "\n", "\r", ""};
std::array<std::string,4> lineEndingsText = {"CRLF", "  LF", "  CR", "NONE"};
Circulator<lineEndings.size()> lineEndingState; 

TuiState tuiState = TuiState::VIEW;
// size_t lineEndingState = 0;
size_t viewIndex = 0;
bool scrollSynced   = true;
bool helpMenuActive = false;
bool configVisable  = false;
bool viewPaused     = false;
bool upperOnSend    = false;
bool enableTimeStamps = false;

std::optional<size_t> viewIndexOpt;

int main(int argc, char* argv[]) {

    auto screen = ScreenInteractive::Fullscreen();
    auto screen_dim = Terminal::Size();

    InputOption option;
    option.multiline = false;
    option.content = &inputStr;
    option.transform = [](InputState state) {

        if (state.focused) {
            state.element |= color(Color::White);
        } else {
            state.element |= color(Color::GrayLight);
        }
        
        return state.element;
    };
        
    auto serialSendComponent = Input(option);

    auto helpComponent = Renderer([] {

        Element view = window(text("Help Menu"), 
            vbox({
                text(" ?    toggle help menu"),
                text(" p    pause with flush"),
                text(" k    scroll up"),
                text(" j    scroll down"),
                text(" K    scroll up 5"),
                text(" J    scroll down 5"),
                text(" :    send mode"),
                text(" C-e  port configuration"),
                text(" C-t  toggle timeStamps"),
                text(" C-p  pause no flush"),
                text(" C-o  clear serial view"),
                text(" C-u  (send) toggle upper case"),
                text(" C-l  (send) cyle line ending"),
             }) 
        );
        
        return view | clear_under;

    });

    std::vector<std::string> availableComPorts;
    std::vector<std::string> availableBaudrates = {"115200", "921600", "9600", "19200", "38400", "57600", "230400", "460800"};
    std::vector<std::string> dataBits           = {"8 Data Bits", "7 Data Bits"};
    std::vector<std::string> stopBits           = {"1 Stop Bits", "1.5 Stop Bits", "2 Stop Bits"};
    std::vector<std::string> parityStrings      = {"None", "Odd", "Even", "Mark", "Space"};
    int portSelected = 0;
    int baudrateSelected = 0;
    int dataBitsSelected = 0;
    int stopBitsSelected = 0;
    int paritySelected = 0;


    auto buttonOption = ButtonOption::Ascii();
    buttonOption.label = "Apply";
    buttonOption.on_click = [&]() {
        serial.open(availableComPorts.at(portSelected), std::stoi(availableBaudrates.at(baudrateSelected)));
        tuiState = TuiState::VIEW;
    };

    auto buttonOption2 = ButtonOption::Ascii();
    buttonOption2.label = "Cancel";
    buttonOption2.on_click = [&]() {
        tuiState = TuiState::VIEW;
    };

    auto portConfigurationComponent = Container::Vertical({
        Dropdown(&availableComPorts, &portSelected),
        Dropdown(&availableBaudrates, &baudrateSelected),
        Collapsible("Advanced Config",
            Container::Vertical({
                Dropdown(&dataBits, &dataBitsSelected),
                Dropdown(&stopBits, &stopBitsSelected),
                Dropdown(&parityStrings, &paritySelected),
            }),
            false
        ),
        Container::Horizontal({
            Button(buttonOption),
            Button(buttonOption2)
        }) | hcenter,
    });

    auto configComponent = Renderer([&] {
        Element view = window(text("Port Configuration"),
            portConfigurationComponent->Render()
        );
        return view;
    });

    auto main_window_renderer = Renderer([&] {

        Elements rows;
        size_t sidx = 0;
        size_t eidx = 0;
        if (!viewIndexOpt.has_value()) {
            sidx = (textRows.size() > viewableTextRows) ? textRows.size() - viewableTextRows: 0;
            eidx = textRows.size();
        } else {
            sidx = viewIndexOpt.value();
            eidx = sidx + viewableTextRows;
        }

        for (size_t i = sidx; i < eidx; i++) {

            if (enableTimeStamps) {
                rows.push_back(
                    hbox({
                        text(std::format("{:%T} ",
                             std::chrono::zoned_time{std::chrono::current_zone(), floor<std::chrono::milliseconds>(timeStamps.at(i))}.get_local_time())) | color(Color::Green),
                        text(textRows.at(i))
                    })
                );
            } else {
                rows.push_back(text(textRows.at(i)));
            }

        }

        std::string statusString;
        if (serial.isConnected()) {
            statusString = std::format("TUI Serial: Connected to {} @ {} {}/{} Time: {:%T}", serial.getPortName(), serial.getBaudrate(), viewIndexOpt.value_or(textRows.size()), textRows.size(),
                                       std::chrono::zoned_time{std::chrono::current_zone(), floor<std::chrono::milliseconds>(std::chrono::system_clock::now())}.get_local_time());
        } else {
            statusString = std::format("TUI Serial: Not Connected");
        }

        Element view = 
            vbox({
                hbox({
                    text(statusString),
                    separatorEmpty(),
                    text((tuiState == TuiState::SEND) ? "SEND-MODE" : "") | inverted | color(Color::Green),
                    separatorEmpty(),
                    text((upperOnSend && tuiState == TuiState::SEND) ? "UPPER" : "") | inverted | color(Color::Green),
                    separatorEmpty(),
                    text((viewPaused) ? "PAUSED" : "") | color(Color::Red) | inverted
                }) | border,
                hbox({
                    text("send:"),
                    serialSendComponent->Render(),
                    separator(),
                    text(lineEndingsText.at(lineEndingState))
                }) | border,
                vflow(rows) | border
            }) | size(WIDTH, GREATER_THAN, 120);

        if (helpMenuActive) {
            view = dbox({view, helpComponent->Render() | bottom_right});
        }

        if (tuiState == TuiState::CONFIG) {
            view = dbox({ view, configComponent->Render() | center });
        }
        
        return view;

    });

    main_window_renderer |= CatchEvent([&](Event event) {

        if (event == Event::Escape) {
            tuiState = TuiState::VIEW;
            return true;
        }
        
        if (tuiState == TuiState::VIEW) {
            
            if (const char c = event.character().at(0); event.is_character()) {

                if (c == ':') {
                    tuiState = TuiState::SEND;
                    return true;
                }

                switch (c) {
                    case 'p': {
                        // TODO: currently implemented is pause without flush
                        viewPaused = !viewPaused;
                        break;
                    }
                    case 'k': {
                        if (viewPaused && !viewIndexOpt.has_value()) {
                            if (textRows.size() < viewableTextRows) return true;
                            viewIndexOpt = textRows.size() - viewableTextRows;
                        } else {
                            viewIndexOpt = (viewIndexOpt.value() == 0) ? 0 : viewIndexOpt.value() - 1;
                        }
                        break;
                    }
                    case 'j': {
                        if (viewPaused && viewIndexOpt.has_value()) {
                            viewIndexOpt.value() += 1;
                            if ((textRows.size() - viewableTextRows) < viewIndexOpt.value()) viewIndexOpt.reset();
                        }
                        break;
                    }
                    case 'K': {
                        if (viewPaused && !viewIndexOpt.has_value()) {
                            if (textRows.size() < viewableTextRows) return true;
                            viewIndexOpt = textRows.size() - viewableTextRows;
                        } else {
                            viewIndexOpt = (viewIndexOpt.value() < 5) ? 0 : viewIndexOpt.value() - 5;
                        }
                        break;       
                    }
                    case 'J': {
                        if (viewPaused && viewIndexOpt.has_value()) {
                            viewIndexOpt.value() += 5;
                            if ((textRows.size() - viewableTextRows) < viewIndexOpt.value()) viewIndexOpt.reset();
                        }
                        break;
                    }
                    case '?': {
                        helpMenuActive = !helpMenuActive;       
                        break;
                    }
                    default: {
                        // not a command
                        break;
                    }                                   
                }

            }

            if (event == Event::Special({5})) {
                tuiState = TuiState::CONFIG;
                availableComPorts = Serial::enumerateComPorts();
                auto it = std::find(availableComPorts.begin(), availableComPorts.end(), serial.getPortName());
                if (it != availableComPorts.end()) { portSelected = std::distance(availableComPorts.begin(), it); }

                it = std::find(availableBaudrates.begin(), availableBaudrates.end(), std::to_string(serial.getBaudrate()));
                if (it != availableBaudrates.end()) { baudrateSelected = std::distance(availableBaudrates.begin(), it); }
            }

            if (event == Event::Special({15})) { // C-o
                // clear serial view
                textRows.clear();
                timeStamps.clear();
            }

            if (event == Event::Special({16})) { // C-p
                // pause serial view with flush
                // TODO: allows pausing the serial terminal, but still capture input in the background 
            }

            if (event == Event::Special({20})) { enableTimeStamps = !enableTimeStamps; }
            
        } else if (tuiState == TuiState::SEND) {

            if (event == Event::Escape) {
                tuiState = TuiState::VIEW;
            } else if (event == Event::Return) {
                viewIndexOpt.reset();
                std::string toSend = inputStr;
                if (upperOnSend) std::transform(toSend.begin(), toSend.end(), toSend.begin(), [](uint8_t c){ return std::toupper(c); });
                serial.send(toSend + lineEndings[lineEndingState]);
                inputStr.clear();
            } else if (event == Event::Special({21})) {
                upperOnSend = !upperOnSend;
            } else if (event == Event::Special({12})) {
                lineEndingState++;
            } else {
                return serialSendComponent->OnEvent(event);
            }
        } else if (tuiState == TuiState::CONFIG) {
            return portConfigurationComponent->OnEvent(event);
        } else {
            assert("impossible");
        }
          
        return true;

    });

    Loop loop(&screen, main_window_renderer);
    auto pollSerial = [&]() {
        while (running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            if (viewPaused) continue;
            serial.read();
        }
    };


    if (argc > 1) {
        // TODO: Sanity check on input args
        std::string port(argv[1]);
        uint32_t baudrate = 115200;
        if (argc > 2) { baudrate = std::stoi(argv[2]); }
        
        serial.open(port, baudrate);

    }

    std::thread serialThread(pollSerial);

    size_t bytesInBuffer = 0;

    loop.RunOnce();
    while (!loop.HasQuitted()) {

        viewableCharsInRow = std::max(screen.dimx() - 2, 80);
        viewableTextRows   = std::max(screen.dimy() - 8, 10);

        const auto bytesRead = serial.copyBytes(buf.data());
        if (bytesRead) {
            parseBytesToRows(buf.data(), bytesRead, viewableCharsInRow);
            screen.PostEvent(Event::Custom);
        }

        loop.RunOnce();
        std::this_thread::sleep_for(std::chrono::milliseconds(fps));

    }
    running = false;
    serialThread.join();

    return 0;
}

void parseBytesToRows(const uint8_t* buffer, const size_t size, size_t width) {

    size_t offset = 0;
    if (textRows.size() > 0) {
        std::string& last = textRows.back();
        if (!(last.size() == width || last.back() == '\n')) {
            auto bytesToSearch = std::min(width - last.size(), size);
            offset = std::distance(buffer, std::find(buffer, &buffer[bytesToSearch], '\n'));
            last.append(std::string(buffer, &buffer[offset]));
        } 
        
    }

    for (size_t i = offset; i < size; i++) {
        const size_t eidx = ((i + width) > size) ? size : i + width;
        const size_t tokenIdx = std::distance(buffer, std::find(&buffer[i], &buffer[eidx], '\n'));

        textRows.push_back(std::string(&buffer[i], &buffer[tokenIdx+1]));
        timeStamps.push_back(std::chrono::system_clock::now());        
        i = tokenIdx;
    }

    while (textRows.size() > maxRows) { 
        if (viewIndexOpt.has_value()) break;
        textRows.pop_front();
        timeStamps.pop_back();
    }

}

