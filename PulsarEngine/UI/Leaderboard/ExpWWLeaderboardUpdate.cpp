#include <UI/Leaderboard/ExpWWLeaderboardUpdate.hpp>
#include <UI/Leaderboard/LeaderboardDisplay.hpp>

namespace Pulsar {
namespace UI {

void ExpWWLeaderboardUpdate::BeforeEntranceAnimations() {
    setLeaderboardDisplayType(LEADERBOARD_DISPLAY_NAMES);
    fillLeaderboardResults(GetRowCount(), this->results);
}

void ExpWWLeaderboardUpdate::OnUpdate() {
    if (checkLeaderboardDisplaySwapInputs()) {
        nextLeaderboardDisplayType();
        fillLeaderboardResults(GetRowCount(), this->results);
    }
}

}  // namespace UI
}  // namespace Pulsar
