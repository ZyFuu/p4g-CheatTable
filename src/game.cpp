#include "game.h"
#include "data.h"
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <string>
#include <map>
#include <sstream>
#include <fstream>
#ifdef _WIN32
#include <windows.h>
#endif

using namespace G;

static const uint8_t CRIT_PATTERN[5] = {0x45, 0x3B, 0xC6, 0x7C, 0x0D};
static const uint8_t SFL_ALWAYS_PATTERN[8] = {0x44,0x3B,0xC0,0x73,0x2F,0x48,0x8B,0x1D};
static const uint8_t SFL_LEVEL_PATTERN[12] = {0x44,0x89,0x85,0x90,0xCF,0x01,0x00,0x0F,0xB7,0x07,0xA8,0x10};
static const uint8_t SFL_FORCE_PATTERN[15] = {0x85,0xED,0x74,0x0B,0x41,0x89,0x6C,0x24,0x08,0x41,0x8D,0x46,0x01,0xEB,0x02};
static const uint8_t SFL_DRAW_PATTERN[26] = {0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0xE9,0x0F,0xB6,0xDA};
static const uint8_t SFL_DEAL_PATTERN[13] = {0x85,0xDB,0x7E,0x12,0x0F,0x1F,0x44,0x00,0x00,0x48,0x8B,0xCE,0xE8};
static const uint8_t SFL_COUNT_PATTERN[13] = {0x8B,0x9C,0x8E,0x54,0xBB,0x01,0x00,0x8B,0x88,0x84,0x02,0x00,0x00};
static const uint8_t SYM_PICK_PATTERN[13] = {0x44,0x0F,0x28,0x44,0x24,0x40,0x8B,0xF5,0x0F,0x28,0x7C,0x24,0x50};
static const uint8_t SOC_ADD_PATTERN[15] = {0x03,0xC3,0x3B,0xC1,0x0F,0x4F,0xC1,0x0F,0xB7,0xCD,0x66,0x89,0x44,0x72,0x34};
static const uint8_t EXP_PATTERN[28] = {0x85,0xFF,0x7E,0x23,0xB8,0x9F,0x86,0x01,0x00,0x44,0x3B,0xC0,0x7F,0x1C,0x45,0x85,0xC0,0x41,0x8B,0xC0,0xB9,0x01,0x00,0x00,0x00,0x0F,0x4E,0xC1};

Game::Game(IMemory* m) : mem_(m) {
    cache_.assign((size_t)(CACHE_END - SAVE), 0);
    freezeCat.assign(ITEM_CAT_COUNT, -1);
}

Game::~Game() { detachHooks(); }

// ------------------------------------------------------------------ cache
void Game::refresh() {
    cacheOk_ = mem_->attached() && mem_->read(SAVE, cache_.data(), cache_.size());
}
static inline bool inCache(uint64_t a, size_t n) { return a >= SAVE && a + n <= CACHE_END; }

uint8_t Game::r8(uint64_t a) {
    if (cacheOk_ && inCache(a, 1)) return cache_[a - SAVE];
    return mem_->rd<uint8_t>(a);
}
uint16_t Game::r16(uint64_t a) {
    if (cacheOk_ && inCache(a, 2)) { uint16_t v; memcpy(&v, &cache_[a - SAVE], 2); return v; }
    return mem_->rd<uint16_t>(a);
}
uint32_t Game::r32(uint64_t a) {
    if (cacheOk_ && inCache(a, 4)) { uint32_t v; memcpy(&v, &cache_[a - SAVE], 4); return v; }
    return mem_->rd<uint32_t>(a);
}
uint64_t Game::r64(uint64_t a) { return mem_->rd<uint64_t>(a); }
void Game::w8(uint64_t a, uint8_t v)   { if (mem_->wr(a, v) && inCache(a, 1)) cache_[a - SAVE] = v; }
void Game::w16(uint64_t a, uint16_t v) { if (mem_->wr(a, v) && inCache(a, 2)) memcpy(&cache_[a - SAVE], &v, 2); }
void Game::w32(uint64_t a, uint32_t v) { if (mem_->wr(a, v) && inCache(a, 4)) memcpy(&cache_[a - SAVE], &v, 4); }
bool Game::flag(int bit) { return (r8(FLAGS + (bit >> 3)) >> (bit & 7)) & 1; }
void Game::setFlag(int bit, bool on) {
    uint8_t b = r8(FLAGS + (bit >> 3));
    b = on ? (b | (1 << (bit & 7))) : (b & ~(1 << (bit & 7)));
    w8(FLAGS + (bit >> 3), b);
}

// ------------------------------------------------------------------ per-frame
void Game::tick(double now) {
    bool was = mem_->attached();
    mem_->poll();
    if (was && !mem_->attached()) {          // the game exited: hooks died with it
        // remember what was active so the same hooks come back on the next launch of the game (auto re-apply)
        pCrit_ = critMode_; pExp_ = exp_.active; pAlways_ = sflAlways_.active; pCards_ = sflDraw_.active; pSym_ = symMode_; pSoc_ = soc_.active;
        pending_ = autoApply && (pCrit_ || pExp_ || pAlways_ || pCards_ || pSym_ || pSoc_);
        crit_ = HookState(); exp_ = HookState(); sflAlways_ = HookState(); sflLevel_ = HookState(); sflForce_ = HookState();
        sflDraw_ = HookState(); sflDeal_ = HookState(); sflCount_ = HookState(); sym_ = HookState(); symMode_ = 0; soc_ = HookState();
        critMode_ = 0; cacheOk_ = false;
        lastEvent = "P4G.exe closed";
    }
    if (!mem_->attached()) return;
    if (!was) { nextApply_ = now + 1.5; applyTries_ = 0; }   // give a freshly started game a moment before patching its code
    if (pending_ && now >= nextApply_) applyPending(now);
    if (now - lastRefresh_ >= 0.1) { lastRefresh_ = now; refresh(); }
    if (now - lastEnforce_ >= 0.1) { lastEnforce_ = now; enforce(); }
}

