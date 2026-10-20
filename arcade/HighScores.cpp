// G1 - high score table.  See HighScores.h.  Shoot.dll addresses in comments.
#include "arcade/HighScores.h"

#include <cstdio>
#include <cstring>

namespace rapture {

using aaron::AnimSprite;

// 0x1001cad0.  The definition is copied; rows are capped at 20; all initials start empty (-1).
CHighScoreTable::CHighScoreTable(aaron::RegKey* key, aaron::Widget* view, const HSTableDef& def, u32 lettersRes, u32 scoreRes,
                                 u32 levelRes, u32 iconRes)
    : key_(key), def_(def), lettersRes_(lettersRes), scoreRes_(scoreRes), levelRes_(levelRes), iconRes_(iconRes), view_(view) {
    rows_ = def_.rows < kMaxRows ? def_.rows : kMaxRows;
    if (rows_ < 0) rows_ = 0;
    columns_ = def_.columns;
    for (int i = 0; i < kMaxRows; ++i)
        for (int k = 0; k < 3; ++k) initials_[i][k] = -1;
}

CHighScoreTable::~CHighScoreTable() {} // 0x1001d910

// 0x1001cbbb: Load(); score of the last row
int CHighScoreTable::GetLowestScore() {
    Load();
    return rows_ > 0 ? score_[rows_ - 1] : 0;
}

// 0x1001cbe1
int CHighScoreTable::GetTopScore() {
    Load();
    return score_[0];
}

// 0x1001cbfd
int CHighScoreTable::InsertScore(int score, int level, int icon) {
    int row = rows_ - 1;
    int rank = 0;
    Load();
    if (row >= 0 && score_[row] < score) {
        while (row > -1 && score_[row] <= score) {
            if (row < rows_ - 1) { // shift row -> row + 1
                std::memcpy(initials_[row + 1], initials_[row], sizeof(initials_[row]));
                score_[row + 1] = score_[row];
                level_[row + 1] = level_[row];
                icon_[row + 1] = icon_[row];
            }
            initials_[row][0] = initials_[row][1] = initials_[row][2] = -1;
            score_[row] = score;
            level_[row] = level;
            icon_[row] = icon;
            rank = row + 1;
            row -= 1;
        }
        Save();
    }
    return rank;
}

// 0x1001d1cd.  QUIRK kept: the 0x18-byte buffer is initialised ONCE ({-1,-1,-1,0,0,0}) and reused for every row, so a
// row missing from the registry inherits the values of the previous row.  Only the columns enabled in `columns` are copied.
bool CHighScoreTable::Load() {
    int rec[6] = {-1, -1, -1, 0, 0, 0};
    aaron::RegKey sub("HighScore", key_);
    for (int r = 0; r < rows_; ++r) {
        char name[16];
        std::snprintf(name, sizeof(name), "%d", r);
        sub.GetBinary(name, rec, 0x18, nullptr);
        if (columns_ & 2)
            for (int k = 0; k < 3; ++k) initials_[r][k] = rec[k];
        if (columns_ & 1) score_[r] = rec[3];
        if (columns_ & 4) level_[r] = rec[4];
        if (columns_ & 8) icon_[r] = rec[5];
    }
    return true;
}

// 0x1001d72e.  The whole "HighScore" key is deleted first and rewritten.  The buffer starts zeroed (NOT -1) and is
// reused across rows (disabled columns keep the previous row's value).
bool CHighScoreTable::Save() {
    int rec[6] = {0, 0, 0, 0, 0, 0};
    aaron::RegKey sub("HighScore", key_);
    sub.DeleteKey();
    for (int r = 0; r < rows_; ++r) {
        if (columns_ & 2)
            for (int k = 0; k < 3; ++k) rec[k] = initials_[r][k];
        if (columns_ & 1) rec[3] = score_[r];
        if (columns_ & 4) rec[4] = level_[r];
        if (columns_ & 8) rec[5] = icon_[r];
        char name[16];
        std::snprintf(name, sizeof(name), "%d", r);
        sub.SetBinary(name, rec, 0x18);
    }
    return true;
}

// 0x1001d3a3
void CHighScoreTable::Clear() {
    for (int r = 0; r < rows_; ++r) {
        score_[r] = 0;
        level_[r] = 0;
        icon_[r] = 0;
        for (int k = 0; k < 3; ++k) initials_[r][k] = -1;
    }
    Save();
}

// 0x1001d441: rank is 1-based (the value returned by InsertScore)
void CHighScoreTable::SetInitials(const int initials[3], int rank1) {
    int row = rank1 - 1;
    if (row < 0 || row >= kMaxRows) return;
    for (int k = 0; k < 3; ++k) initials_[row][k] = initials[k];
    Save();
}

// 0x1001cd69.  Builds, under a fresh container node, for every row: 3 initials sprites, the score CDisplay, the level
// CDisplay, the icon sprite (according to `columns`).  QUIRK: resets the in-memory initials to -1 (Show reloads them).
void CHighScoreTable::CreateSprites() {
    delete container_;
    container_ = nullptr;
    for (int r = 0; r < kMaxRows; ++r) {
        iconSprites_[r] = nullptr;
        scoreDisp_[r] = levelDisp_[r] = nullptr;
        for (int k = 0; k < 3; ++k) initSprites_[r][k] = nullptr;
    }
    aaron::Widget* c = aaron::Widget::InitOrDelete(new aaron::Node(aaron::WidgetParent(view_), 0, 0));
    container_ = static_cast<aaron::Node*>(c);
    if (!container_) return;
    aaron::WidgetParent cp(static_cast<aaron::Widget*>(container_));
    for (int r = 0; r < rows_; ++r) {
        if (columns_ & 2) {
            for (int k = 0; k < 3; ++k) {
                AnimSprite* s = new AnimSprite(cp, lettersRes_, def_.initX0 + k * def_.initPitch, def_.initY0 + r * def_.rowPitch,
                                               def_.z, 0, 1);
                initSprites_[r][k] = static_cast<AnimSprite*>(aaron::Widget::InitOrDelete(s));
                initials_[r][k] = -1;
            }
        }
        if (columns_ & 4) {
            DisplayStyle st = {def_.levelDx, def_.rowPitch, def_.levelX, def_.levelY0 + r * def_.rowPitch, def_.z, 0, 1,
                               def_.levelFlags, def_.levelDigits, 0, 0};
            levelDisp_[r] = new CDisplay(cp, levelRes_, st, false);
        }
        if (columns_ & 8) {
            AnimSprite* s = new AnimSprite(cp, iconRes_, def_.iconX, def_.iconY0 + r * def_.rowPitch, def_.z, 0, 1);
            iconSprites_[r] = static_cast<AnimSprite*>(aaron::Widget::InitOrDelete(s));
        }
        if (columns_ & 1) {
            DisplayStyle st = {def_.scoreDx, def_.rowPitch, def_.scoreX, def_.scoreY0 + r * def_.rowPitch, def_.z, 0, 1,
                               def_.scoreFlags, def_.scoreDigits, 0, 0};
            scoreDisp_[r] = new CDisplay(cp, scoreRes_, st, false);
        }
    }
}

// 0x1001d47c.  show == 0: delete the container (and with it every sprite).  show != 0: create the sprites, Load, and
// fill them; rows with score 0 show nothing for score / level / icon.
void CHighScoreTable::Show(int show) {
    if (show == 0) {
        if (container_) {
            delete container_;
            container_ = nullptr;
            for (int r = 0; r < kMaxRows; ++r) {
                iconSprites_[r] = nullptr;
                scoreDisp_[r] = levelDisp_[r] = nullptr;
                for (int k = 0; k < 3; ++k) initSprites_[r][k] = nullptr;
            }
        }
        return;
    }
    CreateSprites();
    Load();
    for (int r = 0; r < rows_; ++r) {
        if (columns_ & 2) {
            for (int k = 0; k < 3; ++k) {
                AnimSprite* s = initSprites_[r][k];
                if (!s) continue;
                if (initials_[r][k] < 0) s->Show(false);
                else static_cast<moses::CDrawDib*>(s)->Animate(u32(initials_[r][k]), nullptr);
            }
        }
        if ((columns_ & 1) && scoreDisp_[r]) {
            if (score_[r] == 0) scoreDisp_[r]->Show(false);
            else scoreDisp_[r]->DisplayNumber(score_[r], 0);
        }
        if ((columns_ & 4) && levelDisp_[r]) {
            if (score_[r] == 0) levelDisp_[r]->Show(false);
            else levelDisp_[r]->DisplayNumber(level_[r], 0);
        }
        if ((columns_ & 8) && iconSprites_[r]) {
            if (score_[r] == 0) iconSprites_[r]->Show(false);
            else static_cast<moses::CDrawDib*>(iconSprites_[r])->Animate(u32(icon_[r]), nullptr);
        }
    }
}

} // namespace rapture
