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
   romance flags), COMPENDIUM (register every missing persona, edit any entry), ITEMS (42 categories incl. the 253
   skill cards, set all / freeze).

Hooks (crit, EXP, Shuffle Time, shadow symbols, social stats) patch a few bytes of game code each and are
restored when they are turned off or when the trainer closes. Single-player only.

## Saved configuration

ABOUT tab, CONFIGURATION section. "Save the current options" writes `P4GTrainer.ini` next to the exe (plain
`key=value`, editable by hand). With "Load at startup and re-apply every time the game is attached" checked,
the trainer reads the file when it starts and installs the saved hooks and freezes as soon as P4G.exe is
attached - and again after every restart of the game, since the hooks die with the process. Within one trainer
session the options that were active when the game closed are re-applied on the next launch even if they were
never saved. Hooks are installed 1.5 s after attach and retried every 3 s (up to 20 times) while the game is
still starting.

Persona affinities (BATTLE tab) live in UNIT.TBL segment 2, which the game reloads from disk at every start.
Every edited slot is kept as an override (persona id, slot, new value, original value), re-written every 100 ms
while the table is loaded, saved in the file as `affN=pid,slot,value,orig`, and undone with the "Undo" buttons.

## Building

- Windows executable (cross-compiled from Linux): `make win` (needs `g++-mingw-w64-x86-64`).
- Linux preview with a fake process, for UI work: `make preview`, then `./build/preview shot.png <tab>`.
- `python3 gen_data.py` regenerates `src/data.h` from the cheat table (skills, personas, items, compendium).

Dear ImGui 1.91.9 (MIT), stb_image_write (public domain).

## Reproducible build (for reviewers)

On Ubuntu / Debian:

```
sudo apt install make g++-mingw-w64-x86-64
make win
```

Output: `build/P4GTrainer.exe`, statically linked, no installer, no network code, nothing written outside the
game's own process memory. Antivirus heuristics may flag it because it uses the same Win32 calls as any
trainer (OpenProcess, ReadProcessMemory / WriteProcessMemory, VirtualAllocEx for the two code caves).
Everything it does is in `src/mem_win.cpp` (process access) and `src/game.cpp` (what is read and written).

## What it touches

- Reads the save block / party / flags / calendar / compendium of P4G.exe (see the memory map below).
- Writes only the values the user edits in the window, plus the optional 100 ms freezes.
- Two optional code hooks (crit, EXP): a 5-byte and a 9-byte `jmp` into a small cave allocated inside the
  game process; original bytes are restored when the option is turned off or the trainer closes.

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
| Battle work pointer (+12A0 result params, +12E0 shuffle candidates) | P4G.exe+EC08F0 |
| Shuffle Time chance roll (jae patched to nop) | P4G.exe+B46B6 |
| Shuffle level store hook site | P4G.exe+10849A |
| Persona candidate list hook site | P4G.exe+1097DC |
| Card draw (drawCard(ctx, kind)) hook site | P4G.exe+12A5E0 |
| Card deal loop start hook site | P4G.exe+12D6B7 |
| Card count (from the layout row) hook site | P4G.exe+12D655 |
| Field encounter picker tail (shadow symbol group) hook site | P4G.exe+2CA0C3 |
| Social stat up event (gain multiplier) hook site | P4G.exe+42675A |
