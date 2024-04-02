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

#include "serial_windows.hpp"

constexpr size_t fps = 1000 / 60;
constexpr size_t maxRows = 64;

using namespace ftxui;

std::array<uint8_t, 512> buf;

// std::vector<std::string> textRows;

std::deque<std::string> textRows;

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

enum class LineEnding {
    NONE,
    CR,
    LF,
    CRLF
};

std::array<LineEnding,4> lineEndings = {LineEnding::CRLF, LineEnding::LF, LineEnding::CR, LineEnding::NONE}; 

TuiState tuiState = TuiState::VIEW;
size_t lineEndingState = 0;
size_t viewIndex = 0;
bool scrollSynced = true;
bool helpMenuActive = false;
bool configVisable = false;

std::string getLineEndingText(const size_t idx) {
    switch (lineEndings[idx]) {
        case LineEnding::NONE:
            return "NONE";
        case LineEnding::CR:
            return "  CR";
        case LineEnding::LF:
            return "  LF";
        case LineEnding::CRLF:
            return "CRLF";
        default:
            assert("Impossible");
            return "";
    }
}

std::string addLineEnding(const std::string& toSend, const LineEnding ending) {
    switch (ending) {
        case LineEnding::NONE:
            return toSend;
        case LineEnding::CR:
            return toSend + '\r';
        case LineEnding::LF:
            return toSend + '\n';
        case LineEnding::CRLF:
            return toSend + "\r\n";
        default:
            assert("Impossible");
            return "";
    }
}

