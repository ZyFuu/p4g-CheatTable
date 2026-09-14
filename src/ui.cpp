// Persona 4 Golden trainer - user interface (Dear ImGui)
#include "ui.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "game.h"
#include "data.h"
#ifdef _WIN32
#include <windows.h>
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <string>
#include <vector>
#include <utility>
#include <cfloat>

using namespace G;

namespace {

// ---------------------------------------------------------------- state
Game* g = nullptr;
IMemory* gMem = nullptr;
ImFont *fBody = nullptr, *fBold = nullptr, *fTitle = nullptr, *fSmall = nullptr;
float S = 1.0f;   // dpi scale
int tab = 0;
int partySel = 0, mcSlot = 0, affTarget = 0, itemCat = 0, compSel = 1;
std::vector<std::pair<uint16_t, std::string>> skillItems, personaItems, cmmItems, dateItems;
char itemFilter[64] = "";

const ImU32 YELLOW = IM_COL32(247, 198, 0, 255), YELLOW_DIM = IM_COL32(247, 198, 0, 90), BLACK = IM_COL32(13, 13, 15, 255);
const ImU32 WHITE = IM_COL32(240, 240, 240, 255), RED = IM_COL32(255, 75, 64, 255), GREY = IM_COL32(150, 150, 160, 255);
const ImVec4 V_YELLOW(0.97f, 0.78f, 0.0f, 1.0f), V_RED(1.0f, 0.3f, 0.25f, 1.0f), V_GREY(0.6f, 0.6f, 0.65f, 1.0f), V_GREEN(0.55f, 0.85f, 0.2f, 1.0f);

const char* const TAB_NAMES[] = {"PARTY", "BATTLE", "CHARACTER", "SOCIAL LINKS", "COMPENDIUM", "ITEMS", "ABOUT"};
const int TAB_COUNT = 7;

// ---------------------------------------------------------------- helpers
bool icontains(const char* hay, const char* needle) {
    if (!*needle) return true;
    size_t n = strlen(needle);
    for (const char* p = hay; *p; p++) {
        size_t i = 0;
        while (i < n && p[i] && tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) i++;
        if (i == n) return true;
    }
    return false;
}

// text sheared to the right, the way the game's UI slants its headings
void ShearedText(ImFont* font, float size, ImVec2 pos, ImU32 col, const char* text, float k = 0.18f) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    int v0 = dl->VtxBuffer.Size;
    dl->AddText(font, size, pos, col, text);
    float base = pos.y + size;
    for (int i = v0; i < dl->VtxBuffer.Size; i++) dl->VtxBuffer[i].pos.x += (base - dl->VtxBuffer[i].pos.y) * k;
}

void SkewQuad(ImDrawList* dl, ImVec2 a, ImVec2 b, float k, ImU32 col) {
    dl->AddQuadFilled(ImVec2(a.x + k, a.y), ImVec2(b.x + k, a.y), ImVec2(b.x - k, b.y), ImVec2(a.x - k, b.y), col);
}

// slanted yellow tab in the left column
bool NavButton(const char* label, bool selected, float w, float h) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(label, ImVec2(w, h));
    bool hovered = ImGui::IsItemHovered(), clicked = ImGui::IsItemClicked();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (selected) SkewQuad(dl, p, ImVec2(p.x + w, p.y + h), 8 * S, YELLOW);
    else if (hovered) SkewQuad(dl, p, ImVec2(p.x + w, p.y + h), 8 * S, IM_COL32(40, 40, 46, 255));
    ShearedText(fBold, fBold->FontSize, ImVec2(p.x + 22 * S, p.y + (h - fBold->FontSize) * 0.5f), selected ? BLACK : (hovered ? WHITE : GREY), label, 0.12f);
    return clicked;
}

void Section(const char* title) {
    ImGui::Dummy(ImVec2(0, 4 * S));
    ImVec2 p = ImGui::GetCursorScreenPos();
    float h = fBold->FontSize + 8 * S;
    float w = ImGui::CalcTextSize(title).x * 1.15f + 40 * S;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    SkewQuad(dl, p, ImVec2(p.x + w, p.y + h), 6 * S, YELLOW);
    ShearedText(fBold, fBold->FontSize, ImVec2(p.x + 14 * S, p.y + 4 * S), BLACK, title, 0.12f);
    ImGui::Dummy(ImVec2(w, h));
    ImGui::Dummy(ImVec2(0, 2 * S));
}

void Hint(const char* text) {
    ImGui::PushFont(fSmall); ImGui::PushStyleColor(ImGuiCol_Text, V_GREY); ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos(); ImGui::PopStyleColor(); ImGui::PopFont();
}

