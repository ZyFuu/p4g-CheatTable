#include "game.h"
#include "data.h"
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <string>

using namespace G;

static const uint8_t CRIT_PATTERN[5] = {0x45, 0x3B, 0xC6, 0x7C, 0x0D};
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
        crit_ = HookState(); exp_ = HookState(); critMode_ = 0; cacheOk_ = false;
        lastEvent = "P4G.exe closed";
    }
    if (!mem_->attached()) return;
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
    // HP / SP freezes (value captured when the freeze was enabled)
    for (int m = 0; m < 8; m++) {
        uint64_t u = unit(m);
        if (freezeHp[m] >= 0 && r16(u + 8) != (uint16_t)freezeHp[m]) w16(u + 8, (uint16_t)freezeHp[m]);
        if (freezeSp[m] >= 0 && r16(u + 0xA) != (uint16_t)freezeSp[m]) w16(u + 0xA, (uint16_t)freezeSp[m]);
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
    removeHook(crit_); removeHook(exp_); critMode_ = 0;
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
    if (exp_.active) mem_->wr<uint32_t>(exp_.cave + 56, (uint32_t)expMult_);
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
