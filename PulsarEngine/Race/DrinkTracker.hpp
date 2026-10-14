#ifndef _PULSAR_DRINK_TRACKER_
#define _PULSAR_DRINK_TRACKER_

#include <kamek.hpp>

namespace Pulsar {
namespace Race {

namespace DrinkRules {
static const int PLACEMENT_MULTIPLIER = 1;
static const int BLUE_PENALTY = 1;
static const int BOMB_PENALTY = 1;
static const int FALL_PENALTY = 1;
static const int SHOCK_DODGE_REWARD = 2;
static const bool CLAMP_TOTAL_TO_ZERO = true;
}  // namespace DrinkRules

struct DrinkRaceStats {
    int finishPosition;
    int blueHits;
    int bombHits;
    int falls;
    int shockDodges;

    int GetPlacementPoints() const {
        if (finishPosition <= 0)
            return 0;
        return (finishPosition - 1) * DrinkRules::PLACEMENT_MULTIPLIER;
    }

    int GetRawTotal() const {
        return GetPlacementPoints()
          + blueHits * DrinkRules::BLUE_PENALTY
          + bombHits * DrinkRules::BOMB_PENALTY
          + falls * DrinkRules::FALL_PENALTY
          - shockDodges * DrinkRules::SHOCK_DODGE_REWARD;
    }

    int GetTotal() const {
        const int total = GetRawTotal();
        if (DrinkRules::CLAMP_TOTAL_TO_ZERO && total < 0)
            return 0;
        return total;
    }
};

void ResetDrinkTracker();
void UpdateDrinkTracker();
void SetDrinkFinishPosition(u8 finishPosition);
u8 GetDrinkPlayerId();
const DrinkRaceStats &GetDrinkRaceStats();

}  // namespace Race
}  // namespace Pulsar

#endif
