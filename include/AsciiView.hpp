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
                        text(mData.at(i).text)
                    })
                );
            } else {
                rows.push_back(text(mData.at(i).text));
            }
        }
        
        return rows;
        
    }

    void parseBytes(std::span<const uint8_t> slice, const size_t width) {
        
        if (mData.size() > 0) {
            std::string& last = mData.back().text;
            if (last.size() == width || last.back() == '\n') {
                parseRow(slice, width);
            } else {
                const size_t bytesToSearch = std::min(width - last.size(), slice.size());
                const auto it = std::ranges::find(slice.first(bytesToSearch), '\n');
                             
                last.append(std::string(slice.begin(), (it == slice.end()) ? it : it + 1));

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
    
    
private:

    size_t mViewIndex = 0;
    size_t mMaxTextRows = 1024;
    bool mViewTimeStamps = true;
    
    std::deque<SerialData> mData;
    size_t mRowsOfTextAllowed = 0;
    
};


#endif // ASCII_VIEW_H