void Game::enforce() {
    // party buffs: [durAtk, dirAtk, durDef, dirDef, dirAgi, durAgi, flags]  (same values as the cheat table)
    static const uint8_t BUFF[9][7] = {
        {0,0,0,0,0,0,0}, {51,17,0,0,0,0,3}, {0,0,48,16,0,0,8}, {0,0,3,1,1,3,20}, {51,17,48,16,0,0,11},
        {51,17,3,1,1,3,23}, {0,0,51,17,1,3,28}, {51,17,51,17,1,3,31}, {0,0,0,0,0,0,0} };
    static const uint8_t CHARGE[12][2] = {
        {0,0}, {2,0}, {4,0}, {8,0}, {16,0}, {0,1}, {6,0}, {24,0}, {6,1}, {30,0}, {30,1}, {0,0} };
    for (int m = 0; m < 8; m++) {
        if (m == 4) continue;                 // Rise does not fight
        uint64_t u = unit(m);
        int b = buffMode[m];
        if (b >= 1 && b <= 8) {
            const uint8_t* c = BUFF[b];
            w8(u + 0x1C, c[1]); w8(u + 0x1D, c[3]); w8(u + 0x1E, c[4]);
            w8(u + 0x25, c[0]); w8(u + 0x26, c[2]); w8(u + 0x27, c[5]);
            w8(u + 0x14, c[6]);
        }
        int ch = chargeMode[m];
        if (ch >= 1 && ch <= 11) { w8(u + 0x16, CHARGE[ch][0]); w8(u + 0x17, CHARGE[ch][1]); }
    }
    // money and Social Link point freezes
    if (freezeMoney >= 0 && r32(SAVE) != (uint32_t)freezeMoney) w32(SAVE, (uint32_t)freezeMoney);
    for (int i = 0; i < 23; i++)
        if (freezeSlink[i] >= 0 && r16(SLINK + i * 16 + 4) != (uint16_t)freezeSlink[i]) w16(SLINK + i * 16 + 4, (uint16_t)freezeSlink[i]);
    // HP / SP freezes (value captured when the freeze was enabled)
    for (int m = 0; m < 8; m++) {
        uint64_t u = unit(m);
        if (freezeHp[m] >= 0 && r16(u + 8) != (uint16_t)freezeHp[m]) w16(u + 8, (uint16_t)freezeHp[m]);
        if (freezeSp[m] >= 0 && r16(u + 0xA) != (uint16_t)freezeSp[m]) w16(u + 0xA, (uint16_t)freezeSp[m]);
    }
    // enemy debuffs: every unit shares the buff layout seen in the crit code (P4G.exe+D503E): one signed nibble per stat,
    // 1..7 = +stages, 0xE = -1; +1C phys|mag attack, +1D hit|defense, +1E evasion|crit, durations +25..27, active bits +14.
    if (enemyDebuff) {
        uint64_t btl = mem_->rd<uint64_t>(mem_->moduleBase() + BTL_PTR_OFF);
        // the battle work (0x1670 bytes) is allocated at battle start and nulled at exit (P4G.exe+4740AD); bit 7 of [btl+C]
        // is set once the outcome is decided (P4G.exe+5F2D5): from then on (result screen, Shuffle Time) the enemy units are
        // being torn down, so nothing may be written to them any more.
        if (btl && !(mem_->rd<uint8_t>(btl + 0xC) & 0x80)) {
            // [btl+1D0] (next +CF8) is the party list; the enemy list is [btl+1D8], chained through +D00 (P4G.exe+B232B, FB0D8)
            uint64_t node = mem_->rd<uint64_t>(btl + 0x1D8);
            for (int n = 0; node && n < 16; n++) {
                uint64_t u = mem_->rd<uint64_t>(node + 0xCF0);
                bool active = (mem_->rd<uint8_t>(node + 0x9C) & 8) != 0;          // node in use (P4G.exe+38623)
                if (active && u && !(mem_->rd<uint32_t>(u + 0xC) & 0x80000) && mem_->rd<uint16_t>(u + 8) > 0) {
                    uint8_t b[0x14]; if (!mem_->read(u + 0x14, b, sizeof b)) break;
                    uint8_t n1C = b[0x08], n1D = b[0x09], n1E = b[0x0A], d25 = b[0x11], d26 = b[0x12], d27 = b[0x13], fl = b[0];
                    if (enemyDebuff & 1) { n1C = 0xEE; d25 = 0xFF; fl |= 0x03; }
                    if (enemyDebuff & 2) { n1D = (n1D & 0x0F) | 0xE0; d26 = (d26 & 0x0F) | 0xF0; fl |= 0x08; }
                    if (enemyDebuff & 4) { n1D = (n1D & 0xF0) | 0x0E; d26 = (d26 & 0xF0) | 0x0F; n1E = (n1E & 0xF0) | 0x0E; d27 = (d27 & 0xF0) | 0x0F; fl |= 0x14; }
                    if (n1C != b[0x08]) mem_->wr<uint8_t>(u + 0x1C, n1C);
                    if (n1D != b[0x09]) mem_->wr<uint8_t>(u + 0x1D, n1D);
                    if (n1E != b[0x0A]) mem_->wr<uint8_t>(u + 0x1E, n1E);
                    if (d25 != b[0x11]) mem_->wr<uint8_t>(u + 0x25, d25);
                    if (d26 != b[0x12]) mem_->wr<uint8_t>(u + 0x26, d26);
                    if (d27 != b[0x13]) mem_->wr<uint8_t>(u + 0x27, d27);
                    if (fl != b[0]) mem_->wr<uint8_t>(u + 0x14, fl);
                }
                node = mem_->rd<uint64_t>(node + 0xD00);
            }
        }
    }
    // stay daytime: undo the first Evening of the day (mode 1) or every Evening (mode 2)
    int day = (int16_t)r16(TIME), tod = r8(TIME + 2);
    if (day != lastDay_) revertedDay_ = -1;
    if (stayDaytime && tod == 5 && lastTod_ >= 0 && lastTod_ <= 4 && lastDay_ == day && (stayDaytime == 2 || revertedDay_ != day)) {
        w8(TIME + 2, 4); revertedDay_ = day; tod = 4;
        char buf[96]; snprintf(buf, sizeof buf, "Stay daytime: Evening reverted to After School (day %d)", day); lastEvent = buf;
    }
    lastTod_ = tod; lastDay_ = day;
    // item freezes
    for (size_t c = 0; c < ITEM_CAT_COUNT && c < freezeCat.size(); c++) {
        if (freezeCat[c] < 0) continue;
        uint8_t q = (uint8_t)freezeCat[c];
        for (size_t i = 0; i < ITEM_CATS[c].count; i++)
            if (r8(ITEM_CATS[c].items[i].addr) != q) w8(ITEM_CATS[c].items[i].addr, q);
    }
    // persona affinity overrides: the table is reloaded from disk by the game, so keep writing them
    if (!affOverrides.empty()) {
        uint64_t tbl = affinityTable();
        if (tbl) for (auto& o : affOverrides) {
            uint64_t a = tbl + (uint64_t)o.pid * 32 + (uint64_t)o.slot * 2;
            uint16_t cur;
            if (mem_->read(a, &cur, 2) && cur != o.value) mem_->wr<uint16_t>(a, o.value);
        }
    }
}

// ------------------------------------------------------------------ hooks
static int64_t findPattern(IMemory& m, uint64_t start, uint64_t size, const uint8_t* pat, size_t n) {
    std::vector<uint8_t> buf(1 << 20);
    int64_t found = -1; int hits = 0;
    for (uint64_t off = 0; off < size; off += buf.size() - n) {
        size_t want = (size_t)std::min<uint64_t>(buf.size(), size - off);
        if (!m.read(start + off, buf.data(), want)) break;
        for (size_t i = 0; i + n <= want; i++) {
            if (buf[i] == pat[0] && memcmp(&buf[i], pat, n) == 0) { hits++; if (found < 0) found = (int64_t)(start + off + i); }
        }
        if (want < buf.size()) break;
    }
    return hits == 1 ? found : -1;
}

