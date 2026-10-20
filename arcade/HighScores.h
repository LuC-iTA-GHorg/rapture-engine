// G1 - high score table.  Shoot.dll 0x1001cad0-0x1001d72e (name hypothetical: CHighScoreTable);
// Shoot.exe twin: CHighScores 0x40d0b0 (Load 0x40d7ad, Save 0x40dd0e, Clear 0x40d983, Insert 0x40d1dd, SetName 0x40da21).
//
// Persistence: sub key "HighScore" of the machine key; one BINARY value of 0x18 bytes per row, named "0", "1", ...
//     struct HSREC { i32 initials[3]; i32 score; i32 level; i32 icon; }     (initials: -1 = empty slot, else frame index
//                                                                            of the letters group)
// Rows are sorted by score, best first.  `icon` is the difficulty (0..2) in Shoot.dll.
#pragma once

#include "aaron/RegKey.h"
#include "aaron/Widget.h"
#include "arcade/ArcadeTypes.h"
#include "arcade/Display.h"

namespace rapture {

class CHighScoreTable {
public:
    static constexpr int kMaxRows = 20;

    // Shoot.dll 0x1001cad0 (machineKey, view, definition, lettersRes, scoreDigitsRes, levelDigitsRes, iconRes).
    // `key` is borrowed (the scene's machine key); `view` is the widget the display hangs from (Scene::RootWidget()).
    CHighScoreTable(aaron::RegKey* key, aaron::Widget* view, const HSTableDef& def, u32 lettersRes, u32 scoreRes,
                    u32 levelRes, u32 iconRes);
    virtual ~CHighScoreTable();                                     // 0x1001d910 (does not even free the sprites: they hang from the node)

    int GetLowestScore();                                           // 0x1001cbbb  (Load first)
    int GetTopScore();                                              // 0x1001cbe1  (Load first)
    // 0x1001cbfd.  Returns the 1-based rank (0 = not inserted).  Reloads from the registry first and saves after
    // inserting.  QUIRK: ties go ABOVE older equal scores (comparison <=); a score equal to the last row does not enter.
    int InsertScore(int score, int level, int icon);
    bool Load();                                                    // 0x1001d1cd
    bool Save();                                                    // 0x1001d72e
    void Clear();                                                   // 0x1001d3a3
    void SetInitials(const int initials[3], int rank1);             // 0x1001d441 (rank 1-based) + Save
    void Show(int show);                                            // 0x1001d47c (1 = build + fill the sprites, 0 = delete them)

    // read access to the rows (tests, dialogs)
    int Rows() const { return rows_; }
    int Score(int row) const { return score_[row]; }
    int Level(int row) const { return level_[row]; }
    int Icon(int row) const { return icon_[row]; }
    const int* Initials(int row) const { return initials_[row]; }
    u32 Columns() const { return columns_; }

private:
    void CreateSprites();                                           // 0x1001cd69

    aaron::RegKey* key_ = nullptr;                                  // +0x104
    int rows_ = 0;                                                  // +0x108 (<= 20)
    HSTableDef def_;                                                // +0x108..+0x150
    u32 columns_ = 0;                                               // +0x114
    u32 lettersRes_ = 0, scoreRes_ = 0, levelRes_ = 0, iconRes_ = 0; // +0x154 +0x158 +0x15c +0x160
    int icon_[kMaxRows] = {};                                       // +0x164
    int level_[kMaxRows] = {};                                      // +0x1b4
    int initials_[kMaxRows][3];                                     // +0x204
    int score_[kMaxRows] = {};                                      // +0x2f4
    aaron::Widget* view_ = nullptr;                                 // +0x344
    aaron::Node* container_ = nullptr;                              // +0x348
    aaron::AnimSprite* iconSprites_[kMaxRows] = {};                 // +0x34c
    aaron::AnimSprite* initSprites_[kMaxRows][3] = {};              // +0x39c
    CDisplay* scoreDisp_[kMaxRows] = {};                            // +0x48c
    CDisplay* levelDisp_[kMaxRows] = {};                            // +0x4dc
};

} // namespace rapture
