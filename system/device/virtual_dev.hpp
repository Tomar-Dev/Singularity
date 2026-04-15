// system/device/virtual_dev.hpp
#ifndef VIRTUAL_DEV_HPP
#define VIRTUAL_DEV_HPP

#include "system/device/device.h"
class NullDevice : public Device {
public:
    NullDevice() : Device("null", DEV_CHAR) {}
    int init() override { return 1; }
    int read(char* buffer, int size) override { (void)buffer; (void)size; return 0; }
    int write(const char* buffer, int size) override { (void)buffer; return size; }
};

class ZeroDevice : public Device {
public:
    ZeroDevice() : Device("zero", DEV_CHAR) {}
    int init() override { return 1; }
    int read(char* buffer, int size) override;
    int write(const char* buffer, int size) override { (void)buffer; return size; }
};

class RandomDevice : public Device {
public:
    RandomDevice() : Device("random", DEV_CHAR) {}
    int init() override { return 1; }
    int read(char* buffer, int size) override;
    int write(const char* buffer, int size) override { (void)buffer; return size; }
};

#endif