int main(int argc, char* argv[]) {

    auto screen = ScreenInteractive::Fullscreen();
    auto screen_dim = Terminal::Size();

    InputOption option;
    option.multiline = false;
        
    auto serialSendComponent = Input(&inputStr, "");

    auto helpComponent = Renderer([] {

        Element view = window(text("Help Menu"), 
            vbox({
                text(" ?    toggle help menu"),
                text(" k    scroll up"),           
                text(" j    scroll down"),
                text(" :    send mode"),
                text(" C-l  cyle line ending"),
                text(" C-e  port configuration"),
             }) 
        );
        
        return view | clear_under;
    });


    std::vector<std::string> availableComPorts = {"COM1", "COM10","COM1", "COM10","COM1", "COM10", "COM1", "COM10", "COM1", "COM10", };
    std::vector<std::string> availableBaudrates = {"115200", "921600"};
    std::vector<std::string> dataBits = {"8", "7"};
    std::vector<std::string> stopBits = {"1", "2"};
    int portSelected = 0;
    int baudrateSelected = 0;
    int dataBitsSelected = 0;
    int stopBitsSelected = 0;

    // WindowOptions windowOptions;
    // windowOptions.title = "Port Configuration";
    // windowOptions.inner
    auto portConfigurationComponent = Container::Vertical({
        Dropdown(&availableComPorts, &portSelected),
        Dropdown(&availableBaudrates, &baudrateSelected),
        Dropdown(&dataBits, &dataBitsSelected),
        Dropdown(&stopBits, &stopBitsSelected),
    });

    auto configComponent = Renderer([&] {
        Element view = window(text("Port Configuration"),
              portConfigurationComponent->Render()
        );
        return view;
    });


    // auto portConfigurationComponent = Window(windowOptions);

    auto main_window_renderer = Renderer([&] {
        // Element current_view = hex_viewer_renderer->Render();
        // Element view = text("hellow Mate");
        Elements rows;
        size_t sidx = 0;
        size_t eidx = 0;
        if (scrollSynced) {
            sidx = (textRows.size() > viewableTextRows) ? textRows.size() - viewableTextRows: 0;
            eidx = textRows.size();
        } else {
            sidx = viewIndex;
            eidx = std::min(textRows.size(), viewIndex + viewableTextRows);
        }

        // const size_t sidx = (textRows.size() > viewableTextRows) ? textRows.size() - viewableTextRows: 0;
        for (size_t i = sidx; i < eidx; i++) {
            rows.push_back(text(textRows.at(i)));
        }
        
        Element view = 
            vbox({
                text(std::format("TUI Serial: {}/{} -> {}", std::min(viewableTextRows, textRows.size()), textRows.size(), inputStr)) | border,
                hbox({
                    text("send:"),
                    serialSendComponent->Render(),
                    separator(),
                    text(getLineEndingText(lineEndingState))
                }) | border,
                vflow(rows) | border
            }) | size(WIDTH, GREATER_THAN, 120);

        if (helpMenuActive) {
            view = dbox({view, helpComponent->Render() | bottom_right});
        }

        if (tuiState == TuiState::CONFIG) {
            view = dbox({view, configComponent->Render() | center});
        }
        
        return view;
    });



    //     Element view = window(text("Port Configuration", 
    //         vbox({
    //             Dropdown(, )     
    //         })           
    //     ));
         
    // });

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

                if (c == 'k') {
                    if (scrollSynced == true) {
                        if (viewIndex == 0) return true;
                        scrollSynced = false;
                        viewIndex = (textRows.size() < viewableTextRows) ? 0 : (textRows.size() - viewableTextRows);
                    } else {
                        viewIndex = (viewIndex == 0) ? 0 : viewIndex - 1;
                    }
                }

                if (c == 'j') {
                        
                    if (textRows.size() < viewableTextRows) return true;

                    if (viewIndex + viewableTextRows > textRows.size()) {
                        scrollSynced = true;
                    } else {
                        viewIndex++;
                    }
                                        
                }

                if (c == '?') {
                    helpMenuActive = !helpMenuActive;
                }

            }

            if (event == Event::Special({12})) {
                lineEndingState = (lineEndingState + 1) % lineEndings.size();
            }

            if (event == Event::Special({5})) {
                tuiState = TuiState::CONFIG;
            }

            // if (event.is_mouse()) {

            //     if (event.mouse().WheelUp) {

            //         if (textRows.size() < viewableTextRows) return true;
        
            //         if (scrollSynced == true) {
            //             scrollSynced = false;
            //             viewIndex = (textRows.size() < viewableTextRows) ? 0 : textRows.size() - (viewableTextRows - 1);
            //         } else {
            //             viewIndex = (viewIndex == 0) ? 0 : viewIndex - 1;
            //         }
                    
            //     }

            //     if (event.mouse().WheelDown) {

            //         if (textRows.size() < viewableTextRows) { return true; }

            //         viewIndex = (viewIndex > (textRows.size() - viewableTextRows)) ? viewIndex : viewIndex + 1;
                    
            //     }
                
            // }
            
        } else if (tuiState == TuiState::SEND) {

            if (event == Event::Escape) {
                tuiState = TuiState::VIEW;
                return true;
            } else if (event == Event::Return) {
                serial.send(addLineEnding(inputStr, lineEndings[lineEndingState]));
                inputStr.clear();
                return true;
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
    // auto pollSerial = [&]() {
    //     while (!loop.HasQuitted()) {
    //         std::this_thread::sleep_for(std::chrono::milliseconds(fps));
    //         serial.read();
    //     }
    // };

    // std::thread serialThread(pollSerial);

    // serial.open("\\\\.\\COM1");
    serial.open(argv[1]);

    size_t bytesInBuffer = 0;

    loop.RunOnce();
    while (!loop.HasQuitted()) {

        viewableCharsInRow = std::max(screen.dimx() - 2, 120);
        viewableTextRows   = std::max(screen.dimy() - 8, 10);

        bytesRead = serial.read();
        if (bytesRead > 0) {
            bytesInBuffer += bytesRead;
            serial.copyBytes(buf.data());
            parseBytesToRows(buf.data(), bytesRead, viewableCharsInRow);
        }

        loop.RunOnce();
        screen.PostEvent(Event::Custom);
        std::this_thread::sleep_for(std::chrono::milliseconds(fps));

    }

    // serialThread.join();

    return 0;
}

void parseBytesToRows(const uint8_t* buffer, const size_t size, const size_t width) {

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
        i = tokenIdx;
    }

    while (textRows.size() > maxRows) { textRows.pop_front(); }

}