static std::string hexBytes(const uint8_t* b, size_t n) {
    std::string s; char t[4];
    for (size_t i = 0; i < n; i++) { snprintf(t, sizeof t, "%02X ", b[i]); s += t; }
    return s;
}

// Locates one hook site: the known offset first, then a unique-pattern scan of the code section.
// A site is looked up again on every attempt until it is found, so a site released by another tool
// (a Cheat Engine script turned off) becomes usable without restarting the trainer.
bool Game::locateSite(HookState& h, uint64_t off, const uint8_t* pat, size_t n, size_t show, const char* name) {
    if (h.site) return true;
    uint64_t base = mem_->moduleBase();
    uint8_t b[32];
    char addr[32]; snprintf(addr, sizeof addr, "%llX", (unsigned long long)(base + off));
    std::string diag;
    if (!mem_->read(base + off, b, n)) diag = std::string(name) + " hook site unreadable at " + addr;
    else if (memcmp(b, pat, n) == 0) { h.site = base + off; hookError.clear(); return true; }
    else diag = std::string(name) + " hook site at " + addr + " reads " + hexBytes(b, show) +
                (b[0] == 0xE9 ? "= already hooked by another tool (Cheat Engine script still enabled? turn it off or restart the game)"
                              : "instead of " + hexBytes(pat, show) + "(different game build?)");
    int64_t f = findPattern(*mem_, base + CODE_START, CODE_SIZE, pat, n);
    if (f > 0) { h.site = (uint64_t)f; hookError.clear(); return true; }
    hookError = diag;
    return false;
}

static void putRel32(std::vector<uint8_t>& v, size_t at, uint64_t from, uint64_t to) {
    int64_t d = (int64_t)to - (int64_t)from;
    int32_t r = (int32_t)d;
    memcpy(&v[at], &r, 4);
}
static void putU64(std::vector<uint8_t>& v, size_t at, uint64_t x) { memcpy(&v[at], &x, 8); }
static void putU32(std::vector<uint8_t>& v, size_t at, uint32_t x) { memcpy(&v[at], &x, 4); }

// crit cave: attacker struct pointer is in rsi; original: cmp r8d,r14d / jl <crit> ; crit = site + 0x12 (mov eax,2)
std::vector<uint8_t> Game::buildCritCave(uint64_t cave, bool party) {
    uint64_t site = crit_.site, crit = site + 0x12, ret = site + 5;
    std::vector<uint8_t> v;
    if (party) {
        v = { 0x50,                                     // push rax
              0x48,0xB8, 0,0,0,0,0,0,0,0,               // mov rax, PARTY
              0x48,0x3B,0xF0,                           // cmp rsi,rax
              0x72, 0,                                  // jb notParty
              0x48,0xB8, 0,0,0,0,0,0,0,0,               // mov rax, PARTY + 8*0x84
              0x48,0x3B,0xF0,                           // cmp rsi,rax
              0x73, 0,                                  // jae notParty
              0x58,                                     // pop rax
              0xE9, 0,0,0,0,                            // jmp crit
              // notParty (37):
              0x58,                                     // pop rax
              0x45,0x3B,0xC6,                           // cmp r8d,r14d
              0x0F,0x8C, 0,0,0,0,                       // jl crit
              0xE9, 0,0,0,0 };                          // jmp ret
        putU64(v, 3, PARTY); v[15] = (uint8_t)(37 - 16); putU64(v, 18, PARTY + 8 * STRIDE); v[30] = (uint8_t)(37 - 31);
        putRel32(v, 33, cave + 37, crit); putRel32(v, 43, cave + 47, crit); putRel32(v, 48, cave + 52, ret);
    } else {
        v = { 0x50,                                     // push rax
              0x48,0xB8, 0,0,0,0,0,0,0,0,               // mov rax, PARTY (MC unit)
              0x48,0x3B,0xF0,                           // cmp rsi,rax
              0x75, 0,                                  // jne notMc
              0x58,                                     // pop rax
              0xE9, 0,0,0,0,                            // jmp crit
              // notMc (22):
              0x58,                                     // pop rax
              0x45,0x3B,0xC6,                           // cmp r8d,r14d
              0x0F,0x8C, 0,0,0,0,                       // jl crit
              0xE9, 0,0,0,0 };                          // jmp ret
        putU64(v, 3, PARTY); v[15] = (uint8_t)(22 - 16);
        putRel32(v, 18, cave + 22, crit); putRel32(v, 28, cave + 32, crit); putRel32(v, 33, cave + 37, ret);
    }
    return v;
}

// EXP cave: edi = base gain, r8d = gain after equipment; original site = test edi,edi / jle site+27 / mov eax,1869F (9 bytes)
std::vector<uint8_t> Game::buildExpCave(uint64_t cave) {
    uint64_t site = exp_.site;
    std::vector<uint8_t> v = {
        0x85,0xFF,                          // 0  test edi,edi
        0x7E, 0,                            // 2  jle retLow
        0x45,0x85,0xC0,                     // 4  test r8d,r8d
        0x7E, 0,                            // 7  jle noMult
        0x8B,0x05, 0,0,0,0,                 // 9  mov eax,[rip+expMult]
        0x83,0xF8,0x01,                     // 15 cmp eax,1
        0x7E, 0,                            // 18 jle noMult
        0x41,0x81,0xF8, 0x9F,0x86,0x01,0x00,// 20 cmp r8d,99999
        0x7E, 0,                            // 27 jle capped
        0x41,0xB8, 0x9F,0x86,0x01,0x00,     // 29 mov r8d,99999
        0x44,0x0F,0xAF,0xC0,                // 35 capped: imul r8d,eax
        0xB8, 0x9F,0x86,0x01,0x00,          // 39 noMult: mov eax,99999
        0xE9, 0,0,0,0,                      // 44 jmp site+9
        0xE9, 0,0,0,0,                      // 49 retLow: jmp site+27
        0x90,0x90,                          // 54 pad
        0,0,0,0 };                          // 56 expMult (dword)
    v[3] = (uint8_t)(49 - 4); v[8] = (uint8_t)(39 - 9); putRel32(v, 11, cave + 15, cave + 56);
    v[19] = (uint8_t)(39 - 20); v[28] = (uint8_t)(35 - 29);
    putRel32(v, 45, cave + 49, site + 9); putRel32(v, 50, cave + 54, site + 0x27);
    putU32(v, 56, (uint32_t)expMult_);
    return v;
}

bool Game::installHook(HookState& h, const std::vector<uint8_t>& cave, size_t siteLen) {
    if (!mem_->read(h.site, h.orig, siteLen)) { hookError = "Cannot read the hook site"; return false; }
    h.len = siteLen;
    if (!h.cave) h.cave = mem_->allocExec(h.site, 0x1000);
    if (!h.cave) { hookError = "Cannot allocate a code cave near the game code"; return false; }
    int64_t d = (int64_t)h.cave - (int64_t)(h.site + 5);
    if (d > 0x7FFFFFFFLL || d < -0x80000000LL) { hookError = "Code cave out of jmp range"; return false; }
    if (!mem_->write(h.cave, cave.data(), cave.size())) { hookError = "Cannot write the code cave"; return false; }
    if (!mem_->protectExec(h.cave, 0x1000)) { hookError = "Cannot make the code cave executable"; return false; }
    std::vector<uint8_t> jmp(siteLen, 0x90);
    jmp[0] = 0xE9; putRel32(jmp, 1, h.site + 5, h.cave);
    if (!mem_->writeCode(h.site, jmp.data(), jmp.size())) { hookError = "Cannot patch the game code"; return false; }
    h.active = true; hookError.clear();
    return true;
}

