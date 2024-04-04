#include <cstddef>
#include <cstdint>
#include <exception>
#include <fileapi.h>
#include <handleapi.h>
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

    void open(const std::string& port, const uint32_t baudrate = 115200) {

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
            throw std::exception();
        } else {
            mIsOpen = true;
        }

        configurePort();

    }
    
    void configurePort() {

        DCB serialConfig = {0};

        GetCommState(mSerialHandle, &serialConfig);

        serialConfig.BaudRate    = mBaudrate;
        serialConfig.ByteSize    = 8;    
        serialConfig.StopBits    = ONESTOPBIT;
        serialConfig.Parity      = NOPARITY;
        serialConfig.fDtrControl = DTR_CONTROL_DISABLE;
        
        SetCommState(mSerialHandle, &serialConfig);

        PurgeComm(mSerialHandle, PURGE_RXCLEAR | PURGE_TXCLEAR);

    }

    size_t copyBytes(uint8_t* dest) {

        std::unique_lock<std::mutex> lock(mutex);

        if (!mNumBytesInBuffer) return 0;

        std::copy(mBuffer.begin(), mBuffer.begin()+mNumBytesInBuffer, dest);
        
        const auto bytesCopied = mNumBytesInBuffer;
        mNumBytesInBuffer = 0;
        return bytesCopied;

    }
            
    size_t read() {

        std::unique_lock<std::mutex> lock(mutex);

        DWORD err;
        COMSTAT stat;
    
        ClearCommError(mSerialHandle, &err, &stat);
        DWORD bytesRead;

        if (stat.cbInQue == 0) { return 0; }

        ReadFile(mSerialHandle, &mBuffer[mNumBytesInBuffer], stat.cbInQue, &bytesRead, nullptr);        
        mNumBytesInBuffer += bytesRead;

        return bytesRead;

    }

    void send(const char* buffer, size_t length) {

        std::unique_lock<std::mutex> lock(mutex);

        DWORD bytesWritten = 0;
        
        if (!WriteFile(mSerialHandle, buffer, length, &bytesWritten, nullptr)) {
            throw std::exception();
        }

    }

    void send(const std::string toSend) { send(toSend.c_str(), toSend.size()); }

    void close() { CloseHandle(mSerialHandle); mIsOpen = false; }

    const std::string getPortName() const { return mPort; }

    void setPortName(const std::string& port) { mPort = port; }

    const size_t getBaudrate() const { return mBaudrate; }

    void setBaudRate(const uint32_t baudrate) { mBaudrate = baudrate; }

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
    HANDLE mSerialHandle = nullptr;
    bool mIsOpen = false;

    std::array<uint8_t, 4096> mBuffer;
    size_t mNumBytesInBuffer = 0;
    std::mutex mutex;

};


#endif // SERAIL_WINDOWS_HPP
