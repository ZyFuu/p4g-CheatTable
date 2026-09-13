#ifndef _WIN32
// Preview build only: a fake P4G process with plausible save data, so the UI can be rendered without the game.
#include "mem.h"
#include "game.h"
#include <unordered_map>
#include <cstring>

namespace {

class FakeMemory : public IMemory {
    std::unordered_map<uint64_t, std::vector<uint8_t>> pages_;   // 4 KB pages
    uint64_t nextAlloc_ = 0x150000000ULL;

    std::vector<uint8_t>& page(uint64_t a) {
        auto& p = pages_[a & ~0xFFFULL];
        if (p.empty()) p.assign(0x1000, 0);
        return p;
    }
    void put(uint64_t a, const void* b, size_t n) {
        const uint8_t* s = (const uint8_t*)b;
        for (size_t i = 0; i < n; i++) page(a + i)[(a + i) & 0xFFF] = s[i];
    }
    template <class T> void set(uint64_t a, T v) { put(a, &v, sizeof v); }

public:
    FakeMemory() {
        using namespace G;
        set<uint32_t>(SAVE, 123456);                       // money
        set<int16_t>(SAVE + 0xA30, 0);                     // equipped persona slot
        const uint16_t ids[12] = {1, 12, 30, 45, 0, 0, 0, 0, 0, 0, 0, 0};
        for (int s = 0; s < 12; s++) {
            uint64_t r = mcPersona(s);
            set<uint16_t>(r, ids[s] ? 1 : 0); set<uint16_t>(r + 2, ids[s]); set<uint16_t>(r + 4, ids[s] ? 20 + s * 3 : 0);
            set<uint32_t>(r + 8, ids[s] ? 12345 * (s + 1) : 0);
            const uint16_t sk[8] = {39, 121, 222, 40, 0, 0, 0, 0};
            for (int k = 0; k < 8; k++) set<uint16_t>(r + 0xC + k * 2, ids[s] ? sk[k] : 0);
            for (int k = 0; k < 5; k++) set<uint8_t>(r + 0x1C + k, ids[s] ? 10 + k * 3 + s : 0);
        }
        set<uint8_t>(unit(0) + 6, 25); set<uint16_t>(unit(0) + 8, 210); set<uint16_t>(unit(0) + 0xA, 140); set<uint32_t>(unit(0) + 0x40, 98765);
        const uint16_t social[5] = {45, 82, 30, 16, 60};
        for (int i = 0; i < 5; i++) set<uint16_t>(unit(0) + 0x34 + i * 2, social[i]);
        const uint16_t partyPersona[8] = {0, 192, 194, 196, 200, 198, 204, 202};
        for (int m = 1; m < 8; m++) {
            uint64_t u = unit(m);
            set<uint16_t>(u + 8, 150 + m * 10); set<uint16_t>(u + 0xA, 90 + m * 5);
            set<uint16_t>(u + 0x56, partyPersona[m]); set<uint8_t>(u + 0x58, 22 + m); set<uint32_t>(u + 0x5C, 4000 * m);
            const uint16_t sk[8] = {1, 14, 26, 0, 0, 0, 0, 0};
            for (int k = 0; k < 8; k++) set<uint16_t>(u + 0x60 + k * 2, sk[k]);
            for (int k = 0; k < 5; k++) set<uint8_t>(u + 0x70 + k, 12 + k + m);
        }
        set<uint16_t>(TIME, 41); set<uint8_t>(TIME + 2, 4);    // 05/12, after school
        const uint16_t links[5] = {1, 7, 11, 5, 2};
        for (int i = 0; i < 5; i++) { set<uint16_t>(SLINK + i * 16, links[i]); set<uint16_t>(SLINK + i * 16 + 2, 3 + i); set<uint16_t>(SLINK + i * 16 + 4, 7 * i); }
        for (int i = 1; i < 8; i++) { uint64_t c = COMP + i * 0x30; set<uint16_t>(c, 1); set<uint16_t>(c + 2, i); set<uint16_t>(c + 4, 5 + i); }
        // hook sites with the game's original bytes, and the affinity table pointer
        const uint8_t crit[5] = {0x45, 0x3B, 0xC6, 0x7C, 0x0D};
        put(moduleBase() + CRIT_SITE_OFF, crit, 5);
        const uint8_t exp[28] = {0x85,0xFF,0x7E,0x23,0xB8,0x9F,0x86,0x01,0x00,0x44,0x3B,0xC0,0x7F,0x1C,0x45,0x85,0xC0,0x41,0x8B,0xC0,0xB9,0x01,0x00,0x00,0x00,0x0F,0x4E,0xC1};
        put(moduleBase() + EXP_SITE_OFF, exp, 28);
        set<uint64_t>(moduleBase() + AFF_PTR_OFF, 0x160000000ULL);
        const uint16_t aff[16] = {0x14, 0x800, 0x14, 0x1000, 0x14, 0x14, 0x100, 0x200, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14};
        for (int i = 0; i < 256; i++) put(0x160000000ULL + i * 32, aff, 32);
    }
    bool attached() const override { return true; }
    void poll() override {}
    uint64_t moduleBase() const override { return 0x140000000ULL; }
    std::string status() const override { return "PREVIEW MODE - fake P4G.exe (base 140000000)"; }
    bool read(uint64_t a, void* b, size_t n) override {
        uint8_t* d = (uint8_t*)b;
        for (size_t i = 0; i < n; i++) {
            auto it = pages_.find((a + i) & ~0xFFFULL);
            d[i] = it == pages_.end() ? 0 : it->second[(a + i) & 0xFFF];
        }
        return true;
    }
    bool write(uint64_t a, const void* b, size_t n) override { put(a, b, n); return true; }
    bool writeCode(uint64_t a, const void* b, size_t n) override { put(a, b, n); return true; }
    uint64_t allocExec(uint64_t, size_t size) override { uint64_t r = nextAlloc_; nextAlloc_ += (size + 0xFFFF) & ~0xFFFFULL; return r; }
    bool freeMem(uint64_t) override { return true; }
};

} // namespace

IMemory* createProcessMemory() { return new FakeMemory(); }
#endif