void Game::removeHook(HookState& h) {
    if (h.active && mem_->attached()) mem_->writeCode(h.site, h.orig, h.len);
    h.active = false;
    if (h.cave && mem_->attached()) mem_->freeMem(h.cave);
    h.cave = 0;
}

void Game::detachHooks() {
    removeHook(crit_); removeHook(exp_); removeHook(sflAlways_); removeHook(sflLevel_); removeHook(sflForce_);
    removeHook(sflDraw_); removeHook(sflDeal_); removeHook(sflCount_); removeHook(sym_); removeHook(soc_); critMode_ = 0; symMode_ = 0;
}

bool Game::installPatch(HookState& h, uint64_t addr, const uint8_t* bytes, size_t n) {
    if (!mem_->read(addr, h.orig, n)) { hookError = "Cannot read the patch site"; return false; }
    h.len = n;
    if (!mem_->writeCode(addr, bytes, n)) { hookError = "Cannot patch the game code"; return false; }
    h.active = true; hookError.clear();
    return true;
}

bool Game::setCritMode(int mode) {
    if (!mem_->attached()) return false;
    if (mode != 0 && !locateSite(crit_, CRIT_SITE_OFF, CRIT_PATTERN, 5, 5, "Crit")) return false;
    removeHook(crit_);
    critMode_ = 0;
    if (mode == 0) return true;
    uint64_t cave = mem_->allocExec(crit_.site, 0x1000);
    if (!cave) { hookError = "Cannot allocate a code cave near the game code"; return false; }
    crit_.cave = cave;
    if (!installHook(crit_, buildCritCave(cave, mode == 2), 5)) { removeHook(crit_); return false; }
    critMode_ = mode;
    lastEvent = mode == 2 ? "100% crit: whole party" : "100% crit: MC only";
    return true;
}

bool Game::setExpEnabled(bool on) {
    if (!mem_->attached()) return false;
    if (!on) { removeHook(exp_); lastEvent = "EXP multiplier off"; return true; }
    if (exp_.active) return true;
    if (!locateSite(exp_, EXP_SITE_OFF, EXP_PATTERN, 28, 9, "EXP")) return false;
    uint64_t cave = mem_->allocExec(exp_.site, 0x1000);
    if (!cave) { hookError = "Cannot allocate a code cave near the game code"; return false; }
    exp_.cave = cave;
    if (!installHook(exp_, buildExpCave(cave), 9)) { removeHook(exp_); return false; }
    char buf[64]; snprintf(buf, sizeof buf, "EXP multiplier x%d on", expMult_); lastEvent = buf;
    return true;
}

void Game::setExpMult(int m) {
    expMult_ = m < 1 ? 1 : m;
    uint32_t v = (uint32_t)expMult_;
    if (exp_.active) mem_->writeCode(exp_.cave + 56, &v, sizeof v);   // the cave is RX after install
}

// ------------------------------------------------------------------ Shuffle Time
// Battle end (P4G.exe+B45C3): chance = 30 + 20 (All-Out finish) + 10 x pity counter, then rand%100 < chance.
// The 2-byte patch removes the "jae skip" so the roll always passes; bosses and no-shuffle encounters are still excluded before it.
bool Game::setShuffleAlways(bool on) {
    if (!mem_->attached()) return false;
    if (!on) { removeHook(sflAlways_); sflAlways_ = HookState(); lastEvent = "Shuffle Time every battle: off"; return true; }
    if (sflAlways_.active) return true;
    if (!locateSite(sflAlways_, SFL_ALWAYS_OFF, SFL_ALWAYS_PATTERN, 8, 8, "Shuffle")) return false;
    static const uint8_t NOPS[2] = {0x90, 0x90};
    HookState tmp = sflAlways_; tmp.site = sflAlways_.site + 3;       // the jae itself
    if (!installPatch(tmp, tmp.site, NOPS, 2)) { sflAlways_ = HookState(); return false; }
    sflAlways_ = tmp;
    lastEvent = "Shuffle Time every battle: on";
    return true;
}

// level cave: mov eax,<lvl> / mov [rbp+1CF90],eax / jmp back   (eax is overwritten by the next original instruction)
std::vector<uint8_t> Game::buildLevelCave(uint64_t cave) {
    std::vector<uint8_t> v = {0xB8, 0,0,0,0, 0x89,0x85,0x90,0xCF,0x01,0x00, 0xE9, 0,0,0,0};
    putU32(v, 1, (uint32_t)sflLevelVal_);
    putRel32(v, 12, cave + 16, sflLevel_.site + 7);
    return v;
}

bool Game::setShuffleLevelForced(bool on) {
    if (!mem_->attached()) return false;
    if (!on) { removeHook(sflLevel_); lastEvent = "Shuffle level: game roll"; return true; }
    if (sflLevel_.active) return true;
    if (!locateSite(sflLevel_, SFL_LEVEL_OFF, SFL_LEVEL_PATTERN, 12, 7, "Shuffle level")) return false;
    uint64_t cave = mem_->allocExec(sflLevel_.site, 0x1000);
    if (!cave) { hookError = "Cannot allocate a code cave near the game code"; return false; }
    sflLevel_.cave = cave;
    if (!installHook(sflLevel_, buildLevelCave(cave), 7)) { removeHook(sflLevel_); return false; }
    char buf[64]; snprintf(buf, sizeof buf, "Shuffle level forced to %d", sflLevelVal_); lastEvent = buf;
    return true;
}

void Game::setShuffleLevel(int lvl) {
    sflLevelVal_ = lvl < 0 ? 0 : (lvl > 7 ? 7 : lvl);
    uint32_t v = (uint32_t)sflLevelVal_;
    if (sflLevel_.active) mem_->writeCode(sflLevel_.cave + 1, &v, sizeof v);
}

