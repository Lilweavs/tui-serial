#include <cstddef>
#include <cstdint>
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include "Windows.h"
#include <string>
#include <array>
#include <mutex>
#include <stdint.h>
#include <algorithm>
#include <vector>

#ifndef SERIAL_WINDOWS_HPP
#define SERIAL_WINDOWS_HPP

class Serial {

public:

    Serial() { };

    ~Serial() { close(); };

    enum StopBits {
        ONE  = STOPBITS_10,
        HALF = STOPBITS_15,
        TWO  = STOPBITS_20,
    };

    enum DataBits {
        EIGHT = DATABITS_8,
        SEVEN = DATABITS_7,
        SIX   = DATABITS_6,
        FIVE  = DATABITS_5,
    };
    
    enum Parity {
        NONE  = PARITY_NONE,
        ODD   = PARITY_ODD,
        EVEN  = PARITY_EVEN,
        MARK  = PARITY_MARK,
        SPACE = PARITY_SPACE,
    };

    bool open(const std::string& port, const uint32_t baudrate = 115200) {

        std::scoped_lock<std::mutex> lock(mutex);

        if (mIsOpen) { CloseHandle(mSerialHandle); mIsOpen = false; }
                
        mPort = port;
        mBaudrate = baudrate;
        
        mSerialHandle = CreateFile(
            ("\\\\.\\" + mPort).c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );

        if (mSerialHandle == INVALID_HANDLE_VALUE) {
            return false;
        } else {
            mIsOpen = true;
        }

        return configurePort();

    }

    bool configurePort() {

        DCB serialConfig = {0};

        if (!GetCommState(mSerialHandle, &serialConfig)) return false;
        
        serialConfig.BaudRate    = mBaudrate;
        serialConfig.ByteSize    = mDataBits;
        serialConfig.StopBits    = mStopBits;
        serialConfig.Parity      = mParity;
        serialConfig.fDtrControl = DTR_CONTROL_DISABLE;
        
        if (!SetCommState(mSerialHandle, &serialConfig)) return false;

        COMMTIMEOUTS serialTimeouts = {0};
        if (!GetCommTimeouts(mSerialHandle, &serialTimeouts)) return false;

        serialTimeouts.WriteTotalTimeoutConstant   = 100; // prevent long blocks on WriteFile with write timeout
        serialTimeouts.WriteTotalTimeoutMultiplier = 0;

        if (!SetCommTimeouts(mSerialHandle, &serialTimeouts)) return false;
        
        PurgeComm(mSerialHandle, PURGE_RXCLEAR | PURGE_TXCLEAR);
        return true;
    }


    size_t copyBytes(uint8_t* dest) {

        std::scoped_lock<std::mutex> lock(mutex);

        if (!mIsOpen || mNumBytesInBuffer == 0) return 0;

        std::copy(mBuffer.begin(), mBuffer.begin()+mNumBytesInBuffer, dest);
        
        const auto bytesCopied = mNumBytesInBuffer;
        mNumBytesInBuffer = 0;
        return bytesCopied;

    }
            
    size_t read() {

        std::scoped_lock<std::mutex> lock(mutex);

        if (!mIsOpen) { return 0; }
        
        DWORD err;
        COMSTAT stat;
    
        ClearCommError(mSerialHandle, &err, &stat);
        DWORD bytesRead;

        if (stat.cbInQue == 0) { return 0; }

        ReadFile(mSerialHandle, &mBuffer[mNumBytesInBuffer], stat.cbInQue, &bytesRead, nullptr);
        mNumBytesInBuffer += bytesRead;

        return bytesRead;

    }

    bool send(const char* buffer, size_t length) {

        std::scoped_lock<std::mutex> lock(mutex);

        if (!mIsOpen) { return 0; }

        DWORD bytesWritten = 0;
        
        if (!WriteFile(mSerialHandle, buffer, length, &bytesWritten, nullptr)) { return false; }

        return true;
    }

    bool isConnected() { return mIsOpen; }

    void send(const std::string toSend) { send(toSend.c_str(), toSend.size()); }

    void close() { CloseHandle(mSerialHandle); mIsOpen = false; }

    const std::string getPortName() const { return mPort; }

    void setPort(const std::string& port) { mPort = port; }

    void setBaudRate(const uint32_t baudrate) { mBaudrate = baudrate; }

    void setDataBits(const DataBits databits) { mDataBits = databits; }

    void setStopBits(const StopBits stopbits) { mStopBits = stopbits; }

    void setParity(const Parity parity) { mParity = parity; }
    
    const uint32_t getBaudrate() const { return mBaudrate; }

    static std::vector<std::string> enumerateComPorts() {

        std::vector<std::string> validPorts;
        const uint32_t MAX_PORTS = 255;
        char path[1024];
        for (uint32_t k = 0; k < MAX_PORTS; k++) {
            std::string port_name = "COM" + std::to_string(k);
            DWORD test = QueryDosDevice(port_name.c_str(), path, 1024);
            if (test == 0) continue;
            validPorts.push_back(port_name);
        }

        return validPorts;

    }

private:
    
    std::string mPort = "";
    uint32_t mBaudrate = 115200;
    uint32_t mDataBits = DATABITS_8;
    uint32_t mParity   = PARITY_NONE;
    uint32_t mStopBits = STOPBITS_10;
    HANDLE mSerialHandle = nullptr;
    bool mIsOpen = false;

    std::array<uint8_t, 8192> mBuffer;
    size_t mNumBytesInBuffer = 0;
    std::mutex mutex;

};


#endif // SERAIL_WINDOWS_HPP
