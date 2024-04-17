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
#include <ftxui/util/ref.hpp>

#include <cstddef>
#include <cstdint>
#include <cassert>
#include <algorithm>
#include <memory> // for allocator, __shared_ptr_access, shared_ptr
#include <string> // for string
#include <thread>
#include <format>
#include <chrono>

#include "serial_windows.hpp"

#include "AsciiView.hpp"

constexpr size_t fps = 1000 / 60;

using namespace ftxui;

std::array<uint8_t, 8192> buf;

size_t viewableTextRows = 0;
size_t viewableCharsInRow = 0;

std::atomic<bool> running = true;

std::string inputStr;

enum class TuiState {
    VIEW,
    SEND,
    CONFIG
};


TuiState tuiState = TuiState::VIEW;
bool helpMenuActive  = false;
bool configVisable   = false;
bool viewPaused      = false;
bool sendOnType      = false;
bool transmitEnabled = true;

Serial serial;
AsciiView asciiView;

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
                text(" C-k  (send) toggle touch type"),
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

        std::string statusString;
        if (serial.isConnected()) {
            statusString = std::format("TUI Serial: Connected to {} @ {} {}/{}", serial.getPortName(), serial.getBaudrate(), asciiView.getIndex(), asciiView.getNumRows());
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
                    text((serial.mUpperOnSend && tuiState == TuiState::SEND) ? "UPPER" : "") | inverted | color(Color::Green),
                    separatorEmpty(),
                    text((sendOnType && tuiState == TuiState::SEND) ? "TOUCH TYPE" : "") | inverted | color(Color::Green),
                    separatorEmpty(),
                    text((viewPaused) ? "PAUSED" : "") | color(Color::Red) | inverted
                }) | border,
                hbox({
                    text("send:"),
                    serialSendComponent->Render(),
                    separator(),
                    text(serial.getLineEnding())
                }) | border,
                vflow(asciiView.getView()) | border
            }) | size(WIDTH, GREATER_THAN, 120);

        if (helpMenuActive) {
            view = dbox({view, vbox({filler(), hbox({filler(), helpComponent->Render()}) })});
        }

        if (tuiState == TuiState::CONFIG) {
            view = dbox({ view, configComponent->Render() | center });
        }
        
        return view;

    });

    main_window_renderer |= CatchEvent([&](Event event) {

        if (event == Event::Escape) {
            tuiState = TuiState::VIEW;
        } else if (tuiState == TuiState::VIEW) {
            
            if (const char c = event.character().at(0); event.is_character()) {

                switch (c) {
                    case 'p':
                        // TODO: currently implemented is pause without flush
                        viewPaused = !viewPaused;
                        break;
                    case 'k':
                        asciiView.scrollViewUp(1);
                        break;
                    case 'j':
                        asciiView.scrollViewDown(1);
                        break;
                    case 'K':
                        asciiView.scrollViewUp(5);
                        break;       
                    case 'J':
                        asciiView.scrollViewDown(5);
                        break;
                    case '?':
                        helpMenuActive = !helpMenuActive;       
                        break;
                    case ':':
                        tuiState = TuiState::SEND;
                        break;
                    default:
                        // not a command
                        break;
                }
            
            } else if (event == Event::Special({5})) {
                tuiState = TuiState::CONFIG;
                availableComPorts = Serial::enumerateComPorts();
                auto it = std::find(availableComPorts.begin(), availableComPorts.end(), serial.getPortName());
                if (it != availableComPorts.end()) { portSelected = std::distance(availableComPorts.begin(), it); }

                it = std::find(availableBaudrates.begin(), availableBaudrates.end(), std::to_string(serial.getBaudrate()));
                if (it != availableBaudrates.end()) { baudrateSelected = std::distance(availableBaudrates.begin(), it); }
            } else if (event == Event::Special({15})) { // C-o
                asciiView.clearView();
            } else if (event == Event::Special({16})) { // C-p
                // pause serial view with flush
                // TODO: allows pausing the serial terminal, but still capture input in the background 
            } else if (event == Event::Special({20})) {
                asciiView.toggleTimeStamps();
            } else {
                // not a command
            }
            
        } else if (tuiState == TuiState::SEND) {

            if (event == Event::Escape) {
                tuiState = TuiState::VIEW;
            } else if (event == Event::Return) {

                std::string sent = serial.send(inputStr);

                if (transmitEnabled) { asciiView.addTransmitMessage(sent); }
                
                inputStr.clear();
                
            } else if (event == Event::Special({21})) {
                serial.mUpperOnSend = !serial.mUpperOnSend;
            } else if (event == Event::Special({12})) {
                serial.cycleLineEnding();
            } else if (event == Event::Special({11})) {
                sendOnType = !sendOnType;
                inputStr.clear();
            } else {

                if (serialSendComponent->OnEvent(event) && sendOnType) {
                   
                    std::string sent = serial.sendChar(inputStr);
                    
                    if (transmitEnabled) { asciiView.addTransmitMessage(sent); }
                    
                    inputStr.clear();
                    
                }
                
                return true;
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

    loop.RunOnce();
    while (!loop.HasQuitted()) {

        viewableCharsInRow = std::max(screen.dimx() - 2, 80);
        viewableTextRows   = std::max(screen.dimy() - 8, 10);

        const auto bytesRead = serial.copyBytes(buf.data());
        if (bytesRead) {
            asciiView.parseBytes(std::span(buf.begin(), bytesRead), viewableCharsInRow);
            asciiView.resetView(viewableTextRows);
            screen.PostEvent(Event::Custom);
        }

        loop.RunOnce();
        std::this_thread::sleep_for(std::chrono::milliseconds(fps));

    }
    running = false;
    serialThread.join();

    return 0;
}