// force cave: rewrites the persona candidate list (up to 4 ids at [r12], count at [r12+8]) with the ids stored at cave+80;
// zero ids are skipped, and with no id set the game's own list is kept.
std::vector<uint8_t> Game::buildForceCave(uint64_t cave) {
    std::vector<uint8_t> v = {
        0x48,0xBA, 0,0,0,0,0,0,0,0,                                  // mov rdx, cave+80
        0x31,0xC9,                                                   // xor ecx,ecx
        0x0F,0xB7,0x02, 0x66,0x85,0xC0, 0x74,0x07, 0x66,0x41,0x89,0x04,0x4C, 0xFF,0xC1,       // slot 0
        0x0F,0xB7,0x42,0x02, 0x66,0x85,0xC0, 0x74,0x07, 0x66,0x41,0x89,0x04,0x4C, 0xFF,0xC1,  // slot 1
        0x0F,0xB7,0x42,0x04, 0x66,0x85,0xC0, 0x74,0x07, 0x66,0x41,0x89,0x04,0x4C, 0xFF,0xC1,  // slot 2
        0x0F,0xB7,0x42,0x06, 0x66,0x85,0xC0, 0x74,0x07, 0x66,0x41,0x89,0x04,0x4C, 0xFF,0xC1,  // slot 3
        0x85,0xC9, 0x74,0x02, 0x89,0xCD,                             // test ecx,ecx / je orig / mov ebp,ecx
        0x85,0xED, 0x74,0x0A,                                        // orig: test ebp,ebp / je none
        0x41,0x89,0x6C,0x24,0x08,                                    // mov [r12+8],ebp
        0xE9, 0,0,0,0,                                               // jmp site+9
        0xE9, 0,0,0,0 };                                             // none: jmp site+F (xor eax,eax: no Shuffle Time)
    putU64(v, 2, cave + 0x80);
    putRel32(v, 91, cave + 95, sflForce_.site + 9);
    putRel32(v, 96, cave + 100, sflForce_.site + 0xF);
    v.resize(0x80, 0);
    for (int i = 0; i < 4; i++) { uint16_t id = forcedPersona[i]; v.push_back((uint8_t)id); v.push_back((uint8_t)(id >> 8)); }
    return v;
}

bool Game::setForcePersonas(bool on) {
    if (!mem_->attached()) return false;
    if (!on) { removeHook(sflForce_); lastEvent = "Forced Shuffle Time personas: off"; return true; }
    if (sflForce_.active) return true;
    if (!locateSite(sflForce_, SFL_FORCE_OFF, SFL_FORCE_PATTERN, 15, 9, "Shuffle persona")) return false;
    uint64_t cave = mem_->allocExec(sflForce_.site, 0x1000);
    if (!cave) { hookError = "Cannot allocate a code cave near the game code"; return false; }
    sflForce_.cave = cave;
    if (!installHook(sflForce_, buildForceCave(cave), 9)) { removeHook(sflForce_); return false; }
    lastEvent = "Forced Shuffle Time personas: on";
    return true;
}

void Game::syncForcedPersonas() {
    if (!sflForce_.active) return;
    uint8_t b[8];
    for (int i = 0; i < 4; i++) { b[i * 2] = (uint8_t)forcedPersona[i]; b[i * 2 + 1] = (uint8_t)(forcedPersona[i] >> 8); }
    mem_->writeCode(sflForce_.cave + 0x80, b, 8);
}

// draw cave (entry of drawCard): for kind 0, slot = cards dealt so far; the first draw of a slot returns the forced id,
// a redraw of the same slot (duplicate suit / arcana, no-Sword dungeon) falls back to the game's draw so it can never loop.
// data at cave+80: 6 words (id+1, 0 = game's draw), dword at cave+90: last slot served.
std::vector<uint8_t> Game::buildDrawCave(uint64_t cave) {
    std::vector<uint8_t> v = {
        0x80,0xFA,0x00, 0x75,0x2D,                                   // cmp dl,0 / jne orig
        0x48,0xB8, 0,0,0,0,0,0,0,0,                                  // mov rax, cave+80
        0x44,0x8B,0x81,0xB8,0xC8,0x01,0x00,                          // mov r8d,[rcx+1C8B8]
        0x41,0x83,0xF8,0x06, 0x73,0x16,                              // cmp r8d,6 / jae orig
        0x44,0x3B,0x40,0x10, 0x74,0x10,                              // cmp r8d,[rax+10] / je orig
        0x44,0x89,0x40,0x10,                                         // mov [rax+10],r8d
        0x42,0x0F,0xB7,0x04,0x40,                                    // movzx eax,word [rax+r8*2]
        0x85,0xC0, 0x74,0x03,                                        // test eax,eax / je orig
        0xFF,0xC8, 0xC3,                                             // dec eax / ret
        0x48,0x89,0x5C,0x24,0x08, 0x48,0x89,0x6C,0x24,0x10,          // orig: the two prologue stores
        0xE9, 0,0,0,0 };                                             // jmp site+A
    putU64(v, 7, cave + 0x1000);              // data lives in the second, writable page: the game itself writes the last slot
    putRel32(v, 61, cave + 65, sflDraw_.site + 10);
    return v;
}

// deal cave (start of the deal loop, once per Shuffle Time): reset the last slot, then the original test/jle
std::vector<uint8_t> Game::buildDealCave(uint64_t cave, uint64_t data) {
    std::vector<uint8_t> v = {
        0x48,0xB8, 0,0,0,0,0,0,0,0,                                  // mov rax, data
        0xC7,0x00,0xFF,0xFF,0xFF,0xFF,                               // mov dword [rax],-1
        0x85,0xDB,                                                   // test ebx,ebx
        0x0F,0x8E, 0,0,0,0,                                          // jle site+16
        0xE9, 0,0,0,0 };                                             // jmp site+9
    putU64(v, 2, data);
    putRel32(v, 20, cave + 24, sflDeal_.site + 0x16);
    putRel32(v, 25, cave + 29, sflDeal_.site + 9);
    return v;
}

// count cave: the row's card count, overridden by the dword at data when it is not 0 (rcx is dead after the original instruction)
std::vector<uint8_t> Game::buildCountCave(uint64_t cave, uint64_t data) {
    std::vector<uint8_t> v = {
        0x8B,0x9C,0x8E,0x54,0xBB,0x01,0x00,                          // mov ebx,[rsi+rcx*4+1BB54]  (original)
        0x48,0xB9, 0,0,0,0,0,0,0,0,                                  // mov rcx, data
        0x8B,0x09, 0x85,0xC9, 0x74,0x02, 0x89,0xCB,                  // mov ecx,[rcx] / test ecx,ecx / je skip / mov ebx,ecx
        0xE9, 0,0,0,0 };                                             // skip: jmp site+7
    putU64(v, 9, data);
    putRel32(v, 26, cave + 30, sflCount_.site + 7);
    return v;
}

