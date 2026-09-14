#ifdef _WIN32
#include "mem.h"
#include <windows.h>
#include <tlhelp32.h>
#include <cstring>

namespace {

class WinMemory : public IMemory {
    HANDLE proc_ = nullptr;
    DWORD pid_ = 0;
    uint64_t base_ = 0;
    uint64_t size_ = 0;
    ULONGLONG lastScan_ = 0;
    std::string status_ = "Waiting for P4G.exe...";

    void detach() {
        if (proc_) CloseHandle(proc_);
        proc_ = nullptr; pid_ = 0; base_ = 0; size_ = 0;
        status_ = "Waiting for P4G.exe...";
    }

    bool findProcess(DWORD& pid) {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) return false;
        PROCESSENTRY32W pe; pe.dwSize = sizeof pe;
        bool found = false;
        if (Process32FirstW(snap, &pe)) {
            do {
                if (_wcsicmp(pe.szExeFile, L"P4G.exe") == 0) { pid = pe.th32ProcessID; found = true; break; }
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
        return found;
    }

    bool findModule() {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid_);
        if (snap == INVALID_HANDLE_VALUE) return false;
        MODULEENTRY32W me; me.dwSize = sizeof me;
        bool ok = false;
        if (Module32FirstW(snap, &me)) {
            do {
                if (_wcsicmp(me.szModule, L"P4G.exe") == 0) {
                    base_ = (uint64_t)me.modBaseAddr; size_ = me.modBaseSize; ok = true; break;
                }
            } while (Module32NextW(snap, &me));
        }
        CloseHandle(snap);
        return ok;
    }

public:
    ~WinMemory() override { detach(); }

    bool attached() const override { return proc_ != nullptr; }
    uint64_t moduleBase() const override { return base_; }
    std::string status() const override { return status_; }

    void poll() override {
        if (proc_) {
            if (WaitForSingleObject(proc_, 0) == WAIT_OBJECT_0) detach();
            return;
        }
        ULONGLONG now = GetTickCount64();
        if (now - lastScan_ < 1000) return;
        lastScan_ = now;
        DWORD pid = 0;
        if (!findProcess(pid)) return;
        HANDLE h = OpenProcess(PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION | PROCESS_QUERY_INFORMATION | SYNCHRONIZE, FALSE, pid);
        if (!h) { status_ = "P4G.exe found but OpenProcess failed - run the trainer as administrator"; return; }
        proc_ = h; pid_ = pid;
        if (!findModule()) {
            // the module list is not ready during the first milliseconds of the process: retry on the next poll
            CloseHandle(proc_); proc_ = nullptr; pid_ = 0; lastScan_ = now - 500;
            return;
        }
        char buf[128];
        snprintf(buf, sizeof buf, "Attached to P4G.exe (PID %lu, base %llX)", (unsigned long)pid_, (unsigned long long)base_);
        status_ = buf;
    }

    bool read(uint64_t addr, void* buf, size_t n) override {
        if (!proc_) return false;
        SIZE_T got = 0;
        return ReadProcessMemory(proc_, (LPCVOID)addr, buf, n, &got) && got == n;
    }

    bool write(uint64_t addr, const void* buf, size_t n) override {
        if (!proc_) return false;
        SIZE_T put = 0;
        return WriteProcessMemory(proc_, (LPVOID)addr, buf, n, &put) && put == n;
    }

    bool writeCode(uint64_t addr, const void* buf, size_t n) override {
        if (!proc_) return false;
        DWORD old = 0;
        if (!VirtualProtectEx(proc_, (LPVOID)addr, n, PAGE_EXECUTE_READWRITE, &old)) return false;
        SIZE_T put = 0;
        bool ok = WriteProcessMemory(proc_, (LPVOID)addr, buf, n, &put) && put == n;
        DWORD tmp = 0;
        VirtualProtectEx(proc_, (LPVOID)addr, n, old, &tmp);
        FlushInstructionCache(proc_, (LPCVOID)addr, n);
        return ok;
    }

    uint64_t allocExec(uint64_t nearAddr, size_t size) override {
        if (!proc_) return 0;
        // walk the address space upward from the module and take the first free block within +/-2GB (rel32 reach)
        uint64_t lo = nearAddr > 0x7FF00000ULL ? nearAddr - 0x7FF00000ULL : 0x10000;
        uint64_t hi = nearAddr + 0x7FF00000ULL;
        MEMORY_BASIC_INFORMATION mbi;
        uint64_t p = base_ + size_;
        while (p < hi && VirtualQueryEx(proc_, (LPCVOID)p, &mbi, sizeof mbi) == sizeof mbi) {
            uint64_t start = (uint64_t)mbi.BaseAddress, len = mbi.RegionSize;
            if (mbi.State == MEM_FREE && len >= size + 0x10000) {
                uint64_t cand = (start + 0xFFFF) & ~0xFFFFULL;
                if (cand >= lo && cand + size <= start + len) {
                    void* r = VirtualAllocEx(proc_, (LPVOID)cand, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
                    if (r) return (uint64_t)r;
                }
            }
            p = start + len;
        }
        return 0;
    }

    bool protectExec(uint64_t addr, size_t n) override {
        if (!proc_) return false;
        DWORD old = 0;
        if (!VirtualProtectEx(proc_, (LPVOID)addr, n, PAGE_EXECUTE_READ, &old)) return false;
        FlushInstructionCache(proc_, (LPCVOID)addr, n);
        return true;
    }

    bool freeMem(uint64_t addr) override {
        if (!proc_ || !addr) return false;
        return VirtualFreeEx(proc_, (LPVOID)addr, 0, MEM_RELEASE) != 0;
    }
};

} // namespace

IMemory* createProcessMemory() { return new WinMemory(); }
#endif
