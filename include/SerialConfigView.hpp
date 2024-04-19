#ifndef SERIAL_CONFIG_VIEW_H
#define SERIAL_CONFIG_VIEW_H

#include "ftxui/dom/elements.hpp"
#include "ftxui/component/component.hpp"
#include "serial_windows.hpp"
#include <ftxui/component/component_options.hpp>
#include <ftxui/dom/node.hpp>

#include <iterator>

using namespace ftxui;
    
static std::vector<std::string> availableComPorts;
static const std::vector<std::string> availableBaudrates  = {"115200", "921600", "9600", "19200", "38400", "57600", "230400", "460800"};
static const std::vector<std::string> dataBits            = {"8 Data Bits", "7 Data Bits"};
static const std::vector<std::string> stopBits            = {"1 Stop Bits", "1.5 Stop Bits", "2 Stop Bits"};
static const std::vector<std::string> parityStrings       = {"None", "Odd", "Even", "Mark", "Space"};

class SerialConfigView {
public:

    SerialConfigView(Serial& serial) : serial(serial) { }

    ~SerialConfigView() { }
    
    Element getView() {
        return window(text("Port Configuration"), portConfigurationComponent->Render());
    }

    void listAvailableComPorts(const Serial& serial) {
        
        availableComPorts = Serial::enumerateComPorts();
        auto it = std::find(availableComPorts.cbegin(), availableComPorts.cend(), serial.getPortName());
        if (it != availableComPorts.end()) { portSelected = std::distance(availableComPorts.cbegin(), it); }

        it = std::find(availableBaudrates.begin(), availableBaudrates.end(), std::to_string(serial.getBaudrate()));
        if (it != availableBaudrates.end()) { baudrateSelected = std::distance(availableBaudrates.begin(), it); }
        
    }

    bool OnEvent(Event event) { return portConfigurationComponent->OnEvent(event); }
    
private:
    
    int portSelected     = 0;
    int baudrateSelected = 0;
    int dataBitsSelected = 0;
    int stopBitsSelected = 0;
    int paritySelected   = 0;

    Serial& serial;
    
    ButtonOption buttonOption {
        .label = "Apply",
        .on_click = [&]() {
            serial.open(availableComPorts[portSelected],
                        std::stoi(availableBaudrates[baudrateSelected]));
        },
        .transform = [](const EntryState& state) -> Element {
            const std::string t = state.focused ? "[" + state.label + "]"  //
                                                : " " + state.label + " ";
            return text(t);
        }
    };
    
    Component portConfigurationComponent = Container::Vertical({
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
        Button(buttonOption) | center,
    });
    
};


#endif // SERIAL_CONFIG_VIEW_H

