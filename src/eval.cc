#include "defs.h"
#include "movegen.h"
#include "eval.h"
#include "pawnstructuretable.h"

U64 Eval::detail::FILES[8] = {FILE_A, FILE_B, FILE_C, FILE_D, FILE_E, FILE_F, FILE_G, FILE_H};
U64 Eval::detail::NEIGHBOR_FILES[8]{
    FILE_B,
    FILE_A | FILE_C,
    FILE_B | FILE_D,
    FILE_C | FILE_E,
    FILE_D | FILE_F,
    FILE_E | FILE_G,
    FILE_F | FILE_H,
    FILE_G
};
U64 Eval::detail::PASSED_PAWN_MASKS[2][64];
U64 Eval::detail::PAWN_SHIELD_MASKS[2][64];

void Eval::init() {
  // Fill king pawn shield bitboard
  for (int i = 0; i < 64; i++) {
    U64 square = ONE << i;

    detail::PAWN_SHIELD_MASKS[WHITE][i] = ((square << 8) | ((square << 7) & ~FILE_H) |
        ((square << 9) & ~FILE_A)) & RANK_2;
    detail::PAWN_SHIELD_MASKS[BLACK][i] = ((square >> 8) | ((square >> 7) & ~FILE_A) |
        ((square >> 9) & ~FILE_H)) & RANK_7;
  }

  // Calculate passed pawn masks
  U64 currSouthRay = 0x0080808080808080ULL;
  U64 currNorthRay = 0x0101010101010100ULL;
  for (int square = 0; square < 64; square++) {
    detail::PASSED_PAWN_MASKS[WHITE][square] =
        currNorthRay | ((currNorthRay << 1) & ~FILE_A) | ((currNorthRay >> 1) & ~FILE_H);
    detail::PASSED_PAWN_MASKS[BLACK][63 - square] =
        currSouthRay | ((currSouthRay) << 1 & ~FILE_H) | ((currSouthRay >> 1) & ~FILE_A);

    currSouthRay >>= 1;
    currNorthRay <<= 1;
  }
}

int Eval::getMaterialValue(PieceType pieceType) {
  return detail::MATERIAL_VALUES[OPENING][pieceType];
}

bool Eval::hasBishopPair(const Board &board, Color color) {
  return ((board.getPieces(color, BISHOP) & BLACK_SQUARES) != ZERO)
      && ((board.getPieces(color, BISHOP) & WHITE_SQUARES) != ZERO);
}

int Eval::evaluateMobility(const Board &board, Color color, GamePhase phase) {
  int score = 0;

  // Special case for pawn moves
  U64 pawns = board.getPieces(color, PAWN);
  U64 singlePawnPushes, doublePawnPushes, pawnAttacks;
  if (color == WHITE) {
    singlePawnPushes = (pawns << 8) & board.getNotOccupied();
    doublePawnPushes = ((singlePawnPushes & RANK_3) << 8) & board.getNotOccupied();
    pawnAttacks = ((pawns << 7) & ~FILE_H) | ((pawns << 9) & ~FILE_A);
  } else {
    singlePawnPushes = (pawns >> 8) & board.getNotOccupied();
    doublePawnPushes = ((singlePawnPushes & RANK_6) >> 8) & board.getNotOccupied();
    pawnAttacks = ((pawns >> 7) & ~FILE_A) | ((pawns >> 9) & ~FILE_H);
  }
  pawnAttacks &= board.getAttackable(getOppositeColor(color));
  score += _popCount(singlePawnPushes | doublePawnPushes | pawnAttacks) * MOBILITY_BONUS[phase][color];

  // All other pieces
  for (auto pieceType : {ROOK, KNIGHT, BISHOP, QUEEN, KING}) {
    U64 pieces = board.getPieces(color, pieceType);

    while (pieces) {
      int square = _popLsb(pieces);
      U64 attackBitBoard = board.getAttacksForSquare(pieceType, color, square);
      score += _popCount(attackBitBoard) * MOBILITY_BONUS[phase][pieceType];
    }
  }

  return score;
}

int Eval::rooksOnOpenFiles(const Board &board, Color color) {
  int numRooks = 0;

  for (U64 file : detail::FILES) {
    if ((file & board.getPieces(color, ROOK))
        && (file & board.getPieces(color, ROOK)) == (file & board.getOccupied())) {
      numRooks++;
    }
  }
  return numRooks;
}

int Eval::passedPawns(const Board &board, Color color) {
  int passed = 0;
  U64 pawns = board.getPieces(color, PAWN);

  while (pawns != ZERO) {
    int square = _popLsb(pawns);
    if ((board.getPieces(getOppositeColor(color), PAWN) & detail::PASSED_PAWN_MASKS[color][square]) == ZERO) passed++;
    pawns &= ~detail::FILES[square % 8];
  }

  return passed;
}

int Eval::doubledPawns(const Board &board, Color color) {
  int doubled = 0;

  for (U64 file : detail::FILES) {
    int pawnsOnFile = _popCount(file & board.getPieces(color, PAWN));

    if (pawnsOnFile > 1) doubled += (pawnsOnFile - 1);
  }

  return doubled;
}