// combo with a type-to-filter box, for the long skill / persona lists
bool ComboFilter(const char* id, uint16_t& value, const std::vector<std::pair<uint16_t, std::string>>& items, float width) {
    static char filter[64] = "";
    const char* cur = nullptr;
    for (auto& it : items) if (it.first == value) { cur = it.second.c_str(); break; }
    char preview[96];
    if (cur) snprintf(preview, sizeof preview, "%s", cur); else snprintf(preview, sizeof preview, "%u (unknown)", (unsigned)value);
    ImGui::SetNextItemWidth(width);
    bool changed = false;
    if (ImGui::BeginCombo(id, preview, ImGuiComboFlags_HeightLargest)) {
        if (ImGui::IsWindowAppearing()) { filter[0] = 0; ImGui::SetKeyboardFocusHere(); }
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##filter", "type to filter...", filter, sizeof filter);
        ImGui::BeginChild("##list", ImVec2(0, 260 * S), ImGuiChildFlags_None, ImGuiWindowFlags_None);
        for (auto& it : items) {
            if (!icontains(it.second.c_str(), filter)) continue;
            ImGui::PushID(it.first);
            if (ImGui::Selectable(it.second.c_str(), it.first == value)) { value = it.first; changed = true; ImGui::CloseCurrentPopup(); }
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::EndCombo();
    }
    return changed;
}

static const char* personaName(uint16_t id) {
    for (auto& it : personaItems) if (it.first == id) return it.second.c_str();
    return "?";
}

bool ComboIndex(const char* id, int& idx, const char* const* names, int count, float width) {
    ImGui::SetNextItemWidth(width);
    if (idx < 0 || idx >= count) idx = 0;
    bool changed = false;
    if (ImGui::BeginCombo(id, names[idx])) {
        for (int i = 0; i < count; i++) { if (ImGui::Selectable(names[i], i == idx)) { idx = i; changed = true; } }
        ImGui::EndCombo();
    }
    return changed;
}

// numeric fields bound to game memory
void FieldU8(const char* label, uint64_t addr, float w = 90) {
    int v = g->r8(addr); ImGui::SetNextItemWidth(w * S);
    if (ImGui::InputInt(label, &v, 1, 10)) { if (v < 0) v = 0; if (v > 255) v = 255; g->w8(addr, (uint8_t)v); }
}
void FieldU16(const char* label, uint64_t addr, float w = 110, int maxv = 65535) {
    int v = g->r16(addr); ImGui::SetNextItemWidth(w * S);
    if (ImGui::InputInt(label, &v, 1, 10)) { if (v < 0) v = 0; if (v > maxv) v = maxv; g->w16(addr, (uint16_t)v); }
}
void FieldU32(const char* label, uint64_t addr, float w = 140) {
    int v = (int)g->r32(addr); ImGui::SetNextItemWidth(w * S);
    if (ImGui::InputInt(label, &v, 100, 10000)) { if (v < 0) v = 0; g->w32(addr, (uint32_t)v); }
}
void SkillCombo(const char* id, uint64_t addr, float w) {
    uint16_t v = g->r16(addr);
    if (ComboFilter(id, v, skillItems, w * S)) g->w16(addr, v);
}
void PersonaCombo(const char* id, uint64_t addr, float w) {
    uint16_t v = g->r16(addr);
    if (ComboFilter(id, v, personaItems, w * S)) g->w16(addr, v);
}

// one persona record (stock slot, party member's persona, or compendium entry): same layout everywhere
void PersonaRecord(uint64_t rec, bool showId) {
    if (showId) { ImGui::TextColored(V_YELLOW, "Persona"); ImGui::SameLine(110 * S); PersonaCombo("##pid", rec + 2, 260); }
    ImGui::TextColored(V_YELLOW, "Level"); ImGui::SameLine(110 * S); FieldU8("##lvl", rec + 4, 90);
    ImGui::SameLine(); ImGui::TextColored(V_YELLOW, "  EXP"); ImGui::SameLine(); FieldU32("##exp", rec + 8, 140);
    ImGui::Dummy(ImVec2(0, 4 * S));
    ImGui::TextColored(V_YELLOW, "Skills");
    for (int k = 0; k < 8; k++) {
        ImGui::PushID(k);
        if (k % 2 == 1) ImGui::SameLine(340 * S);
        ImGui::Text("%d", k + 1); ImGui::SameLine(); SkillCombo("##sk", rec + 0xC + k * 2, 280);
        ImGui::PopID();
    }
    ImGui::Dummy(ImVec2(0, 4 * S));
    ImGui::TextColored(V_YELLOW, "Stats");
    static const char* const ST[5] = {"St", "Ma", "En", "Ag", "Lu"};
    for (int k = 0; k < 5; k++) {
        ImGui::PushID(100 + k);
        if (k) ImGui::SameLine();
        ImGui::Text("%s", ST[k]); ImGui::SameLine(); FieldU8("##st", rec + 0x1C + k, 70);
        ImGui::PopID();
    }
}

// ---------------------------------------------------------------- tabs
void TabParty() {
    Section("PARTY MEMBER");
    for (int m = 0; m < 8; m++) {
        if (m) ImGui::SameLine();
        bool sel = partySel == m;
        if (sel) ImGui::PushStyleColor(ImGuiCol_Button, V_YELLOW), ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.05f, 0.05f, 0.06f, 1));
        if (ImGui::Button(MEMBER_NAMES[m], ImVec2(118 * S, 30 * S))) partySel = m;
        if (sel) ImGui::PopStyleColor(2);
    }
    ImGui::Dummy(ImVec2(0, 6 * S));
    uint64_t u = unit(partySel);
    auto vital = [&](const char* name, uint64_t addr, int* freeze) {
        ImGui::TextColored(V_YELLOW, "%s", name); ImGui::SameLine(110 * S);
        int v = g->r16(addr); ImGui::SetNextItemWidth(110 * S);
        ImGui::PushID(name);
        if (ImGui::InputInt("##v", &v, 1, 10)) { if (v < 0) v = 0; if (v > 9999) v = 9999; g->w16(addr, (uint16_t)v); if (*freeze >= 0) *freeze = v; }
        ImGui::SameLine();
        bool f = *freeze >= 0;
        if (ImGui::Checkbox("Freeze", &f)) *freeze = f ? (int)g->r16(addr) : -1;
        ImGui::PopID();
    };
    vital("HP", u + 8, &g->freezeHp[partySel]);
    vital("SP", u + 0xA, &g->freezeSp[partySel]);
    {
        bool all = true; for (int m = 0; m < 8; m++) if (g->freezeHp[m] < 0 || g->freezeSp[m] < 0) all = false;
        if (ImGui::Checkbox("Freeze HP and SP for the whole party (at their current values)", &all))
            for (int m = 0; m < 8; m++) { g->freezeHp[m] = all ? (int)g->r16(unit(m) + 8) : -1; g->freezeSp[m] = all ? (int)g->r16(unit(m) + 0xA) : -1; }
    }
    if (partySel == 0) {
        ImGui::TextColored(V_YELLOW, "Level"); ImGui::SameLine(110 * S); FieldU8("##mclvl", u + 6, 90);
        ImGui::SameLine(); ImGui::TextColored(V_YELLOW, "  EXP"); ImGui::SameLine(); FieldU32("##mcexp", u + 0x40, 140);
        Hint("Yu's own level and EXP; each persona keeps its own below. Levels catch up after the next battle when you edit EXP.");
        Section("PERSONA STOCK (12 SLOTS)");
        int equipped = (int16_t)g->r16(SAVE + 0xA30);
        if (ImGui::BeginTable("stock", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH, ImVec2(0, 0))) {
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 30 * S);
            ImGui::TableSetupColumn("Persona", ImGuiTableColumnFlags_WidthFixed, 260 * S);
            ImGui::TableSetupColumn("Lv", ImGuiTableColumnFlags_WidthFixed, 60 * S);
            ImGui::TableSetupColumn("EXP", ImGuiTableColumnFlags_WidthFixed, 110 * S);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
            for (int s = 0; s < 12; s++) {
                uint64_t rec = mcPersona(s);
                ImGui::PushID(s);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (s == equipped) ImGui::TextColored(V_YELLOW, "%d*", s + 1); else ImGui::Text("%d", s + 1);
                ImGui::TableSetColumnIndex(1); PersonaCombo("##p", rec + 2, 250);
                ImGui::TableSetColumnIndex(2); ImGui::Text("%d", g->r8(rec + 4));
                ImGui::TableSetColumnIndex(3); ImGui::Text("%u", g->r32(rec + 8));
                ImGui::TableSetColumnIndex(4);
                bool sel = mcSlot == s;
                if (sel) ImGui::PushStyleColor(ImGuiCol_Button, V_YELLOW), ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.05f, 0.05f, 0.06f, 1));
                if (ImGui::SmallButton(sel ? "editing" : "edit")) mcSlot = s;
                if (sel) ImGui::PopStyleColor(2);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        Hint("* = equipped. A persona slot needs its flag set to exist: use a slot the game already filled.");
        char title[48]; snprintf(title, sizeof title, "SLOT %d DETAILS", mcSlot + 1);
        Section(title);
        ImGui::PushID("mcrec"); PersonaRecord(mcPersona(mcSlot), false); ImGui::PopID();
    } else {
        Section("PERSONA");
        ImGui::PushID("prec"); PersonaRecord(partyPersona(partySel), true); ImGui::PopID();
        Hint("Party members have no separate level: the persona's level is theirs.");
    }
}

