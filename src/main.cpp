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
#include <vector>
#include <fstream>

#include "serial_windows.hpp"
#include "SerialConfigView.hpp"
#include "PreviousCommandsView.hpp"
#include "AsciiView.hpp"
#include "SendView.hpp"
#include "Utils.hpp"

constexpr size_t fps = 1000 / 60;

using namespace ftxui;

std::array<uint8_t, 8192> buf;

size_t viewableTextRows = 0;
size_t viewableCharsInRow = 0;

std::atomic<bool> running = true;

enum class TuiState {
    VIEW,
    SEND,
    CONFIG,
    HISTORY
};

TuiState tuiState = TuiState::VIEW;
bool helpMenuActive  = false;
bool viewPaused      = false;
bool transmitEnabled = true;

Serial serial;
SendView sendView;
AsciiView asciiView;
SerialConfigView serialConfigView(serial);
PreviousCommandsView previousCommandsView;

constexpr uint8_t MAJOR_VERSION = 0;
constexpr uint8_t MINOR_VERSION = 1;
constexpr uint8_t DEV_VERSION   = 0;

int main(int argc, char* argv[]) {

    const std::vector<std::string> argList(argv + 1, argv + argc);
    if (std::ranges::find_if(argList, [](const auto& str) { return str == "-v" || str == "--version";}) != argList.end()) {
        std::printf("tui-serial v%d.%d.%d\n", MAJOR_VERSION, MINOR_VERSION, DEV_VERSION);
        return 0;
    }

        
    auto screen = ScreenInteractive::Fullscreen();
    auto screen_dim = Terminal::Size();

    auto helpView = Renderer([] {

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
                text(" ^    (send) view send history"),
                text(" d    (history) remove from history"),
                text(" e    (history) edit from history"),
                text(" C-b  (send) send break state"),
                text(" C-k  (send) toggle touch type"),
                text(" C-u  (send) toggle upper case"),
                text(" C-l  (send) cyle line ending"),
             }) 
        );
        
        return view | clear_under;

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
                    text((tuiState == TuiState::SEND)? "SEND-MODE": "") | inverted | color(Color::Green),
                    separatorEmpty(),
                    text((sendView.upperOnSend() && tuiState == TuiState::SEND) ? "UPPER" : "") | inverted | color(Color::Green),
                    separatorEmpty(),
                    text((sendView.sendOnType() && tuiState == TuiState::SEND) ? "TOUCH TYPE" : "") | inverted | color(Color::Green),
                    separatorEmpty(),
                    text((viewPaused) ? "PAUSED" : "") | color(Color::Red) | inverted,
                    filler(),
                    text(serial.getLastError()) | color(Color::Red)
                }) | border,
                sendView.getView(),
                asciiView.getView(),
            }) | size(WIDTH, GREATER_THAN, 120);

        if (helpMenuActive) {
            view = dbox({view, vbox({filler(), hbox({filler(), helpView->Render()}) })});
        }

        if (tuiState == TuiState::CONFIG) {
            view = dbox({ view, serialConfigView.getView() | center });
        }

        if (tuiState == TuiState::HISTORY) {
            view = dbox({ view, previousCommandsView.getView() | center });
        }
        
        return view;

    });

    main_window_renderer |= CatchEvent([&](Event event) {

        if (event == Event::Escape) {
            tuiState = TuiState::VIEW;
            return true;
        }

        switch (tuiState) {
            case TuiState::VIEW:
             
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
                    serialConfigView.listAvailableComPorts(serial);
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
                break;
                
            case TuiState::SEND:

                if (event == Event::Return) {

                    std::string toSend = sendView.getUserInput();

                    if (sendView.sendOnType()) {
                        toSend = sendView.lineEnding();
                    } else {
                        if (toSend.empty()) return true;
                    }

                    if (serial.send(toSend) && transmitEnabled) {
                        asciiView.addTransmitMessage(toSend);
                        if ((toSend.front() != '\r') && (toSend.front() != '\n')) {
                            std::erase_if(toSend, [](char c) { return std::iscntrl(c); } );
                            previousCommandsView.addToHistory(toSend);
                        }
                    }
                    
                } else if (event == Event::ArrowUp || event == Event::Special({8})) {
                    tuiState = TuiState::HISTORY;
                } else if (event == Event::Special({21})) {
                    sendView.toggleUpperOnSend();
                } else if (event == Event::Special({12})) {
                    sendView.cycleLineEnding();
                } else if (event == Event::Special({2})) {
                    serial.sendBreakState();
                } else if (event == Event::Special({11})) {
                    sendView.toggleSendOnType();
                } else {
                    if (sendView.OnEvent(event) && sendView.sendOnType()) {
                        const std::string toSend = sendView.getUserInput();
                        serial.send(toSend);
                        if (transmitEnabled) { asciiView.addTransmitMessage(toSend); }
                    }
                }
                break;
                
            case TuiState::CONFIG:
                return serialConfigView.OnEvent(event);
            case TuiState::HISTORY:
                if (const char c = event.character().at(0); event.is_character()) {
                    switch (c) {
                        case 'd':
                            previousCommandsView.removeFromHistory();
                            break;
                        case 'e':
                            sendView.setUserInput(previousCommandsView.getSendFromHistory());
                            tuiState = TuiState::SEND;
                            break;
                        default:
                            return previousCommandsView.OnEvent(event);
                    }
                } else if (event == Event::Escape) {
                    tuiState = TuiState::VIEW;
                } else if (event == Event::Return) {
                    sendView.setUserInput(previousCommandsView.getSendFromHistory());
                    const std::string toSend = sendView.getUserInput();
                    serial.send(toSend);
                    if (transmitEnabled) { asciiView.addTransmitMessage(toSend); }
                } else {
                    return previousCommandsView.OnEvent(event);
                }
                break;
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

    {
        std::filesystem::path appDataFile = getApplicationFolderDirectory();
        appDataFile = appDataFile / "tui-serial" / "history.txt";
        if (std::filesystem::exists(appDataFile)) {
            std::ifstream file(appDataFile.string());
            std::string line;
            while(std::getline(file, line)) {
                previousCommandsView.addToHistory(line);
            }
        }
    }

    std::thread serialThread(pollSerial);

    loop.RunOnce();
    while (!loop.HasQuitted()) {

        viewableCharsInRow = std::max(screen.dimx() - 2, 80);
        viewableTextRows   = std::max(screen.dimy() - 8, 10);

        if (const auto bytesRead = serial.copyBytes(buf.data()); bytesRead > 0) {
            asciiView.parseBytes(std::span(buf.begin(), bytesRead), viewableCharsInRow);
            asciiView.resetView(viewableTextRows);
            screen.PostEvent(Event::Custom);
        }

        loop.RunOnce();
        std::this_thread::sleep_for(std::chrono::milliseconds(fps));

    }

    const auto previousCommands = previousCommandsView.getCommandHistory();    

    std::filesystem::path appDataFolder = getApplicationFolderDirectory();
    
    if (!appDataFolder.empty()) {
        
        appDataFolder = appDataFolder / "tui-serial";

        if (!std::filesystem::exists(appDataFolder)) {
            std::filesystem::create_directory(appDataFolder);
        }

        auto appDataFile = appDataFolder / "history.txt";

        std::ofstream file(appDataFile.string(), std::ios::trunc);

        for (const auto cmds : previousCommands) {
            file << cmds << '\n';
        }

        
    }

    
    running = false;
    serialThread.join();

    return 0;
}


