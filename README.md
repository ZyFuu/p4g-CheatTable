# Persona 4 Golden - Cheat Table (standalone trainer)

Standalone trainer for the 64-bit Steam build of Persona 4 Golden. Same memory map and code hooks as the
Cheat Engine table, without Cheat Engine: one executable, no runtime to install.

## Usage

1. Start Persona 4 Golden and load a save.
2. Run `P4GTrainer.exe`. It attaches by itself (status in the top-right corner). If it never attaches,
   run it as administrator.
3. Tabs: PARTY (HP/SP/level/EXP, persona stock, skills, stats for every member), BATTLE (100% crit,
   EXP multiplier, buffs and charges written every 100 ms, persona affinities), CHARACTER (money, social
   stats with the rank thresholds read from the game, calendar, stay-daytime), SOCIAL LINKS (23 slots +
   romance flags), COMPENDIUM (register every missing persona, edit any entry), ITEMS (41 categories,
   set all / freeze).

Hooks (crit, EXP) patch 5 and 9 bytes of game code and are restored when they are turned off or when the
trainer closes. Single-player only.

## Building

- Windows executable (cross-compiled from Linux): `make win` (needs `g++-mingw-w64-x86-64`).
- Linux preview with a fake process, for UI work: `make preview`, then `./build/preview shot.png <tab>`.
- `python3 gen_data.py` regenerates `src/data.h` from the cheat table (skills, personas, items, compendium).

Dear ImGui 1.91.9 (MIT), stb_image_write (public domain).

## Memory map (P4G.exe, image base 140000000)

| What | Address |
|---|---|
| Save block (money at +0, equipped persona slot +A30, 12 persona records +A34) | 1451BCD70 |
| Party units, 8 x 0x84 (MC, Yosuke, Chie, Yukiko, Rise, Kanji, Naoto, Teddie) | 1451BD9E4 |
| Event flags (bit n = byte n>>3, bit n&7) | 1451BDF0C |
| Calendar (+0 day, +2 time of day) | 1451BE64C |
| Social Links, 23 x 16 (+0 id, +2 rank, +4 points) | 1451BE660 |
| Compendium, id x 0x30 | 1451BF280 |
| Persona affinity table pointer (UNIT.TBL segment 2) | P4G.exe+EC0988 |
| Crit roll hook site | P4G.exe+D50E6 |
| EXP gain helper hook site | P4G.exe+101256 |
