/*
    Harvest Moon DS (EU, ABCP) mine helper for this melonDS fork.

    1. Floor indicator: tints the mine floor cells that hide something
       (hole, stairs, cursed item) and rocks that contain Mystril / Mythic Stone.
    2. Level selection on the stairs down: the patched ROM posts a request
       (REQ = default target) and waits; the frontend shows a selector and
       answers with the chosen level (ANS).

    The logic here is plain C++ (no Qt) so it can also be tested headless.
*/

#ifndef HMMINEHELPER_H
#define HMMINEHELPER_H

#include <vector>
#include "types.h"

namespace HMMine
{

enum CellKind { Kind_Hole = 0, Kind_Stairs, Kind_Cursed, Kind_Ore, Kind_MAX };

struct Cell
{
    int x, y;       // top-left corner in top-screen pixels (may be partly off-screen)
    CellKind kind;
};

struct State
{
    bool inMine = false;
    std::vector<Cell> cells;

    bool selecting = false;
    int curLevel = 0;      // level the player is on
    int minLevel = 0;      // smallest selectable level (cur + 1)
    int maxLevel = 0;      // bottom of this mine
    int selLevel = 0;      // currently selected level
};

class Helper
{
public:
    // keys: DS keys currently held, active-high (bit0 A, bit1 B, ... bit8 R, bit9 L)
    // returns true if the selection is active (frontend should not feed keys to the game)
    // itcm: ARM9 ITCM (32 KB) - the ROM patch keeps its handshake words there
    bool update(melonDS::u8* ram, melonDS::u32 ramMask, melonDS::u8* itcm, bool isHMEU,
                bool indicatorOn, bool selectorOn, melonDS::u32 keys);

    State state;

private:
    melonDS::u32 prevKeys = 0;
    int holdFrames = 0;
    melonDS::u32 holdKey = 0;
};

}

#endif // HMMINEHELPER_H
