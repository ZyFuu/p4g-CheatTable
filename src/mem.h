#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

// Process memory access. The Windows build talks to P4G.exe; the Linux preview build uses a fake process
// so the UI can be rendered and screenshotted without the game.
class IMemory {
public:
    virtual ~IMemory() {}
    virtual bool attached() const = 0;
    virtual void poll() = 0;                                   // attach / detect exit (cheap, call every frame)
    virtual bool read(uint64_t addr, void* buf, size_t n) = 0;
    virtual bool write(uint64_t addr, const void* buf, size_t n) = 0;
    virtual uint64_t moduleBase() const = 0;                   // P4G.exe image base
    virtual uint64_t allocExec(uint64_t nearAddr, size_t size) = 0; // RWX page within +/-2GB of `nearAddr`, 0 on failure
    virtual bool freeMem(uint64_t addr) = 0;
    virtual bool writeCode(uint64_t addr, const void* buf, size_t n) = 0; // write into a code page
    virtual std::string status() const = 0;

    template <class T> T rd(uint64_t a, T def = T()) { T v{}; return read(a, &v, sizeof v) ? v : def; }
    template <class T> bool wr(uint64_t a, T v) { return write(a, &v, sizeof v); }
};

IMemory* createProcessMemory();
