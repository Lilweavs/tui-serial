#include <algorithm>
#include <cstddef>
#include <deque>
#include <ftxui/screen/color.hpp>
#include <string>
#include <chrono>

#include "ftxui/dom/elements.hpp"

#ifndef ASCII_VIEW_H
#define ASCII_VIEW_H

using namespace ftxui;

struct SerialData {
    bool rxtx = false;
    std::string text;
    std::chrono::time_point<std::chrono::utc_clock> time;
};

class AsciiView {
public:

    AsciiView() { }

    ~AsciiView() { }

    Elements getView() {
        
        using namespace std::chrono;
        
        Elements rows;
        for (size_t i = mViewIndex; i < std::min(mViewIndex+mRowsOfTextAllowed,mData.size()); i++) {

            if (mViewTimeStamps) {
                rows.push_back(
                    hbox({
                        text(std::format("{:%T} ", floor<milliseconds>(mData.at(i).time))) | color(Color::Green),
                        text(std::format("[{}] {}", rxOrTxStr[mData.at(i).rxtx], mData.at(i).text)) | color((mData.at(i).rxtx) ? Color::White : Color::Cyan)
                    })
                );
            } else {
                rows.push_back(text(std::format("[TX] {}", mData.at(i).text)) | color((mData.at(i).rxtx) ? Color::White : Color::Blue));
            }
        }
        
        return rows;
        
    }

    void parseBytes(std::span<const uint8_t> slice, const size_t width) {
        
        if (mData.size() > 0) {
            auto& last = mData.back();
            if (last.text.size() == width || last.text.back() == '\n' || last.rxtx == false) {
                parseRow(slice, width);
            } else {
                const size_t bytesToSearch = std::min(width - last.text.size(), slice.size());
                const auto it = std::ranges::find(slice.first(bytesToSearch), '\n');
                             
                last.text.append(std::string(slice.begin(), (it == slice.end()) ? it : it + 1));

                parseRow(std::span((it == slice.end()) ? it : it + 1, slice.end()), width);
                
            }
        } else {
            parseRow(slice, width);
        }
        
        while (mData.size() > mMaxTextRows) { mData.pop_front(); }
        
    }

    void parseRow(std::span<const uint8_t> slice, const size_t width) {

        if (slice.empty()) return;
        
        const auto it = std::ranges::find(slice.first(std::min(slice.size(), width)), '\n');
        mData.emplace_back(
            SerialData{
                .rxtx = true,
                .text = std::string(slice.begin(), (it == slice.end()) ? it : it + 1),
                .time = std::chrono::utc_clock::now()
            }
        );
        parseRow(std::span((it == slice.end()) ? it : it + 1, slice.end()), width);
    }

    size_t getNumRows() { return mData.size(); }
    
    size_t getIndex() { return mViewIndex; }

    void clearView() { mData.clear(); mViewIndex = 0; }

    void scrollViewUp(size_t count) { 
        for (size_t i = 0; i < count; i++) {
            mViewIndex -= (mViewIndex == 0) ? 0 : 1;
        }
    }        

    void scrollViewDown(size_t count) {
        
        if (mData.size() < mRowsOfTextAllowed) { return; };

        mViewIndex += count;

        mViewIndex = std::min(mViewIndex + count, mData.size() - mRowsOfTextAllowed);
                
    }

    void toggleTimeStamps() { mViewTimeStamps = !mViewTimeStamps; }

    void resetView(const size_t viewableTextRows) { 
        mRowsOfTextAllowed = viewableTextRows;
        mViewIndex = (mData.size() < mRowsOfTextAllowed) ? 0 : mData.size() - mRowsOfTextAllowed;
        
    }
    
    void addTransmitMessage(const std::string& txMsg) {

        if (mData.empty()) {
            mData.push_back(
                SerialData{
                    .rxtx = false,
                    .text = txMsg,
                    .time = std::chrono::utc_clock::now()
                }  
            );
            return;
        }

        auto& last = mData.back();

        if (last.rxtx == false && last.text.back() != '\n') {
            last.text.append(txMsg);
        } else {
            mData.push_back(
                SerialData{
                    .rxtx = false,
                    .text = txMsg,
                    .time = std::chrono::utc_clock::now()
                }  
            );
        }
        
    }
    
private:
    static constexpr std::array<std::string, 2> rxOrTxStr = { "TX", "RX"};
    size_t mViewIndex = 0;
    size_t mMaxTextRows = 1024;
    bool mViewTimeStamps = true;
    bool mViewTransmit = true;
    
    std::deque<SerialData> mData;
    size_t mRowsOfTextAllowed = 0;
    
};


#endif // ASCII_VIEW_H

