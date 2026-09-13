#pragma once
#include "mem.h"
#include <cstdint>
#include <string>
#include <vector>

// Addresses verified in the 64-bit Steam P4G.exe (fixed image base 140000000, no relocations).
namespace G {
    const uint64_t SAVE  = 0x1451BCD70ULL;   // save block: +0 money, +A30 equipped persona slot, +A34 12 persona records (0x30 each)
    const uint64_t PARTY = 0x1451BD9E4ULL;   // 8 party units of 0x84: MC, Yosuke, Chie, Yukiko, Rise, Kanji, Naoto, Teddie
    const uint64_t FLAGS = 0x1451BDF0CULL;   // event flag bitfield (bit n: byte n>>3, bit n&7)
    const uint64_t TIME  = 0x1451BE64CULL;   // +0 day index (word), +2 time of day (byte), +8 next day, +A next time
    const uint64_t SLINK = 0x1451BE660ULL;   // 23 records of 16: +0 community id, +2 rank, +4 points
    const uint64_t COMP  = 0x1451BF280ULL;   // compendium: id * 0x30 -> +0 flags, +2 id, +4 level, +8 exp, +C skills[8], +1C stats[5]
    const uint64_t CACHE_END = 0x1451C2500ULL;
    const int STRIDE = 0x84;
    const uint64_t AFF_PTR_OFF   = 0xEC0988;   // module-relative pointer to UNIT.TBL segment 2 (persona affinities, 32 bytes per id)
    const uint64_t CRIT_SITE_OFF = 0xD50E6;    // cmp r8d,r14d / jl +0D   (crit roll)
    const uint64_t EXP_SITE_OFF  = 0x101256;   // test edi,edi / jle +23 / mov eax,1869F  (EXP gain helper)
    const uint64_t CODE_START = 0x1000, CODE_SIZE = 0x885600;   // .shared section of the exe

    inline uint64_t unit(int i) { return PARTY + (uint64_t)i * STRIDE; }
    inline uint64_t mcPersona(int slot) { return SAVE + 0xA34 + (uint64_t)slot * 0x30; }
    inline uint64_t partyPersona(int member) { return unit(member) + 0x54; }   // same record layout as mcPersona()
    inline uint64_t compRecord(int id) { return COMP + (uint64_t)id * 0x30; }
    static const char* const MEMBER_NAMES[8] = {"Yu (MC)", "Yosuke", "Chie", "Yukiko", "Rise", "Kanji", "Naoto", "Teddie"};
}

struct HookState { uint64_t site = 0; size_t len = 0; uint8_t orig[16] = {}; uint64_t cave = 0; bool active = false; };

class Game {
public:
    explicit Game(IMemory* m);
    ~Game();
    IMemory& mem() { return *mem_; }
    bool attached() const { return mem_->attached(); }
    std::string status() const { return mem_->status(); }
    void tick(double now);          // call once per frame

    // cached reads (refreshed every 100 ms) + direct writes
    uint8_t  r8(uint64_t a);  uint16_t r16(uint64_t a);  uint32_t r32(uint64_t a);  uint64_t r64(uint64_t a);
    void w8(uint64_t a, uint8_t v);  void w16(uint64_t a, uint16_t v);  void w32(uint64_t a, uint32_t v);
    bool flag(int bit);  void setFlag(int bit, bool on);

    // ---- features driven every tick ----
    int buffMode[8] = {0};      // per member, 0 = leave alone, 1..7 = combo, 8 = clear
    int chargeMode[8] = {0};    // 0 = leave alone, 1..10 = combo, 11 = clear
    int stayDaytime = 0;        // 0 off, 1 once per day, 2 always
    int freezeHp[8] = {-1,-1,-1,-1,-1,-1,-1,-1};   // per member: -1 off, else the value written every 100 ms
    int freezeSp[8] = {-1,-1,-1,-1,-1,-1,-1,-1};
    std::vector<int> freezeCat; // per item category: -1 = off, else quantity enforced
    std::string lastEvent;

    // ---- hooks ----
    int critMode() const { return critMode_; }
    bool setCritMode(int mode);     // 0 off, 1 MC only, 2 whole party
    bool expEnabled() const { return exp_.active; }
    bool setExpEnabled(bool on);
    int expMult() const { return expMult_; }
    void setExpMult(int m);
    std::string hookError;

    // ---- compendium ----
    int registerMissingPersonas();  // returns the number of records written

    // ---- affinities ----
    uint64_t affinityTable();       // 0 if the pointer is not readable
    int equippedPersonaId();

private:
    IMemory* mem_;
    std::vector<uint8_t> cache_;
    double lastRefresh_ = -1, lastEnforce_ = -1;
    bool cacheOk_ = false;
    int lastTod_ = -1, lastDay_ = -1, revertedDay_ = -1;
    HookState crit_, exp_;
    int critMode_ = 0, expMult_ = 2;

    void refresh();
    void enforce();
    bool locateSite(HookState& h, uint64_t off, const uint8_t* pat, size_t n, size_t show, const char* name);
    bool installHook(HookState& h, const std::vector<uint8_t>& cave, size_t siteLen);
    void removeHook(HookState& h);
    std::vector<uint8_t> buildCritCave(uint64_t cave, bool party);
    std::vector<uint8_t> buildExpCave(uint64_t cave);
    void detachHooks();
};
