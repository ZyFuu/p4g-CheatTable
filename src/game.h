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
    const uint64_t BTL_PTR_OFF   = 0xEC08F0;   // module-relative pointer to the battle work; +12A0 = result params, +12E0 = shuffle params
    const uint64_t SFL_ALWAYS_OFF = 0xB46B3;   // cmp r8d,eax / jae +2F : Shuffle Time roll (rand%100 < chance)
    const uint64_t SFL_LEVEL_OFF  = 0x10849A;  // mov [rbp+1CF90],r8d : shuffle level 0..7 picked from btlSflDifficultyTable
    const uint64_t SFL_FORCE_OFF  = 0x1097DC;  // test ebp,ebp / je / mov [r12+8],ebp : end of the persona candidate list builder
    const uint64_t SFL_DRAW_OFF   = 0x12A5E0;  // drawCard(ctx, kind): kind 0 = weighted draw over the 22 major + 40 minor arcana (+ persona)
    const uint64_t SFL_DEAL_OFF   = 0x12D6B7;  // test ebx,ebx / jle : start of the deal loop (ebx = number of cards to draw, max 6)
    const uint64_t SFL_COUNT_OFF  = 0x12D655;  // mov ebx,[rsi+rcx*4+1BB54] : number of cards to draw picked from the layout row
    const uint64_t SOC_ADD_OFF    = 0x42675A;  // social stat up event (cmmmisc.c): add eax,ebx / cmp eax,ecx(999) / cmovg -> points += gain
    const uint64_t SYM_PICK_OFF   = 0x2CA0C3;  // field encounter picker (k_encount.c): tail where the symbol group (edi..r12, type r15) is final
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
    int enemyDebuff = 0;        // bitmask written on every living enemy while in battle: 1 attack, 2 defense, 4 hit+evasion
    int64_t freezeMoney = -1;   // -1 off, else the yen value written every 100 ms
    int freezeSlink[23] = {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1};   // per slot: -1 off, else points
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

    // ---- Shuffle Time ----
    bool shuffleAlways() const { return sflAlways_.active; }
    bool setShuffleAlways(bool on);
    bool shuffleLevelForced() const { return sflLevel_.active; }
    bool setShuffleLevelForced(bool on);
    int shuffleLevel() const { return sflLevelVal_; }
    void setShuffleLevel(int lvl);              // 0..7, written into the cave when active
    bool forcePersonasEnabled() const { return sflForce_.active; }
    bool setForcePersonas(bool on);
    uint16_t forcedPersona[4] = {0, 0, 0, 0};
    void syncForcedPersonas();                  // push forcedPersona[] into the cave
    // forced cards per dealt slot (0..5): 0 = game's draw, else card id + 1 (major arcana 0..21, minor 22 + suit*10 + rank-1)
    uint16_t forceCards[6] = {0, 0, 0, 0, 0, 0};
    int forceCount = 0;                         // 0 = game's choice, 1..6 = number of cards dealt
    bool forceCardsEnabled() const { return sflDraw_.active; }
    bool setForceCards(bool on);
    void syncForceCards();
    // ---- social stat gain multiplier ----
    bool socialMultEnabled() const { return soc_.active; }
    bool setSocialMultEnabled(bool on);
    int socialMult() const { return socMult_; }
    void setSocialMult(int m);

    // ---- dungeon shadow symbols ----
    int symbolMode() const { return symMode_; }
    bool setSymbolMode(int mode);               // 0 game's choice, 2 = group 2 symbols only, 3 = group 3 symbols only
    struct ShuffleInfo { bool ok = false; uint16_t ids[4] = {0,0,0,0}; int count = 0, cards = 0, mode = 0, level = 0; bool triggered = false; };
    ShuffleInfo shuffleInfo();                  // what the game decided at the end of the last battle

    // ---- configuration file (key=value, next to the exe) ----
    void setConfigPath(const std::string& p) { cfgPath_ = p; }
    const std::string& configPath() const { return cfgPath_; }
    bool saveConfig();              // writes every option below
    bool loadConfig();              // reads the file; hook options are applied on the next attach (or now if attached)
    bool configExists() const;
    bool configAutoApply() const;   // the autoApply flag stored in the file (without loading the rest)
    bool storeAutoApply();          // writes only the autoApply flag into an existing file
    bool autoApply = true;          // stored in the file: re-apply the saved options on every attach
    std::string configStatus;

    // ---- compendium ----
    int registerMissingPersonas();  // returns the number of records written

    // ---- affinities ----
    uint64_t affinityTable();       // 0 if the pointer is not readable
    int equippedPersonaId();
    // UNIT.TBL segment 2 is reloaded from disk at every game start, so edits are kept as overrides and re-written
    // every 100 ms while the table is loaded (the original value is remembered so the edit can be undone)
    struct AffOverride { uint16_t pid, slot, value, orig; };
    std::vector<AffOverride> affOverrides;
    void setAffinity(int pid, int slot, uint16_t value);
    void clearAffinities(int pid);              // -1 = every persona; restores the original values
    const AffOverride* affinityOverride(int pid, int slot) const;

private:
    IMemory* mem_;
    std::vector<uint8_t> cache_;
    double lastRefresh_ = -1, lastEnforce_ = -1;
    bool cacheOk_ = false;
    int lastTod_ = -1, lastDay_ = -1, revertedDay_ = -1;
    HookState crit_, exp_, sflAlways_, sflLevel_, sflForce_, sflDraw_, sflDeal_, sflCount_, sym_, soc_;
    std::string cfgPath_;
    bool pending_ = false;          // saved hook options waiting for an attached game
    int pCrit_ = 0, pSym_ = 0; bool pExp_ = false, pAlways_ = false, pCards_ = false, pSoc_ = false;
    double nextApply_ = 0;          // earliest time for the next attempt (the game code is not always ready at attach)
    int applyTries_ = 0;
    void applyPending(double now);
    int critMode_ = 0, expMult_ = 2, sflLevelVal_ = 7, symMode_ = 0, socMult_ = 2;

    void refresh();
    void enforce();
    bool locateSite(HookState& h, uint64_t off, const uint8_t* pat, size_t n, size_t show, const char* name);
    bool installHook(HookState& h, const std::vector<uint8_t>& cave, size_t siteLen);
    void removeHook(HookState& h);
    std::vector<uint8_t> buildCritCave(uint64_t cave, bool party);
    std::vector<uint8_t> buildExpCave(uint64_t cave);
    std::vector<uint8_t> buildLevelCave(uint64_t cave);
    std::vector<uint8_t> buildForceCave(uint64_t cave);
    std::vector<uint8_t> buildDrawCave(uint64_t cave);
    std::vector<uint8_t> buildDealCave(uint64_t cave, uint64_t data);
    std::vector<uint8_t> buildCountCave(uint64_t cave, uint64_t data);
    std::vector<uint8_t> buildSymbolCave(uint64_t cave);
    std::vector<uint8_t> buildSocialCave(uint64_t cave);
    bool installPatch(HookState& h, uint64_t addr, const uint8_t* bytes, size_t n);
    void detachHooks();
};