// The draw cave is a 2-page allocation: code in the first page (made RX by installHook), data in the second page, which stays
// writable because the game's own code writes there (last slot served) on every draw. +0: 6 card words, +10: last slot, +14: count.
bool Game::setForceCards(bool on) {
    if (!mem_->attached()) return false;
    if (!on) { removeHook(sflCount_); removeHook(sflDeal_); removeHook(sflDraw_); lastEvent = "Forced Shuffle Time cards: off"; return true; }
    if (sflDraw_.active) return true;
    if (!locateSite(sflDraw_, SFL_DRAW_OFF, SFL_DRAW_PATTERN, 26, 10, "Card draw")) return false;
    if (!locateSite(sflDeal_, SFL_DEAL_OFF, SFL_DEAL_PATTERN, 13, 9, "Card deal")) return false;
    if (!locateSite(sflCount_, SFL_COUNT_OFF, SFL_COUNT_PATTERN, 13, 7, "Card count")) return false;
    uint64_t cave = mem_->allocExec(sflDraw_.site, 0x2000);
    if (!cave) { hookError = "Cannot allocate a code cave near the game code"; return false; }
    sflDraw_.cave = cave;
    uint8_t data[0x18] = {0};
    for (int i = 0; i < 6; i++) { data[i * 2] = (uint8_t)forceCards[i]; data[i * 2 + 1] = (uint8_t)(forceCards[i] >> 8); }
    data[0x10] = data[0x11] = data[0x12] = data[0x13] = 0xFF;                         // last slot = -1
    data[0x14] = (uint8_t)(forceCount < 0 ? 0 : (forceCount > 6 ? 6 : forceCount));  // cards dealt (0 = game's choice)
    if (!mem_->write(cave + 0x1000, data, sizeof data)) { hookError = "Cannot write the code cave"; removeHook(sflDraw_); return false; }
    if (!installHook(sflDraw_, buildDrawCave(cave), 10)) { removeHook(sflDraw_); return false; }
    uint64_t cave2 = mem_->allocExec(sflDeal_.site, 0x1000);
    if (!cave2) { hookError = "Cannot allocate a code cave near the game code"; removeHook(sflDraw_); return false; }
    sflDeal_.cave = cave2;
    if (!installHook(sflDeal_, buildDealCave(cave2, cave + 0x1010), 9)) { removeHook(sflDeal_); removeHook(sflDraw_); return false; }
    uint64_t cave3 = mem_->allocExec(sflCount_.site, 0x1000);
    if (!cave3) { hookError = "Cannot allocate a code cave near the game code"; removeHook(sflDeal_); removeHook(sflDraw_); return false; }
    sflCount_.cave = cave3;
    if (!installHook(sflCount_, buildCountCave(cave3, cave + 0x1014), 7)) { removeHook(sflCount_); removeHook(sflDeal_); removeHook(sflDraw_); return false; }
    lastEvent = "Forced Shuffle Time cards: on";
    return true;
}

void Game::syncForceCards() {
    if (!sflDraw_.active) return;
    uint8_t b[12];
    for (int i = 0; i < 6; i++) { b[i * 2] = (uint8_t)forceCards[i]; b[i * 2 + 1] = (uint8_t)(forceCards[i] >> 8); }
    mem_->write(sflDraw_.cave + 0x1000, b, 12);
    uint32_t cnt = (uint32_t)(forceCount < 0 ? 0 : (forceCount > 6 ? 6 : forceCount));
    mem_->write(sflDraw_.cave + 0x1014, &cnt, 4);
}

// ------------------------------------------------------------------ social stat gain multiplier
// cave: r8d = mult * gain (ebx) ; add eax,r8d ; cmp eax,ecx ; cmovg eax,ecx (the game's 999 cap) ; jmp back. r8 is dead here.
std::vector<uint8_t> Game::buildSocialCave(uint64_t cave) {
    std::vector<uint8_t> v = { 0x41,0xB8, 0,0,0,0, 0x44,0x0F,0xAF,0xC3, 0x44,0x01,0xC0, 0x39,0xC8, 0x0F,0x4F,0xC1, 0xE9, 0,0,0,0 };
    putU32(v, 2, (uint32_t)socMult_);
    putRel32(v, 19, cave + 23, soc_.site + 7);
    return v;
}

bool Game::setSocialMultEnabled(bool on) {
    if (!mem_->attached()) return false;
    if (!on) { removeHook(soc_); lastEvent = "Social stat multiplier off"; return true; }
    if (soc_.active) return true;
    if (!locateSite(soc_, SOC_ADD_OFF, SOC_ADD_PATTERN, 15, 7, "Social stat")) return false;
    uint64_t cave = mem_->allocExec(soc_.site, 0x1000);
    if (!cave) { hookError = "Cannot allocate a code cave near the game code"; return false; }
    soc_.cave = cave;
    if (!installHook(soc_, buildSocialCave(cave), 7)) { removeHook(soc_); return false; }
    char buf[64]; snprintf(buf, sizeof buf, "Social stat multiplier x%d on", socMult_); lastEvent = buf;
    return true;
}

void Game::setSocialMult(int m) {
    socMult_ = m < 1 ? 1 : m;
    uint32_t v = (uint32_t)socMult_;
    if (soc_.active) mem_->writeCode(soc_.cave + 2, &v, sizeof v);
}

// ------------------------------------------------------------------ dungeon shadow symbols
// Encounter picker (P4G.exe+2C9D00): per floor, 30 weighted slots split in three groups - 0..19 normal symbols (type 1),
// 20..24 (type 2, es002), 25..29 (type 4, es003). The hook forces the group; when the floor has no entry in that group
// (all weights 0) it falls back to the normal one so the picker never returns "no encounter". Mode byte at cave+100.
std::vector<uint8_t> Game::buildSymbolCave(uint64_t cave) {
    std::vector<uint8_t> v = {
        0x48,0xB8, 0,0,0,0,0,0,0,0,                                  // mov rax, cave+100
        0x44,0x0F,0xB6,0x10, 0x45,0x85,0xD2, 0x74,0x76,              // movzx r10d,[rax] / test / je orig
        0x4C,0x8B,0x4C,0x24,0x28, 0x4D,0x6B,0xC9,0x3F,               // r9 = floor row * 3F
        0x4C,0x8B,0x5C,0x24,0x30, 0x4D,0x6B,0xDB,0x1E, 0x4D,0x01,0xD9,   // + variant * 1E
        0x41,0x83,0xFA,0x02, 0x75,0x13,                              // mode 2 ?
        0xBF,0x14,0,0,0, 0x41,0xBC,0x19,0,0,0, 0x41,0xBF,0x02,0,0,0, 0xEB,0x11,   // edi=20 r12d=25 r15d=2
        0xBF,0x19,0,0,0, 0x41,0xBC,0x1E,0,0,0, 0x41,0xBF,0x04,0,0,0,             // edi=25 r12d=30 r15d=4
        0x45,0x31,0xDB, 0x41,0x89,0xFA,                              // sum: r11d=0, r10d=edi
        0x4B,0x8D,0x04,0x11, 0x66,0x41,0x83,0x7C,0x80,0x0C,0x00, 0x74,0x09,      // loop: slot id == 0 ? skip
        0x41,0x0F,0xB7,0x44,0x80,0x0E, 0x41,0x01,0xC3,               // r11d += weight
        0x41,0xFF,0xC2, 0x45,0x39,0xE2, 0x7C,0xE2,                   // next slot
        0x45,0x85,0xDB, 0x75,0x0E,                                   // any weight ? keep the group
        0x31,0xFF, 0x41,0xBC,0x14,0,0,0, 0x41,0xBF,0x01,0,0,0,       // else normal group
        0x44,0x0F,0x28,0x44,0x24,0x40,                               // orig: movaps xmm8,[rsp+40]
        0xE9, 0,0,0,0 };                                             // jmp site+6
    putU64(v, 2, cave + 0x100);
    putRel32(v, 144, cave + 148, sym_.site + 6);
    v.resize(0x100, 0);
    v.push_back((uint8_t)symMode_);
    return v;
}

