#include <PulsarSystem.hpp>
#include <Race/DrinkTracker.hpp>
#include <UI/CtrlRaceBase/CustomCtrlRaceBase.hpp>

#include <MarioKartWii/Race/RaceInfo/RaceInfo.hpp>
#include <MarioKartWii/UI/Ctrl/CtrlRace/CtrlRaceWifi.hpp>

namespace Pulsar {
namespace UI {

class CtrlRaceDrinkCounter : public CtrlRaceWifiStartMessage {
public:
    static u32 Count();
    static void Create(Page &page, u32 index, u32 count);

    void OnUpdate() override;
    bool IsInactive() override;
    bool HasStarted() override;

private:
    void Load();
    wchar_t textBuffer[0x100];
};

u32 CtrlRaceDrinkCounter::Count() {
    if (Racedata::sInstance == nullptr)
        return 0;

    const RacedataScenario &scenario = Racedata::sInstance->racesScenario;
    if (scenario.localPlayerCount != 1)
        return 0;

    const GameMode mode = scenario.settings.gamemode;
    if (mode != MODE_GRAND_PRIX && mode != MODE_VS_RACE
      && mode != MODE_PRIVATE_VS && mode != MODE_PUBLIC_VS)
        return 0;

    return 1;
}

void CtrlRaceDrinkCounter::Create(Page &page, u32 index, u32) {
    CtrlRaceDrinkCounter *counter = new (CtrlRaceDrinkCounter);
    page.AddControl(index, *counter, 0);
    counter->Load();
}

static CustomCtrlBuilder DRINKCOUNTER(CtrlRaceDrinkCounter::Count, CtrlRaceDrinkCounter::Create);

void CtrlRaceDrinkCounter::Load() {
    this->hudSlotId = 0;

    ControlLoader loader(this);
    loader.Load("game_image", "CTInfo", "CTInfo", nullptr);
    this->textBox_00 = this->layout.GetPaneByName("TextBox_00");

    Race::ResetDrinkTracker();

    this->textBuffer[0] = L'\0';
    Text::Info info;
    info.strings[0] = this->textBuffer;
    this->SetMessage(BMG_TEXT, &info);
}

bool CtrlRaceDrinkCounter::HasStarted() {
    Raceinfo *raceInfo = Raceinfo::sInstance;
    return raceInfo != nullptr
      && raceInfo->timerMgr != nullptr
      && raceInfo->timerMgr->hasRaceStarted;
}

bool CtrlRaceDrinkCounter::IsInactive() {
    Raceinfo *raceInfo = Raceinfo::sInstance;

    // The race HUD/page can be reused between races, so Load() is not a reliable
    // race boundary. During intro/countdown, explicitly clear any completed-race
    // values before the control becomes visible again.
    if (raceInfo != nullptr && raceInfo->stage < RACESTAGE_RACE) {
        const Race::DrinkRaceStats &stats = Race::GetDrinkRaceStats();
        if (stats.finishPosition != 0 || stats.blueHits != 0 || stats.bombHits != 0
          || stats.falls != 0 || stats.shockDodges != 0) {
            Race::ResetDrinkTracker();
        }
    } else {
        Race::UpdateDrinkTracker();
    }

    if (CtrlRaceBase::IsInactive())
        return true;

    if (raceInfo == nullptr || raceInfo->timerMgr == nullptr)
        return true;

    return !raceInfo->timerMgr->hasRaceStarted;
}

void CtrlRaceDrinkCounter::OnUpdate() {
    Race::UpdateDrinkTracker();

    const Race::DrinkRaceStats &stats = Race::GetDrinkRaceStats();
    const int total = stats.GetTotal();

    if (stats.finishPosition > 0) {
        swprintf(
          this->textBuffer,
          sizeof(this->textBuffer) / sizeof(this->textBuffer[0]),
          L"DRINK POINTS: %d\nPlace +%d  Blue +%d  Bomb +%d  Falls +%d  Shock -%d",
          total,
          stats.GetPlacementPoints(),
          stats.blueHits * Race::DrinkRules::BLUE_PENALTY,
          stats.bombHits * Race::DrinkRules::BOMB_PENALTY,
          stats.falls * Race::DrinkRules::FALL_PENALTY,
          stats.shockDodges * Race::DrinkRules::SHOCK_DODGE_REWARD);
    } else {
        swprintf(
          this->textBuffer,
          sizeof(this->textBuffer) / sizeof(this->textBuffer[0]),
          L"Drink points: %d\nBlue +%d  Bomb +%d  Falls +%d  Shock -%d",
          total,
          stats.blueHits * Race::DrinkRules::BLUE_PENALTY,
          stats.bombHits * Race::DrinkRules::BOMB_PENALTY,
          stats.falls * Race::DrinkRules::FALL_PENALTY,
          stats.shockDodges * Race::DrinkRules::SHOCK_DODGE_REWARD);
    }

    Text::Info info;
    info.strings[0] = this->textBuffer;
    this->SetMessage(BMG_TEXT, &info);

    CtrlRaceWifiStartMessage::OnUpdate();
}

}  // namespace UI
}  // namespace Pulsar
