#ifndef SERIAL_WINDOWS_HPP
#define SERIAL_WINDOWS_HPP

#define NOMINMAX 1
#define WIN32_LEAN_AND_MEAN
#include "Windows.h"
#include <chrono>
#include <cstdint>
#include <thread>
#include <string>
#include <array>
#include <mutex>
#include <algorithm>
#include <vector>

class Serial {

public:

    Serial() { };

    ~Serial() { close(); };

    enum class Error {
        None,
        UnableToOpenPort,
        CannotGetCommState,
        CannotSetCommState,
        CannotSetCommTimeout,
        CannotGetCommTimeout,
        
    };

    enum StopBits {
        ONE  = ONESTOPBIT,
        HALF = ONE5STOPBITS,
        TWO  = TWOSTOPBITS,
    };

    enum DataBits {
        EIGHT = 8,
        SEVEN = 7,
        SIX   = 6,
        FIVE  = 5,
    };
    
    enum Parity {
        NONE  = NOPARITY,
        ODD   = ODDPARITY,
        EVEN  = EVENPARITY,
        MARK  = MARKPARITY,
        SPACE = SPACEPARITY,
    };

    Error open(const std::string& port, const uint32_t baudrate = 115200) {

        std::scoped_lock<std::mutex> lock(mMutex);

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
            mError = Error::UnableToOpenPort;
            return mError;
        } else {
            mIsOpen = true;
        }

        mError = configurePort();
        return mError;

    }

    Error configurePort() {

        DCB serialConfig = {0};

        if (!GetCommState(mSerialHandle, &serialConfig)) return Error::CannotGetCommState;

        serialConfig.BaudRate    = mBaudrate;
        serialConfig.ByteSize    = mDataBits;
        serialConfig.StopBits    = mStopBits;
        serialConfig.Parity      = mParity;
        serialConfig.fOutxCtsFlow = false;
        serialConfig.fRtsControl = RTS_CONTROL_DISABLE;
        serialConfig.fOutX = false;
        serialConfig.fInX = false;

        
        if (!SetCommState(mSerialHandle, &serialConfig)) return Error::CannotGetCommState;

        COMMTIMEOUTS serialTimeouts = {0};
        if (!GetCommTimeouts(mSerialHandle, &serialTimeouts)) return Error::CannotGetCommTimeout;
        serialTimeouts.WriteTotalTimeoutConstant   = 100; // prevent long blocks on WriteFile with write timeout
        serialTimeouts.WriteTotalTimeoutMultiplier = 0;

        if (!SetCommTimeouts(mSerialHandle, &serialTimeouts)) return Error::CannotSetCommTimeout;
        
        PurgeComm(mSerialHandle, PURGE_RXCLEAR | PURGE_TXCLEAR);
        return Error::None;
    }


    size_t copyBytes(uint8_t* dest) {

        std::scoped_lock<std::mutex> lock(mMutex);

        if (!mIsOpen || mNumBytesInBuffer == 0) return 0;

        std::copy(mBuffer.begin(), mBuffer.begin()+mNumBytesInBuffer, dest);
        
        const auto bytesCopied = mNumBytesInBuffer;
        mNumBytesInBuffer = 0;
        return bytesCopied;

    }
            
    size_t read() {

        std::scoped_lock<std::mutex> lock(mMutex);

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

        std::scoped_lock<std::mutex> lock(mMutex);

        if (!mIsOpen) { return 0; }

        DWORD bytesWritten = 0;
        
        if (!WriteFile(mSerialHandle, buffer, length, &bytesWritten, nullptr)) { return false; }

        return true;
    }

    bool send(std::string toSend) {
        return send(toSend.c_str(), toSend.size());
    }

    void sendBreakState() {
        std::scoped_lock<std::mutex> lock(mMutex);
        SetCommBreak(mSerialHandle);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        ClearCommBreak(mSerialHandle);
    }

    bool isConnected() { return mIsOpen; }

    void close() { CloseHandle(mSerialHandle); mIsOpen = false; }

    const std::string getPortName() const { return mPort; }

    void setPort(const std::string& port) { mPort = port; }

    void setBaudRate(const uint32_t baudrate) { mBaudrate = baudrate; }

    void setDataBits(const DataBits databits) { mDataBits = databits; }

    void setStopBits(const StopBits stopbits) { mStopBits = stopbits; }

    void setParity(const Parity parity) { mParity = parity; }

    const std::string getLastError() const {
        switch(mError) {
            case Error::None: return "";
            case Error::UnableToOpenPort: return "UnableToOpenPort";
            case Error::CannotGetCommState: return "CannotGetCommState";
            case Error::CannotSetCommState: return "CannotSetCommState";
            case Error::CannotSetCommTimeout: return "CannotSetCommTimeout";
            case Error::CannotGetCommTimeout: return "CannotGetCommTimeout";
        }
        return "";
    }

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
    
    static constexpr std::array<const char*,4> sLineEndings = {"\r\n", "\n", "\r", ""};
    Error mError = Error::None;
    size_t mLineEndingState = 0;
    std::string mPort = "";
    uint32_t mBaudrate = 115200;
    uint32_t mDataBits = DataBits::EIGHT;
    uint32_t mParity   = Parity::NONE;
    uint32_t mStopBits = StopBits::ONE;
    HANDLE mSerialHandle = nullptr;
    bool mIsOpen = false;

    std::array<uint8_t, 8192> mBuffer;
    size_t mNumBytesInBuffer = 0;
    std::mutex mMutex;

};


#endif // SERAIL_WINDOWS_HPP