bool Game::setSymbolMode(int mode) {
    if (!mem_->attached()) return false;
    symMode_ = (mode == 2 || mode == 3) ? mode : 0;
    if (symMode_ == 0) { removeHook(sym_); lastEvent = "Shadow symbols: game's choice"; return true; }
    if (sym_.active) { uint8_t m = (uint8_t)symMode_; mem_->writeCode(sym_.cave + 0x100, &m, 1); }
    else {
        if (!locateSite(sym_, SYM_PICK_OFF, SYM_PICK_PATTERN, 13, 6, "Shadow symbol")) { symMode_ = 0; return false; }
        uint64_t cave = mem_->allocExec(sym_.site, 0x1000);
        if (!cave) { hookError = "Cannot allocate a code cave near the game code"; symMode_ = 0; return false; }
        sym_.cave = cave;
        if (!installHook(sym_, buildSymbolCave(cave), 6)) { removeHook(sym_); symMode_ = 0; return false; }
    }
    char buf[64]; snprintf(buf, sizeof buf, "Shadow symbols: group %d only", symMode_); lastEvent = buf;
    return true;
}

Game::ShuffleInfo Game::shuffleInfo() {
    ShuffleInfo s;
    uint64_t btl = mem_->rd<uint64_t>(mem_->moduleBase() + BTL_PTR_OFF);
    if (!btl) return s;
    uint8_t b[0x58];
    if (!mem_->read(btl + 0x12A0, b, sizeof b)) return s;
    s.ok = true;
    s.triggered = (b[0] & 1) != 0;
    for (int i = 0; i < 4; i++) s.ids[i] = (uint16_t)(b[0x40 + i * 2] | (b[0x41 + i * 2] << 8));
    memcpy(&s.count, b + 0x48, 4); memcpy(&s.cards, b + 0x4C, 4); memcpy(&s.mode, b + 0x50, 4); memcpy(&s.level, b + 0x54, 4);
    return s;
}

// ------------------------------------------------------------------ configuration file
static FILE* openFile(const std::string& path, const char* mode) {
#ifdef _WIN32
    int n = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    std::wstring w(n, L'\0'); MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &w[0], n);
    std::wstring wm(mode, mode + strlen(mode));
    return _wfopen(w.c_str(), wm.c_str());
#else
    return fopen(path.c_str(), mode);
#endif
}

bool Game::configExists() const {
    if (cfgPath_.empty()) return false;
    FILE* f = openFile(cfgPath_, "rb"); if (!f) return false; fclose(f); return true;
}

bool Game::configAutoApply() const {
    FILE* f = cfgPath_.empty() ? nullptr : openFile(cfgPath_, "rb");
    if (!f) return false;
    bool on = true; char line[256];
    while (fgets(line, sizeof line, f)) if (!strncmp(line, "autoApply=", 10)) on = atoi(line + 10) != 0;
    fclose(f);
    return on;
}

bool Game::storeAutoApply() {
    // rewrite only the autoApply line so the checkbox sticks without touching the other saved options
    if (!configExists()) return false;
    FILE* f = openFile(cfgPath_, "rb"); if (!f) return false;
    std::string out; char line[256]; bool seen = false;
    while (fgets(line, sizeof line, f)) {
        if (!strncmp(line, "autoApply=", 10)) { out += autoApply ? "autoApply=1\n" : "autoApply=0\n"; seen = true; }
        else out += line;
    }
    fclose(f);
    if (!seen) out += autoApply ? "autoApply=1\n" : "autoApply=0\n";
    f = openFile(cfgPath_, "wb"); if (!f) return false;
    fwrite(out.data(), 1, out.size(), f); fclose(f);
    configStatus = autoApply ? "Auto-load enabled" : "Auto-load disabled (the file is kept, use Load to apply it by hand)";
    return true;
}

bool Game::saveConfig() {
    if (cfgPath_.empty()) { configStatus = "No config path"; return false; }
    FILE* f = openFile(cfgPath_, "wb");
    if (!f) { configStatus = "Cannot write " + cfgPath_; return false; }
    fprintf(f, "# Persona 4 Golden trainer - saved options (edit with the trainer, or by hand: key=value)\n");
    fprintf(f, "autoApply=%d\n", autoApply ? 1 : 0);
    fprintf(f, "crit=%d\nexp=%d\nexpMult=%d\n", critMode_, exp_.active ? 1 : 0, expMult_);
    fprintf(f, "shuffleAlways=%d\nforceCards=%d\nforceCount=%d\n", sflAlways_.active ? 1 : 0, sflDraw_.active ? 1 : 0, forceCount);
    for (int i = 0; i < 6; i++) fprintf(f, "card%d=%d\n", i, forceCards[i]);
    fprintf(f, "symbols=%d\nenemyDebuff=%d\nsocialMult=%d\nsocialMultValue=%d\nstayDaytime=%d\n", symMode_, enemyDebuff, soc_.active ? 1 : 0, socMult_, stayDaytime);
    for (int m = 0; m < 8; m++) fprintf(f, "buff%d=%d\ncharge%d=%d\nfreezeHp%d=%d\nfreezeSp%d=%d\n", m, buffMode[m], m, chargeMode[m], m, freezeHp[m], m, freezeSp[m]);
    fprintf(f, "freezeMoney=%lld\n", (long long)freezeMoney);
    for (int i = 0; i < 23; i++) fprintf(f, "freezeSlink%d=%d\n", i, freezeSlink[i]);
    for (size_t c = 0; c < freezeCat.size(); c++) fprintf(f, "freezeCat%zu=%d\n", c, freezeCat[c]);
    fprintf(f, "affinities=%zu\n", affOverrides.size());
    for (size_t i = 0; i < affOverrides.size(); i++)
        fprintf(f, "aff%zu=%u,%u,%u,%u\n", i, affOverrides[i].pid, affOverrides[i].slot, affOverrides[i].value, affOverrides[i].orig);
    fclose(f);
    configStatus = "Saved: " + cfgPath_;
    return true;
}

