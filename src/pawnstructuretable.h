#ifndef PAWNSTRUCTURETABLE_H
#define PAWNSTRUCTURETABLE_H

#include "board.h"

namespace PawnStructureTable {
struct PawnStructureEntry {
  int scores[2];
};

PawnStructureEntry *get(ZKey key);
bool exists(ZKey);
void set(ZKey zkey, PawnStructureEntry entry);
};

#endif