int Eval::isolatedPawns(const Board &board, Color color) {
  int isolated = 0;

  U64 pawns = board.getPieces(color, PAWN);

  for (int fileNumber = 0; fileNumber < 8; fileNumber++) {
    U64 fileMask = detail::FILES[fileNumber];
    U64 neighborFileMask = detail::NEIGHBOR_FILES[fileNumber];

    if ((fileMask & pawns) && !(neighborFileMask & pawns)) isolated++;
  }

  return isolated;
}

int Eval::pawnsShieldingKing(const Board &board, Color color) {
  int kingSquare = _bitscanForward(board.getPieces(color, KING));
  return _popCount(detail::PAWN_SHIELD_MASKS[color][kingSquare] & board.getPieces(color, PAWN));
}

int Eval::evaluatePawnStructure(const Board &board, Color color, GamePhase phase) {
  if (PawnStructureTable::exists(board.getPawnStructureZKey())) {
    int whiteScore = PawnStructureTable::get(board.getPawnStructureZKey())->scores[phase];

    if (color == BLACK) {
      return -whiteScore;
    } else {
      return whiteScore;
    }
  }

  int scores[2] = {0};

  // Passed pawns
  int passedPawnDiff = passedPawns(board, WHITE) - passedPawns(board, BLACK);
  scores[OPENING] += PASSED_PAWN_BONUS[OPENING] * passedPawnDiff;
  scores[ENDGAME] += PASSED_PAWN_BONUS[ENDGAME] * passedPawnDiff;

  // Doubled pawns
  int doubledPawnDiff = doubledPawns(board, WHITE) - doubledPawns(board, BLACK);
  scores[OPENING] += DOUBLED_PAWN_PENALTY[OPENING] * doubledPawnDiff;
  scores[ENDGAME] += DOUBLED_PAWN_PENALTY[ENDGAME] * doubledPawnDiff;

  // Isolated pawns
  int isolatedPawnDiff = isolatedPawns(board, WHITE) - isolatedPawns(board, BLACK);
  scores[OPENING] += ISOLATED_PAWN_PENALTY[OPENING] * isolatedPawnDiff;
  scores[ENDGAME] += ISOLATED_PAWN_PENALTY[ENDGAME] * isolatedPawnDiff;

  PawnStructureTable::PawnStructureEntry entry{};
  entry.scores[OPENING] = scores[OPENING];
  entry.scores[ENDGAME] = scores[ENDGAME];

  PawnStructureTable::set(board.getPawnStructureZKey(), entry);

  if (color == BLACK) {
    return -scores[phase];
  } else {
    return scores[phase];
  }
}

template<GamePhase phase>
int Eval::evaluateForPhase(const Board &board, Color color) {
  int score = 0;

  Color otherColor = getOppositeColor(color);

  // Material value
  score += detail::MATERIAL_VALUES[phase][PAWN]
      * (_popCount(board.getPieces(color, PAWN)) - _popCount(board.getPieces(otherColor, PAWN)));
  score += detail::MATERIAL_VALUES[phase][KNIGHT]
      * (_popCount(board.getPieces(color, KNIGHT)) - _popCount(board.getPieces(otherColor, KNIGHT)));
  score += detail::MATERIAL_VALUES[phase][BISHOP]
      * (_popCount(board.getPieces(color, BISHOP)) - _popCount(board.getPieces(otherColor, BISHOP)));
  score += detail::MATERIAL_VALUES[phase][ROOK]
      * (_popCount(board.getPieces(color, ROOK)) - _popCount(board.getPieces(otherColor, ROOK)));
  score += detail::MATERIAL_VALUES[phase][QUEEN]
      * (_popCount(board.getPieces(color, QUEEN)) - _popCount(board.getPieces(otherColor, QUEEN)));

  // Piece square tables
  score += (board.getPSquareTable().getScore(phase, color) - board.getPSquareTable().getScore(phase, otherColor));

  // Mobility
  score += evaluateMobility(board, color, phase);

  // Rook on open file
  score += ROOK_OPEN_FILE_BONUS[phase] * (rooksOnOpenFiles(board, color) - rooksOnOpenFiles(board, otherColor));

  // Bishop pair
  score += hasBishopPair(board, color) ? BISHOP_PAIR_BONUS[phase] : 0;
  score -= hasBishopPair(board, otherColor) ? BISHOP_PAIR_BONUS[phase] : 0;

  // Pawn structure
  score += evaluatePawnStructure(board, color, phase);

  // King pawn shield (not done in endgame so omit)
  if (phase == OPENING) {
    score +=
        KING_PAWN_SHIELD_BONUS[OPENING] * (pawnsShieldingKing(board, color) - pawnsShieldingKing(board, otherColor));
  }

  return score;
}

int Eval::getPhase(const Board &board) {
  static const int totalPhase = 24;
  int phase = totalPhase / 2;

  for (auto pieceType : {ROOK, KNIGHT, BISHOP, QUEEN}) {
    phase -= _popCount(board.getPieces(WHITE, pieceType)) * detail::PHASE_WEIGHTS[pieceType];
    phase -= _popCount(board.getPieces(BLACK, pieceType)) * detail::PHASE_WEIGHTS[pieceType];
  }

  return ((phase * 256) + (totalPhase / 2)) / totalPhase;
}

int Eval::evaluate(const Board &board, Color color) {
  int phase = getPhase(board);
  return ((evaluateForPhase<OPENING>(board, color) * (256 - phase)) + (evaluateForPhase<ENDGAME>(board, color) * phase))
      / 256;
}