bool Game::loadConfig() {
    if (cfgPath_.empty()) { configStatus = "No config path"; return false; }
    FILE* f = openFile(cfgPath_, "rb");
    if (!f) { configStatus = "No saved configuration yet"; return false; }
    std::map<std::string, std::string> kv;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        char* eq = strchr(line, '='); if (!eq) continue;
        *eq = 0; kv[line] = eq + 1;
    }
    fclose(f);
    auto get = [&](const std::string& k, long long def) { auto it = kv.find(k); return it == kv.end() ? def : atoll(it->second.c_str()); };
    autoApply = get("autoApply", 1) != 0;
    pCrit_ = (int)get("crit", 0); pExp_ = get("exp", 0) != 0; expMult_ = (int)get("expMult", expMult_);
    pAlways_ = get("shuffleAlways", 0) != 0; pCards_ = get("forceCards", 0) != 0; forceCount = (int)get("forceCount", 0);
    for (int i = 0; i < 6; i++) forceCards[i] = (uint16_t)get("card" + std::to_string(i), 0);
    pSym_ = (int)get("symbols", 0); enemyDebuff = (int)get("enemyDebuff", 0);
    pSoc_ = get("socialMult", 0) != 0; socMult_ = (int)get("socialMultValue", socMult_); stayDaytime = (int)get("stayDaytime", 0);
    for (int m = 0; m < 8; m++) {
        std::string i = std::to_string(m);
        buffMode[m] = (int)get("buff" + i, 0); chargeMode[m] = (int)get("charge" + i, 0);
        freezeHp[m] = (int)get("freezeHp" + i, -1); freezeSp[m] = (int)get("freezeSp" + i, -1);
    }
    freezeMoney = get("freezeMoney", -1);
    for (int i = 0; i < 23; i++) freezeSlink[i] = (int)get("freezeSlink" + std::to_string(i), -1);
    for (size_t c = 0; c < freezeCat.size(); c++) freezeCat[c] = (int)get("freezeCat" + std::to_string(c), -1);
    // affinity overrides: the file replaces the current list; values already written in memory stay as the game's
    // "current" ones, so the originals are restored first when an override is dropped by the file
    if (kv.count("affinities")) {
        std::vector<AffOverride> fresh;
        long long n = get("affinities", 0);
        for (long long i = 0; i < n && i < 4096; i++) {
            auto it = kv.find("aff" + std::to_string(i)); if (it == kv.end()) continue;
            unsigned pid, slot, value, orig;
            if (sscanf(it->second.c_str(), "%u,%u,%u,%u", &pid, &slot, &value, &orig) != 4) continue;
            if (pid > 255 || slot > 15 || value > 0xFFFF || orig > 0xFFFF || value == orig) continue;
            fresh.push_back({(uint16_t)pid, (uint16_t)slot, (uint16_t)value, (uint16_t)orig});
        }
        uint64_t tbl = affinityTable();
        for (auto& o : affOverrides) {                       // overrides dropped by the file go back to the game's value
            bool kept = false;
            for (auto& f2 : fresh) if (f2.pid == o.pid && f2.slot == o.slot) { kept = true; break; }
            if (!kept && tbl) mem_->wr<uint16_t>(tbl + (uint64_t)o.pid * 32 + (uint64_t)o.slot * 2, o.orig);
        }
        affOverrides.swap(fresh);
    }
    pending_ = true; nextApply_ = 0; applyTries_ = 0;       // applied by the next tick (now if attached, else at attach)
    configStatus = mem_->attached() ? "Loaded: " + cfgPath_ : "Loaded: " + cfgPath_ + " (hooks will be installed when the game is attached)";
    return true;
}

void Game::applyPending(double now) {
    pending_ = false;
    int failed = 0;
    if (pCrit_ != critMode_ && !setCritMode(pCrit_)) failed++;
    if (pExp_ != exp_.active && !setExpEnabled(pExp_)) failed++;
    if (pAlways_ != sflAlways_.active && !setShuffleAlways(pAlways_)) failed++;
    if (pCards_ != sflDraw_.active && !setForceCards(pCards_)) failed++;
    else if (pCards_) syncForceCards();
    if (pSym_ != symMode_ && !setSymbolMode(pSym_)) failed++;
    if (pSoc_ != soc_.active && !setSocialMultEnabled(pSoc_)) failed++;
    else if (pSoc_) setSocialMult(socMult_);
    if (exp_.active) setExpMult(expMult_);
    if (failed && applyTries_ < 20) {                    // code not ready yet (game still starting): try again in a few seconds
        applyTries_++; pending_ = true; nextApply_ = now + 3.0;
        configStatus = "Applying the saved options... (" + std::to_string(failed) + " hook(s) not ready, retry " + std::to_string(applyTries_) + "/20: " + hookError + ")";
        return;
    }
    configStatus = failed ? ("Saved options applied, " + std::to_string(failed) + " hook(s) failed: " + hookError) : "Saved options applied";
    lastEvent = configStatus;
}

// ------------------------------------------------------------------ compendium / affinities
int Game::registerMissingPersonas() {
    int added = 0;
    for (size_t i = 0; i < COMP_ROW_COUNT; i++) {
        const CompRow& r = COMP_ROWS[i];
        uint64_t a = compRecord(r.id);
        if (r16(a) & 1) continue;
        w16(a, 1); w16(a + 2, r.id); w16(a + 4, r.level); w32(a + 8, r.exp);
        for (int k = 0; k < 8; k++) w16(a + 0xC + k * 2, r.skills[k]);
        w8(a + 0x1C, r.st); w8(a + 0x1D, r.ma); w8(a + 0x1E, r.en); w8(a + 0x1F, r.ag); w8(a + 0x20, r.lu);
        added++;
    }
    char buf[96]; snprintf(buf, sizeof buf, "Compendium: %d personas registered at base level", added); lastEvent = buf;
    return added;
}

uint64_t Game::affinityTable() {
    if (!mem_->attached()) return 0;
    uint64_t p = r64(mem_->moduleBase() + AFF_PTR_OFF);
    return (p > 0x10000 && p < 0x7FFFFFFFFFFFULL) ? p : 0;
}

int Game::equippedPersonaId() {
    int slot = (int16_t)r16(SAVE + 0xA30);
    if (slot < 0 || slot > 11) slot = 0;
    return r16(mcPersona(slot) + 2);
}

const Game::AffOverride* Game::affinityOverride(int pid, int slot) const {
    for (auto& o : affOverrides) if (o.pid == pid && o.slot == slot) return &o;
    return nullptr;
}

void Game::setAffinity(int pid, int slot, uint16_t value) {
    if (pid < 0 || pid > 255 || slot < 0 || slot > 15) return;
    uint64_t tbl = affinityTable();
    uint64_t a = tbl ? tbl + (uint64_t)pid * 32 + (uint64_t)slot * 2 : 0;
    for (size_t i = 0; i < affOverrides.size(); i++) {
        AffOverride& o = affOverrides[i];
        if (o.pid != pid || o.slot != slot) continue;
        if (value == o.orig) affOverrides.erase(affOverrides.begin() + i);   // back to the game's value: no override needed
        else o.value = value;
        if (a) mem_->wr<uint16_t>(a, value);
        return;
    }
    uint16_t orig = a ? mem_->rd<uint16_t>(a, value) : value;
    if (orig == value) return;
    affOverrides.push_back({(uint16_t)pid, (uint16_t)slot, value, orig});
    if (a) mem_->wr<uint16_t>(a, value);
}

void Game::clearAffinities(int pid) {
    uint64_t tbl = affinityTable();
    for (size_t i = 0; i < affOverrides.size();) {
        AffOverride& o = affOverrides[i];
        if (pid >= 0 && o.pid != pid) { i++; continue; }
        if (tbl) mem_->wr<uint16_t>(tbl + (uint64_t)o.pid * 32 + (uint64_t)o.slot * 2, o.orig);
        affOverrides.erase(affOverrides.begin() + i);
    }
}
