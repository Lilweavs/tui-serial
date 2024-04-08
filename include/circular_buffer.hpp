#include <cstddef>
#include <string>

#ifndef CIRCULAR_BUFFER_H
#define CIRCULAR_BUFFER_H

template<typename T, size_t N>
class CircularBuffer {
public:

    CircularBuffer() { };

    void push_back(const T& item) {
        mBuffer[mTail] = item;
        mTail = (mTail + 1) % N;
    }
    
    

private:

    std::array<T, N> mBuffer;
    size_t mHead = 0;
    size_t mTail = 0;
    size_t mSize = 0;
};

#endif // CIRCULAR_BUFFER_H