void TabBattle() {
    Section("100% CRITICAL HITS");
    int crit = g->critMode();
    bool changed = false;
    changed |= ImGui::RadioButton("Off", &crit, 0); ImGui::SameLine();
    changed |= ImGui::RadioButton("Yu only", &crit, 1); ImGui::SameLine();
    changed |= ImGui::RadioButton("Whole party", &crit, 2);
    if (changed && !g->setCritMode(crit)) {}
    Hint("Code hook on the crit roll (P4G.exe+D50E6): every attack from the chosen characters crits. Enemies keep their normal roll.");

    Section("EXP MULTIPLIER");
    bool on = g->expEnabled();
    if (ImGui::Checkbox("Enabled", &on)) g->setExpEnabled(on);
    ImGui::SameLine(160 * S);
    static const int MULTS[] = {1, 2, 3, 5, 10, 25, 50, 99};
    static const char* const MULT_NAMES[] = {"x1 (no bonus)", "x2", "x3", "x5", "x10", "x25", "x50", "x99"};
    int mi = 1; for (int i = 0; i < 8; i++) if (MULTS[i] == g->expMult()) mi = i;
    if (ComboIndex("##mult", mi, MULT_NAMES, 8, 160 * S)) g->setExpMult(MULTS[mi]);
    Hint("Hook on the EXP gain helper (P4G.exe+1011D0): applies to Yu, every persona and every party member, before the game's 99,999 cap per battle.");
    if (!g->hookError.empty()) ImGui::TextColored(V_RED, "%s", g->hookError.c_str());

    Section("SHUFFLE TIME");
    bool sa = g->shuffleAlways();
    if (ImGui::Checkbox("Shuffle Time after every battle", &sa)) g->setShuffleAlways(sa);
    Hint("Removes the chance roll (30% + 20% All-Out finish + 10% per battle without one). Bosses and scripted fights stay excluded by the game.");
    bool fc = g->forceCardsEnabled();
    if (ImGui::Checkbox("Force the cards", &fc)) g->setForceCards(fc);
    ImGui::SameLine(300 * S); ImGui::Text("Cards dealt"); ImGui::SameLine();
    static const char* const COUNTS[7] = {"Game's choice", "1", "2", "3", "4", "5", "6"};
    int fcn = g->forceCount;
    if (ComboIndex("##fcn", fcn, COUNTS, 7, 140 * S)) { g->forceCount = fcn; g->syncForceCards(); }
    Hint("Card 1..6 = dealt slots in order (the game deals up to 6). The game never deals two cards of the same suit or arcana: such a forced card is redrawn by it.");
    static std::vector<std::string> cardNames; static std::vector<uint16_t> cardVals;
    if (cardNames.empty()) {
        static const char* const ARC[22][2] = {
            {"Fool", "changes all dealt cards, +1 draw"}, {"Magician", "ranks up 1 skill of the equipped persona"},
            {"Priestess", "1 dealt card becomes an Arcana card, +1 draw"}, {"Empress", "1 dealt card disappears, +1 draw"},
            {"Emperor", "equipped persona +1 level"}, {"Hierophant", "1 dealt card becomes a Persona card"},
            {"Lovers", "+2 draws, no item drops after battle"}, {"Chariot", "equipped persona +1 Ag"},
            {"Justice", "equipped persona +1 St"}, {"Hermit", "shadows ignore you on this floor"},
            {"Fortune", "equipped persona +1 Lu"}, {"Strength", "equipped persona +1 Ma"},
            {"Hanged Man", "equipped persona +1 En"}, {"Death", "ends Shuffle Time"},
            {"Temperance", "gain a Chest Key"}, {"Devil", "+3 draws, EXP reduced to 1"},
            {"Tower", "+3 draws, yen reduced to 0"}, {"Star", "+1 draw, removes 1 of your picked cards"},
            {"Moon", "+2 draws, EXP halved"}, {"Sun", "+2 draws, yen halved"},
            {"Judgement", "no effect"}, {"Aeon", "+4 draws"} };
        static const int WAND[10] = {20, 25, 30, 35, 40, 45, 50, 55, 60, 70}, CUPSP[10] = {6, 8, 10, 12, 14, 16, 18, 20, 22, 25};
        char t[128];
        cardNames.push_back("Game's draw"); cardVals.push_back(0);
        for (int r = 1; r <= 10; r++) { snprintf(t, sizeof t, "Sword %d - skill card (rank %d)", r, r); cardNames.push_back(t); cardVals.push_back((uint16_t)(22 + r)); }
        for (int r = 1; r <= 10; r++) { snprintf(t, sizeof t, "Coin %d - yen +%d%%", r, r * 10); cardNames.push_back(t); cardVals.push_back((uint16_t)(32 + r)); }
        for (int r = 1; r <= 10; r++) { snprintf(t, sizeof t, "Wand %d - EXP +%d%%", r, WAND[r - 1]); cardNames.push_back(t); cardVals.push_back((uint16_t)(42 + r)); }
        for (int r = 1; r <= 10; r++) { snprintf(t, sizeof t, "Cup %d - HP +%d%%, SP +%d%%", r, r * 10, CUPSP[r - 1]); cardNames.push_back(t); cardVals.push_back((uint16_t)(52 + r)); }
        for (int a = 0; a < 22; a++) { snprintf(t, sizeof t, "Arcana %d %s - %s", a, ARC[a][0], ARC[a][1]); cardNames.push_back(t); cardVals.push_back((uint16_t)(a + 1)); }
    }
    static std::vector<const char*> cardPtrs;
    if (cardPtrs.empty()) for (auto& n : cardNames) cardPtrs.push_back(n.c_str());
    for (int i = 0; i < 6; i++) {
        ImGui::PushID(500 + i);
        if (i % 2) ImGui::SameLine(490 * S);
        ImGui::Text("Card %d", i + 1); ImGui::SameLine();
        int idx = 0; for (size_t k = 0; k < cardVals.size(); k++) if (cardVals[k] == g->forceCards[i]) idx = (int)k;
        if (ComboIndex("##fc", idx, cardPtrs.data(), (int)cardPtrs.size(), 380 * S)) { g->forceCards[i] = cardVals[idx]; g->syncForceCards(); }
        ImGui::PopID();
    }
    Game::ShuffleInfo si = g->shuffleInfo();
    if (si.ok) { ImGui::TextColored(V_YELLOW, "Last battle:"); ImGui::SameLine(); ImGui::Text("%s", si.triggered ? "Shuffle Time" : "no Shuffle Time"); }

    Section("DUNGEON SHADOWS");
    static const char* const SYMS[3] = {"Game's choice", "Powerful shadows only", "Rare shadows only (gold hands)"};
    int sm = g->symbolMode() == 2 ? 1 : (g->symbolMode() == 3 ? 2 : 0);
    ImGui::Text("Shadow symbols"); ImGui::SameLine(160 * S);
    if (ComboIndex("##sym", sm, SYMS, 3, 300 * S)) g->setSymbolMode(sm == 1 ? 2 : (sm == 2 ? 3 : 0));
    Hint("Every floor has 3 encounter groups (20 normal slots, 5 powerful, 5 rare). Forces the group when the floor has one; takes effect on the next floor / re-entry.");
    ImGui::Text("Enemy debuffs"); ImGui::SameLine(160 * S);
    bool dAtk = g->enemyDebuff & 1, dDef = g->enemyDebuff & 2, dAgi = g->enemyDebuff & 4;
    if (ImGui::Checkbox("Attack -1 (Tarunda)", &dAtk)) g->enemyDebuff = (g->enemyDebuff & ~1) | (dAtk ? 1 : 0);
    ImGui::SameLine();
    if (ImGui::Checkbox("Defense -1 (Rakunda)", &dDef)) g->enemyDebuff = (g->enemyDebuff & ~2) | (dDef ? 2 : 0);
    ImGui::SameLine();
    if (ImGui::Checkbox("Hit / Evasion -1 (Sukunda)", &dAgi)) g->enemyDebuff = (g->enemyDebuff & ~4) | (dAgi ? 4 : 0);
    Hint("Written every 100 ms on every living enemy while a battle is running (bosses included), with a duration that never runs out.");

    Section("PARTY BUFFS AND CHARGES (WRITTEN EVERY 100 MS WHILE SET)");
    static const char* const BUFFS[] = {"Leave alone", "Attack", "Defense", "Agility", "Attack + Defense", "Attack + Agility", "Defense + Agility", "All three", "Clear"};
    static const char* const CHARGES[] = {"Leave alone", "Tetrakarn (repel phys)", "Makarakarn (repel magic)", "Power Charge", "Mind Charge", "Tetraja", "Tetrakarn + Makarakarn", "Power + Mind Charge", "Shields + Tetraja", "All but Tetraja", "All", "Clear"};
    if (ImGui::BeginTable("buffs", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn("Member", ImGuiTableColumnFlags_WidthFixed, 110 * S);
        ImGui::TableSetupColumn("Buffs", ImGuiTableColumnFlags_WidthFixed, 230 * S);
        ImGui::TableSetupColumn("Charges / shields", ImGuiTableColumnFlags_WidthFixed, 260 * S);
        ImGui::TableHeadersRow();
        for (int m = 0; m < 8; m++) {
            if (m == 4) continue;
            ImGui::PushID(m);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::Text("%s", MEMBER_NAMES[m]);
            ImGui::TableSetColumnIndex(1); ComboIndex("##b", g->buffMode[m], BUFFS, 9, 220 * S);
            ImGui::TableSetColumnIndex(2); ComboIndex("##c", g->chargeMode[m], CHARGES, 12, 250 * S);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    Section("AFFINITIES OF A PERSONA");
    static const char* const TARGETS[] = {"Yu - equipped persona", "Yosuke", "Chie", "Yukiko", "Rise", "Kanji", "Naoto", "Teddie"};
    ComboIndex("##afft", affTarget, TARGETS, 8, 220 * S);
    int pid = affTarget == 0 ? g->equippedPersonaId() : g->r16(unit(affTarget) + 0x56);
    const char* pname = "?"; for (auto& it : personaItems) if (it.first == pid) pname = it.second.c_str();
    ImGui::SameLine(); ImGui::TextColored(V_YELLOW, "%s", pname);
    uint64_t tbl = g->affinityTable();
    if (!tbl) { ImGui::TextColored(V_RED, "Affinity table pointer not readable (game not attached?)"); }
    else {
        static const uint16_t AFF_VALS[] = {0x14, 0x800, 0x1000, 0x100, 0x200, 0x400, 0x0A, 0x1E, 0x100A, 0x81E};
        static const char* const AFF_LABELS[] = {"Normal (100%)", "Weak", "Resist", "Null", "Repel", "Drain", "Normal, 50% dmg", "Normal, 150% dmg", "Resist, 50% dmg", "Weak, 150% dmg"};
        if (ImGui::BeginTable("aff", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
            for (int i = 0; i < 16; i++) {
                uint64_t a = tbl + (uint64_t)pid * 32 + i * 2;
                uint16_t v = g->r16(a);
                bool edited = g->affinityOverride(pid, i) != nullptr;
                ImGui::PushID(i);
                ImGui::TableNextColumn();
                ImGui::TextColored(edited ? V_GREEN : (i < 8 ? V_YELLOW : V_GREY), edited ? "%s *" : "%s", AFF_NAMES[i]);
                ImGui::TableNextColumn();
                int cur = -1; for (int k = 0; k < 10; k++) if (AFF_VALS[k] == v) cur = k;
                char preview[40]; if (cur >= 0) snprintf(preview, sizeof preview, "%s", AFF_LABELS[cur]); else snprintf(preview, sizeof preview, "custom %04X", v);
                ImGui::SetNextItemWidth(170 * S);
                if (ImGui::BeginCombo("##a", preview)) {
                    for (int k = 0; k < 10; k++) if (ImGui::Selectable(AFF_LABELS[k], k == cur)) g->setAffinity(pid, i, AFF_VALS[k]);
                    ImGui::EndCombo();
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
    int nThis = 0; for (auto& o : g->affOverrides) if (o.pid == pid) nThis++;
    ImGui::Text("Edited affinities: %d on this persona, %d in total", nThis, (int)g->affOverrides.size());
    ImGui::SameLine();
    if (ImGui::SmallButton("Undo this persona")) g->clearAffinities(pid);
    ImGui::SameLine();
    if (ImGui::SmallButton("Undo all")) g->clearAffinities(-1);
    Hint("Edits the loaded persona table (UNIT.TBL segment 2), so the change applies wherever that persona is used. The game reloads that table at every start: edited slots (*) are re-written every 100 ms while the trainer runs, and are part of the saved configuration (ABOUT tab).");
}

void TabCharacter() {
    Section("MONEY");
    { int v = (int)g->r32(SAVE); ImGui::SetNextItemWidth(160 * S);
      if (ImGui::InputInt("yen", &v, 100, 10000)) { if (v < 0) v = 0; g->w32(SAVE, (uint32_t)v); if (g->freezeMoney >= 0) g->freezeMoney = v; } }
    ImGui::SameLine();
    bool fm = g->freezeMoney >= 0;
    if (ImGui::Checkbox("Freeze", &fm)) g->freezeMoney = fm ? (int64_t)g->r32(SAVE) : -1;
    Section("SOCIAL STATS");
    bool sm = g->socialMultEnabled();
    if (ImGui::Checkbox("Gain multiplier", &sm)) g->setSocialMultEnabled(sm);
    ImGui::SameLine(200 * S);
    static const int SMULTS[] = {2, 3, 5, 10};
    static const char* const SMULT_NAMES[] = {"x2", "x3", "x5", "x10"};
    int smi = 0; for (int i = 0; i < 4; i++) if (SMULTS[i] == g->socialMult()) smi = i;
    if (ComboIndex("##smult", smi, SMULT_NAMES, 4, 120 * S)) g->setSocialMult(SMULTS[smi]);
    Hint("Hook on the stat-up event (P4G.exe+42675A): every gain (studying, jobs, books...) is multiplied; the game's 999 cap still applies.");
    if (ImGui::BeginTable("social", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn("Stat", ImGuiTableColumnFlags_WidthFixed, 130 * S);
        ImGui::TableSetupColumn("Points", ImGuiTableColumnFlags_WidthFixed, 120 * S);
        ImGui::TableSetupColumn("Rank", ImGuiTableColumnFlags_WidthFixed, 190 * S);
        ImGui::TableSetupColumn("Set rank", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        for (int i = 0; i < 5; i++) {
            const SocialDef& sd = SOCIAL[i];
            uint64_t a = unit(0) + 0x34 + i * 2;
            int pts = g->r16(a), rank = 0, need = 0;
            for (int r = 0; r < 4; r++) { need += sd.inc[r]; if (pts >= need) rank = r + 1; }
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextColored(V_YELLOW, "%s", sd.name);
            ImGui::TableSetColumnIndex(1); FieldU16("##pts", a, 100, 999);
            ImGui::TableSetColumnIndex(2); ImGui::Text("Rank %d - %s", rank + 1, sd.ranks[rank]);
            ImGui::TableSetColumnIndex(3);
            int th = 0;
            for (int r = 0; r < 5; r++) {
                if (r) ImGui::SameLine();
                char b[8]; snprintf(b, sizeof b, "%d", r + 1);
                if (ImGui::SmallButton(b)) g->w16(a, (uint16_t)th);
                if (r < 4) th += sd.inc[r];
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    Hint("Thresholds read from the game's own table: Courage 16/40/80/140, Knowledge 30/80/150/240, Diligence 16/40/80/130, Understanding 16/40/80/140, Expression 13/33/53/85.");

    Section("CALENDAR");
    int day = (int16_t)g->r16(TIME); if (day < 0) day = 0; if (day > 365) day = 365;
    int tod = g->r8(TIME + 2); if (tod > 5) tod = 5;
    ImGui::TextColored(V_YELLOW, "Day"); ImGui::SameLine(110 * S);
    if (ComboIndex("##day", day, DATES, 366, 200 * S)) g->w16(TIME, (uint16_t)day);
    ImGui::SameLine(); ImGui::TextColored(V_YELLOW, "  Time of day"); ImGui::SameLine();
    if (ComboIndex("##tod", tod, TOD_NAMES, 6, 180 * S)) g->w8(TIME + 2, (uint8_t)tod);
    Hint("Index 0 = 04/01. The field refreshes on the next area change.");
    Section("STAY DAYTIME AFTER STORY EVENTS");
    static const char* const STAY[] = {"Off", "Once per day: the first Evening of the day becomes After School", "Always: Evening never happens (turn off to end the day)"};
    ComboIndex("##stay", g->stayDaytime, STAY, 3, 520 * S);
    Hint("Experimental: reverts the time-of-day byte after the game sets Evening; change area afterwards so the map reloads in daytime.");
}

void TabSocialLinks() {
    Section("SOCIAL LINKS (23 SLOTS, FILLED IN THE ORDER THEY ARE STARTED)");
    bool allFrozen = true; for (int s = 0; s < 23; s++) if (g->freezeSlink[s] < 0) allFrozen = false;
    if (ImGui::Checkbox("Freeze the points of every link (at their current values)", &allFrozen))
        for (int s = 0; s < 23; s++) g->freezeSlink[s] = allFrozen ? g->r16(SLINK + s * 16 + 4) : -1;
    if (ImGui::BeginTable("sl", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY, ImVec2(0, 380 * S))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 34 * S);
        ImGui::TableSetupColumn("Link", ImGuiTableColumnFlags_WidthFixed, 300 * S);
        ImGui::TableSetupColumn("Rank (0-10)", ImGuiTableColumnFlags_WidthFixed, 130 * S);
        ImGui::TableSetupColumn("Points", ImGuiTableColumnFlags_WidthFixed, 130 * S);
        ImGui::TableSetupColumn("Freeze", ImGuiTableColumnFlags_WidthFixed, 70 * S);
        ImGui::TableHeadersRow();
        for (int s = 0; s < 23; s++) {
            uint64_t a = SLINK + s * 16;
            ImGui::PushID(s);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::Text("%02d", s + 1);
            ImGui::TableSetColumnIndex(1); { uint16_t v = g->r16(a); if (ComboFilter("##cmm", v, cmmItems, 290 * S)) g->w16(a, v); }
            ImGui::TableSetColumnIndex(2); FieldU16("##rank", a + 2, 110, 10);
            ImGui::TableSetColumnIndex(3);
            { int v = g->r16(a + 4); ImGui::SetNextItemWidth(110 * S);
              if (ImGui::InputInt("##pts", &v, 1, 10)) { if (v < 0) v = 0; if (v > 999) v = 999; g->w16(a + 4, (uint16_t)v); if (g->freezeSlink[s] >= 0) g->freezeSlink[s] = v; } }
            ImGui::TableSetColumnIndex(4);
            { bool fz = g->freezeSlink[s] >= 0; if (ImGui::Checkbox("##fz", &fz)) g->freezeSlink[s] = fz ? g->r16(a + 4) : -1; }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    Section("ROMANCE FLAGS");
    for (size_t i = 0; i < ROMANCE_COUNT; i++) {
        if (i % 4) ImGui::SameLine(0, 24 * S);
        bool on = g->flag(ROMANCE[i].bit);
        ImGui::PushID((int)i);
        if (ImGui::Checkbox(ROMANCE[i].name, &on)) g->setFlag(ROMANCE[i].bit, on);
        ImGui::PopID();
    }
    Hint("Event flags the game sets at the rank 9/10 choice; they mark the relationship for later scenes, they do not replay the confession.");
}

void TabCompendium() {
    Section("REGISTER EVERYTHING");
    if (ImGui::Button("Register every missing persona (base level, innate skills, base stats)", ImVec2(0, 32 * S))) g->registerMissingPersonas();
    Hint("Personas already registered are left as they are. 205 fusable personas; party personas are not part of the compendium.");
    Section("EDIT AN ENTRY");
    uint16_t sel = (uint16_t)compSel;
    ImGui::TextColored(V_YELLOW, "Persona"); ImGui::SameLine(110 * S);
    if (ComboFilter("##csel", sel, personaItems, 260 * S)) compSel = sel;
    uint64_t rec = compRecord(compSel);
    bool reg = g->r16(rec) & 1;
    ImGui::SameLine();
    if (ImGui::Checkbox("Registered", &reg)) { g->w16(rec, reg ? 1 : 0); if (reg) g->w16(rec + 2, (uint16_t)compSel); }
    uint16_t stored = g->r16(rec + 2);
    if (reg && stored != compSel) { ImGui::SameLine(); ImGui::TextColored(V_RED, "stored id %u", (unsigned)stored); }
    ImGui::PushID("crec"); PersonaRecord(rec, false); ImGui::PopID();
}

void TabItems() {
    ImGui::BeginChild("cats", ImVec2(250 * S, 0), ImGuiChildFlags_Borders);
    for (size_t c = 0; c < ITEM_CAT_COUNT; c++) {
        bool frozen = g->freezeCat[c] >= 0;
        char label[96]; snprintf(label, sizeof label, "%s%s", ITEM_CATS[c].name, frozen ? "  *" : "");
        if (ImGui::Selectable(label, (int)c == itemCat)) itemCat = (int)c;
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("items", ImVec2(0, 0), ImGuiChildFlags_None);
    const ItemCat& cat = ITEM_CATS[itemCat];
    Section(cat.name);
    if (ImGui::Button("All = 99")) for (size_t i = 0; i < cat.count; i++) g->w8(cat.items[i].addr, 99);
    ImGui::SameLine();
    if (ImGui::Button("All = 1")) for (size_t i = 0; i < cat.count; i++) g->w8(cat.items[i].addr, 1);
    ImGui::SameLine();
    if (ImGui::Button("All = 0")) for (size_t i = 0; i < cat.count; i++) g->w8(cat.items[i].addr, 0);
    ImGui::SameLine(0, 24 * S);
    bool frozen = g->freezeCat[itemCat] >= 0;
    if (ImGui::Checkbox("Freeze at 99", &frozen)) g->freezeCat[itemCat] = frozen ? 99 : -1;
    ImGui::SameLine(0, 24 * S);
    ImGui::SetNextItemWidth(200 * S);
    ImGui::InputTextWithHint("##if", "filter...", itemFilter, sizeof itemFilter);
    ImGui::BeginChild("list", ImVec2(0, 0), ImGuiChildFlags_None);
    if (ImGui::BeginTable("it", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Qty", ImGuiTableColumnFlags_WidthFixed, 120 * S);
        for (size_t i = 0; i < cat.count; i++) {
            if (!icontains(cat.items[i].name, itemFilter)) continue;
            ImGui::PushID((int)i);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextWrapped("%s", cat.items[i].name);
            ImGui::TableSetColumnIndex(1); FieldU8("##q", cat.items[i].addr, 100);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
    ImGui::EndChild();
}

void TabAbout() {
    Section("CONFIGURATION");
    ImGui::TextWrapped("File: %s", g->configPath().c_str());
    if (ImGui::Button("Save the current options", ImVec2(260 * S, 30 * S))) g->saveConfig();
    ImGui::SameLine();
    if (ImGui::Button("Load the saved options", ImVec2(260 * S, 30 * S))) g->loadConfig();
    if (ImGui::Checkbox("Load at startup and re-apply every time the game is attached", &g->autoApply)) g->storeAutoApply();
    if (!g->configStatus.empty()) ImGui::TextColored(V_GREEN, "%s", g->configStatus.c_str());
    Hint("Everything checked or picked in the tabs is saved: hooks, multipliers, buffs, freezes (with their values), shadow symbols, enemy debuffs, item freezes, edited persona affinities.");

    Section("ABOUT");
    ImGui::TextWrapped("Standalone trainer for the 64-bit Steam build of Persona 4 Golden (fixed image base 140000000). Same addresses and hooks as the cheat table, no Cheat Engine required.");
    ImGui::Dummy(ImVec2(0, 8 * S));
    ImGui::TextColored(V_YELLOW, "Memory map");
    ImGui::BulletText("Save block 1451BCD70: money, persona stock (+A34), party units (+C74, 0x84 each)");
    ImGui::BulletText("Event flags 1451BDF0C, calendar 1451BE64C, Social Links 1451BE660, compendium 1451BF280");
    ImGui::BulletText("Persona affinities: UNIT.TBL segment 2, pointer at P4G.exe+EC0988");
    ImGui::BulletText("Hooks: crit roll at P4G.exe+D50E6, EXP gain helper at P4G.exe+101256 (restored when turned off or on exit)");
    ImGui::Dummy(ImVec2(0, 8 * S));
    ImGui::TextColored(V_YELLOW, "Notes");
    ImGui::BulletText("Run as administrator if the game is not detected.");
    ImGui::BulletText("Single-player only. Values written while the trainer is open stay in the save when you save the game.");
    ImGui::BulletText("Dear ImGui %s", IMGUI_VERSION);
}

void DrawBackground(ImDrawList* dl, ImVec2 p0, ImVec2 p1) {
    dl->AddRectFilled(p0, p1, BLACK);
    // faint scanlines
    for (float y = p0.y; y < p1.y; y += 6 * S) dl->AddRectFilled(ImVec2(p0.x, y), ImVec2(p1.x, y + 2 * S), IM_COL32(255, 255, 255, 7));
    // yellow diagonal stripes, top-left and bottom-right corners
    auto stripes = [&](ImVec2 c, float len, float thick, int n, float dir) {
        for (int i = 0; i < n; i++) {
            float o = i * thick * 2;
            ImVec2 a(c.x + o * dir, c.y), b(c.x + (o + thick) * dir, c.y);
            dl->AddQuadFilled(a, b, ImVec2(b.x + len * 0.45f * dir, c.y + len * (dir > 0 ? 1 : -1)), ImVec2(a.x + len * 0.45f * dir, c.y + len * (dir > 0 ? 1 : -1)), YELLOW);
        }
    };
    // a striped band along the top edge and in the footer, clear of the text
    dl->PushClipRect(p0, ImVec2(p0.x + 560 * S, p0.y + 9 * S), true);
    stripes(ImVec2(p0.x - 60 * S, p0.y - 10 * S), 40 * S, 16 * S, 22, 1);
    dl->PopClipRect();
    dl->PushClipRect(ImVec2(p1.x - 420 * S, p1.y - 30 * S), p1, true);
    stripes(ImVec2(p1.x + 40 * S, p1.y + 10 * S), 60 * S, 16 * S, 16, -1);
    dl->PopClipRect();
}

} // namespace

// ---------------------------------------------------------------- public
std::string configFilePath() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH]; DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring w(buf, n); size_t k = w.find_last_of(L"\\/"); w = w.substr(0, k + 1) + L"P4GTrainer.ini";
    int m = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(m - 1, 0); WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &out[0], m, nullptr, nullptr);
    return out;
#else
    return "P4GTrainer.ini";
#endif
}

void UiInit(float dpiScale) {
    S = dpiScale;
    ImGuiIO& io = ImGui::GetIO();
#ifdef _WIN32
    const char* body = "C:\\Windows\\Fonts\\segoeui.ttf", *bold = "C:\\Windows\\Fonts\\segoeuib.ttf", *title = "C:\\Windows\\Fonts\\seguibl.ttf";
    const char* boldFb = "C:\\Windows\\Fonts\\arialbd.ttf", *titleFb = "C:\\Windows\\Fonts\\ariblk.ttf", *bodyFb = "C:\\Windows\\Fonts\\arial.ttf";
#else
    const char* body = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", *bold = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", *title = "/usr/share/fonts/truetype/dejavu/DejaVuSansCondensed-Bold.ttf";
    const char* boldFb = bold, *titleFb = title, *bodyFb = body;
#endif
    auto load = [&](const char* a, const char* b, float size) -> ImFont* {
        ImFont* f = io.Fonts->AddFontFromFileTTF(a, size * S);
        if (!f) f = io.Fonts->AddFontFromFileTTF(b, size * S);
        return f;
    };
    fBody = load(body, bodyFb, 17); if (!fBody) fBody = io.Fonts->AddFontDefault();
    fBold = load(bold, boldFb, 18); if (!fBold) fBold = fBody;
    fTitle = load(title, titleFb, 40); if (!fTitle) fTitle = fBold;
    fSmall = load(body, bodyFb, 14); if (!fSmall) fSmall = fBody;
    io.FontDefault = fBody;

    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowRounding = 0; st.FrameRounding = 0; st.PopupRounding = 0; st.GrabRounding = 0; st.TabRounding = 0; st.ChildRounding = 0;
    st.FrameBorderSize = 1; st.WindowBorderSize = 0; st.ChildBorderSize = 1;
    st.FramePadding = ImVec2(8, 5); st.ItemSpacing = ImVec2(10, 7); st.CellPadding = ImVec2(8, 5); st.ScrollbarSize = 14;
    ImVec4* c = st.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(0.05f, 0.05f, 0.06f, 1); c[ImGuiCol_ChildBg] = ImVec4(0.08f, 0.08f, 0.09f, 1); c[ImGuiCol_PopupBg] = ImVec4(0.09f, 0.09f, 0.11f, 0.98f);
    c[ImGuiCol_Border] = ImVec4(0.28f, 0.28f, 0.32f, 1); c[ImGuiCol_Text] = ImVec4(0.94f, 0.94f, 0.94f, 1); c[ImGuiCol_TextDisabled] = ImVec4(0.55f, 0.55f, 0.6f, 1);
    c[ImGuiCol_FrameBg] = ImVec4(0.13f, 0.13f, 0.16f, 1); c[ImGuiCol_FrameBgHovered] = ImVec4(0.2f, 0.2f, 0.24f, 1); c[ImGuiCol_FrameBgActive] = ImVec4(0.3f, 0.26f, 0.1f, 1);
    c[ImGuiCol_Button] = ImVec4(0.16f, 0.16f, 0.19f, 1); c[ImGuiCol_ButtonHovered] = ImVec4(0.33f, 0.28f, 0.08f, 1); c[ImGuiCol_ButtonActive] = ImVec4(0.6f, 0.49f, 0.05f, 1);
    c[ImGuiCol_CheckMark] = V_YELLOW; c[ImGuiCol_SliderGrab] = V_YELLOW; c[ImGuiCol_SliderGrabActive] = ImVec4(1, 0.88f, 0.3f, 1);
    c[ImGuiCol_Header] = ImVec4(0.33f, 0.28f, 0.08f, 1); c[ImGuiCol_HeaderHovered] = ImVec4(0.45f, 0.38f, 0.1f, 1); c[ImGuiCol_HeaderActive] = ImVec4(0.6f, 0.49f, 0.05f, 1);
    c[ImGuiCol_Separator] = ImVec4(0.25f, 0.25f, 0.28f, 1); c[ImGuiCol_TableHeaderBg] = ImVec4(0.14f, 0.14f, 0.17f, 1);
    c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0); c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.03f); c[ImGuiCol_TableBorderLight] = ImVec4(0.2f, 0.2f, 0.24f, 1);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0.06f, 0.06f, 0.07f, 1); c[ImGuiCol_ScrollbarGrab] = ImVec4(0.3f, 0.3f, 0.34f, 1); c[ImGuiCol_ScrollbarGrabHovered] = V_YELLOW;
    c[ImGuiCol_NavCursor] = V_YELLOW;
    st.ScaleAllSizes(S);

    for (size_t i = 0; i < SKILL_COUNT; i++) skillItems.push_back({SKILLS[i].id, SKILLS[i].desc[0] ? std::string(SKILLS[i].name) + "  -  " + SKILLS[i].desc : std::string(SKILLS[i].name)});
    for (size_t i = 0; i < PERSONA_COUNT; i++) personaItems.push_back({PERSONAS[i].id, PERSONAS[i].name});
    cmmItems.push_back({0, "(empty slot)"});
    for (size_t i = 0; i < CMM_COUNT; i++) cmmItems.push_back({CMM[i].id, std::string(CMM[i].name) + "  -  " + CMM[i].arcana});

    gMem = createProcessMemory();
    g = new Game(gMem);
    g->setConfigPath(configFilePath());
    if (g->configExists()) { if (g->configAutoApply()) g->loadConfig(); else { g->autoApply = false; g->configStatus = "Saved configuration found (auto-load is off)"; } }
}

void UiSelectTab(int t) { if (t >= 0 && t < TAB_COUNT) tab = t; }

void UiShutdown() { delete g; g = nullptr; delete gMem; gMem = nullptr; }

void UiFrame(double now) {
    g->tick(now);
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0)); ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##main", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetWindowPos(), p1(p0.x + io.DisplaySize.x, p0.y + io.DisplaySize.y);
    DrawBackground(dl, p0, p1);

    // header
    const float headerH = 96 * S, navW = 230 * S, footH = 34 * S;
    ShearedText(fTitle, fTitle->FontSize, ImVec2(p0.x + 30 * S, p0.y + 16 * S), WHITE, "PERSONA 4");
    float w1 = fTitle->CalcTextSizeA(fTitle->FontSize, FLT_MAX, 0.0f, "PERSONA 4").x;
    ShearedText(fTitle, fTitle->FontSize, ImVec2(p0.x + 30 * S + w1 + 18 * S, p0.y + 16 * S), YELLOW, "GOLDEN");
    {
        ImVec2 a(p0.x + 34 * S, p0.y + 64 * S), b(a.x + 190 * S, a.y + 24 * S);
        SkewQuad(dl, a, b, 5 * S, WHITE);
        ShearedText(fBold, fBold->FontSize, ImVec2(a.x + 16 * S, a.y + 2 * S), BLACK, "CHEAT TABLE", 0.12f);
    }
    // status, right side of the header
    {
        bool att = g->attached();
        const char* txt = g->status().c_str();
        std::string s = g->status();
        ImVec2 sz = ImGui::CalcTextSize(s.c_str());
        ImVec2 a(p1.x - sz.x - 44 * S, p0.y + 22 * S);
        dl->AddRectFilled(ImVec2(a.x - 12 * S, a.y - 6 * S), ImVec2(p1.x - 20 * S, a.y + sz.y + 6 * S), IM_COL32(20, 20, 24, 230));
        dl->AddRectFilled(ImVec2(a.x - 12 * S, a.y - 6 * S), ImVec2(a.x - 7 * S, a.y + sz.y + 6 * S), att ? IM_COL32(140, 214, 0, 255) : RED);
        dl->AddText(a, att ? WHITE : GREY, s.c_str());
        (void)txt;
        if (!g->lastEvent.empty()) dl->AddText(fSmall, fSmall->FontSize, ImVec2(a.x, a.y + sz.y + 12 * S), YELLOW, g->lastEvent.c_str());
    }

    // nav
    ImGui::SetCursorScreenPos(ImVec2(p0.x + 16 * S, p0.y + headerH));
    ImGui::BeginChild("nav", ImVec2(navW, io.DisplaySize.y - headerH - footH), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    for (int i = 0; i < TAB_COUNT; i++) {
        ImGui::PushID(i);
        if (NavButton(TAB_NAMES[i], tab == i, navW - 20 * S, 40 * S)) tab = i;
        ImGui::PopID();
        ImGui::Dummy(ImVec2(0, 2 * S));
    }
    ImGui::EndChild();

    // content
    ImGui::SetCursorScreenPos(ImVec2(p0.x + navW + 24 * S, p0.y + headerH));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.07f, 0.07f, 0.085f, 0.92f));
    ImGui::BeginChild("content", ImVec2(io.DisplaySize.x - navW - 44 * S, io.DisplaySize.y - headerH - footH), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleColor();
#ifndef _WIN32
    if (const char* sc = getenv("P4G_PREVIEW_SCROLL")) ImGui::SetScrollY((float)atof(sc));   // preview screenshots only
#endif
    if (!g->attached() && tab != 6) {
        ImGui::Dummy(ImVec2(0, 20 * S));
        ImGui::TextColored(V_YELLOW, "Waiting for the game.");
        ImGui::TextWrapped("Start Persona 4 Golden, load a save, and the trainer attaches by itself. If it never does, run the trainer as administrator.");
    } else {
        switch (tab) {
        case 0: TabParty(); break; case 1: TabBattle(); break; case 2: TabCharacter(); break; case 3: TabSocialLinks(); break;
        case 4: TabCompendium(); break; case 5: TabItems(); break; default: TabAbout(); break;
        }
    }
    ImGui::EndChild();

    // footer
    ImGui::PushFont(fSmall);
    dl->AddText(ImVec2(p0.x + 30 * S, p1.y - footH + 9 * S), GREY, "64-bit Steam build  |  single-player only  |  hooks are removed when the trainer closes");
    ImGui::PopFont();
    ImGui::End();
}
