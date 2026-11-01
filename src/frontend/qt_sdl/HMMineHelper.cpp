/*
    Harvest Moon DS (EU, ABCP) mine helper - see HMMineHelper.h
    All addresses below were reverse engineered from the EU ROM (Rev 1).
*/

#include "HMMineHelper.h"

using namespace melonDS;

namespace HMMine
{

// --- game data (EU ROM) -------------------------------------------------------------------
static const u32 kGlobalPtr   = 0x021C0DA0; // -> game state block (p)
static const u32 kMapIdOff    = 0x0FE0;     // u8  p+0xFE0: current map (mines: 0x39..0x40)
static const u32 kGridOff     = 0x4E6C;     // u8[] p+0x4E6C: floor cells (+0x366 for map 0x39)
static const u32 kStairsFlag  = 0x5234;     // u8  bit0: stairs already found on this floor
static const u32 kMineIdxOff  = 0x5235;     // u8  0..3 = mine 1..4
static const u32 kLevelOff    = 0x5236;     // u16 current level
static const u32 kDims        = 0x022BC004; // u8 width, (u8) +2: height
static const u32 kRockTblPtr  = 0x0218BFD0; // -> u16[] items inside rocks (index = rock type - 2)
static const u32 kGndTblPtr   = 0x0218BFD8; // -> u16[] ground content   (index = low nibble - 1)
static const u32 kCamX        = 0x023D3E0C; // s32 8.8
static const u32 kCamY        = 0x023D3E10; // s32 8.8

// handshake with the ROM patch (stairs hook; words in ARM9 ITCM at 0x01FF9E00, offset 0x1E00)
static const u32 kMagicOff    = 0x1E00;
static const u32 kReqOff      = 0x1E04;
static const u32 kAnsOff      = 0x1E08;
static const u32 kMagic       = 0x484D4D48; // 'HMMH'

// ground content ids
static const u16 kIdHole      = 0x1000;
static const u16 kIdStairs    = 0x1002;
static const u16 kIdCursedTool= 0x1003;
static const u16 kIdCursedAcc = 0x1004;
// rock content ids
static const u16 kIdMystril   = 0x39;
static const u16 kIdMythic    = 0x3C;

static const int kMaxLevel[4] = { 10, 255, 999, 65535 };

static inline u8  rd8 (u8* ram, u32 mask, u32 a) { return ram[a & mask]; }
static inline u16 rd16(u8* ram, u32 mask, u32 a) { return ram[a & mask] | (ram[(a + 1) & mask] << 8); }
static inline u32 rd32(u8* ram, u32 mask, u32 a) { return rd16(ram, mask, a) | ((u32)rd16(ram, mask, a + 2) << 16); }
static inline void wr32(u8* ram, u32 mask, u32 a, u32 v)
{
    for (int i = 0; i < 4; i++) ram[(a + i) & mask] = (u8)(v >> (8 * i));
}
static inline bool isRAM(u32 a) { return a >= 0x02000000 && a < 0x02400000; }

bool Helper::update(u8* ram, u32 mask, u8* itcm, bool isHMEU, bool indicatorOn, bool selectorOn, u32 keys)
{
    state.inMine = false;
    state.cells.clear();

    if (!isHMEU || !ram || !itcm)
    {
        state.selecting = false;
        prevKeys = keys;
        return false;
    }

    // ---- level selection on the stairs down --------------------------------------------
    // announce ourselves (the ROM consumes it when the stairs are taken)
    wr32(itcm, 0x7FFF, kMagicOff, selectorOn ? kMagic : 0);

    u32 p = rd32(ram, mask, kGlobalPtr);
    u32 req = rd32(itcm, 0x7FFF, kReqOff);
    bool waiting = (req != 0) && (rd32(itcm, 0x7FFF, kAnsOff) == 0);

    if (waiting && isRAM(p))
    {
        int mine = rd8(ram, mask, p + kMineIdxOff) & 3;
        int cur = rd16(ram, mask, p + kLevelOff);
        int maxl = kMaxLevel[mine];
        int minl = (int)req;                      // = cur + 1
        if (minl > maxl) minl = maxl;

        if (!state.selecting)
        {
            state.selecting = true;
            state.selLevel = minl;
            holdKey = 0; holdFrames = 0;
            prevKeys = keys;                     // ignore keys that were already down
        }
        state.curLevel = cur;
        state.minLevel = minl;
        state.maxLevel = maxl;

        u32 pressed = keys & ~prevKeys;

        // auto-repeat for the direction keys
        const u32 kRepeatable = (1<<4)|(1<<5)|(1<<6)|(1<<7)|(1<<8)|(1<<9)|(1<<10)|(1<<11);
        u32 held = keys & kRepeatable;
        if (held && (held == holdKey))
        {
            holdFrames++;
            if (holdFrames > 18 && ((holdFrames - 18) % 3) == 0) pressed |= held;
        }
        else
        {
            holdKey = held;
            holdFrames = 0;
        }

        int sel = state.selLevel;
        if (pressed & (1<<6))  sel += 1;      // up
        if (pressed & (1<<7))  sel -= 1;      // down
        if (pressed & (1<<4))  sel += 10;     // right
        if (pressed & (1<<5))  sel -= 10;     // left
        if (pressed & (1<<8))  sel += 100;    // R
        if (pressed & (1<<9))  sel -= 100;    // L
        if (pressed & (1<<10)) sel += 1000;   // X
        if (pressed & (1<<11)) sel -= 1000;   // Y
        if (sel < minl) sel = minl;
        if (sel > maxl) sel = maxl;
        state.selLevel = sel;

        if (pressed & (1<<0))                 // A: go to the selected level
            wr32(itcm, 0x7FFF, kAnsOff, (u32)sel);
        else if (pressed & (1<<1))            // B: just the next level
            wr32(itcm, 0x7FFF, kAnsOff, (u32)minl);

        prevKeys = keys;
        return true;
    }

    if (state.selecting)
    {
        // keep swallowing the confirm button until it is released
        if (keys & 3) { prevKeys = keys; return true; }
        state.selecting = false;
    }
    prevKeys = keys;

    // ---- floor indicator ---------------------------------------------------------------
    if (!indicatorOn || !isRAM(p)) return false;

    int map = rd8(ram, mask, p + kMapIdOff);
    if (map < 0x39 || map > 0x40) return false;

    int w = rd8(ram, mask, kDims);
    int h = rd8(ram, mask, kDims + 2);
    if (w <= 0 || w > 64 || h <= 0 || h > 64) return false;

    u32 gnd = rd32(ram, mask, kGndTblPtr);
    u32 rock = rd32(ram, mask, kRockTblPtr);
    if (!isRAM(gnd)) return false;

    state.inMine = true;

    u32 grid = p + kGridOff + (map == 0x39 ? 0x366 : 0);
    bool stairsFound = rd8(ram, mask, p + kStairsFlag) & 1;
    int camx = (int)rd32(ram, mask, kCamX) >> 8;
    int camy = (int)rd32(ram, mask, kCamY) >> 8;

    for (int r = 0; r < h; r++)
    {
        for (int c = 0; c < w; c++)
        {
            u8 v = rd8(ram, mask, grid + r * w + c);
            int lo = v & 0xF, hi = v >> 4;
            int kind = -1;

            if (lo >= 1 && lo <= 0xB)
            {
                u16 id = rd16(ram, mask, gnd + (lo - 1) * 2);
                if (id == kIdHole) kind = Kind_Hole;
                else if (id == kIdStairs && !stairsFound) kind = Kind_Stairs;
                else if (id == kIdCursedTool || id == kIdCursedAcc) kind = Kind_Cursed;
            }
            if (kind < 0 && hi >= 2 && hi <= 0xF && isRAM(rock))
            {
                u16 id = rd16(ram, mask, rock + (hi - 2) * 2);
                if (id == kIdMystril || id == kIdMythic) kind = Kind_Ore;
            }
            if (kind < 0) continue;

            int sx = (c + 1) * 16 - camx;
            int sy = (r + 4) * 16 - camy;
            if (sx <= -16 || sx >= 256 || sy <= -16 || sy >= 192) continue;
            state.cells.push_back({sx, sy, (CellKind)kind});
        }
    }
    return false;
}

}
