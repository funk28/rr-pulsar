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
    void SetPanePosition() override;
    bool IsInactive() override;
    bool HasStarted() override;

private:
    void Load();
    wchar_t textBuffer[0x100];
    float originalTextX;
    float originalTextY;
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

    // Reuse Retro Rewind's existing CTInfo text banner, so no custom BRLYT is required.
    ControlLoader loader(this);
    loader.Load("game_image", "CTInfo", "CTInfo", nullptr);
    this->textBox_00 = this->layout.GetPaneByName("TextBox_00");

    this->originalTextX = 0.0f;
    this->originalTextY = 0.0f;
    if (this->textBox_00 != nullptr) {
        this->originalTextX = this->textBox_00->trans.x;
        this->originalTextY = this->textBox_00->trans.y;
        this->textBox_00->scale.x = 0.72f;
        this->textBox_00->scale.z = 0.72f;
    }

    this->textBuffer[0] = L'\0';
    Text::Info info;
    info.strings[0] = this->textBuffer;
    this->SetMessage(BMG_TEXT, &info);
}

void CtrlRaceDrinkCounter::SetPanePosition() {
    CtrlRaceWifiStartMessage::SetPanePosition();

    if (this->textBox_00 != nullptr) {
        // CTInfo normally sits directly on top of the position HUD. Shift this
        // compact counter toward the lower-middle of the screen instead.
        this->textBox_00->trans.x = this->originalTextX + 235.0f;
        this->textBox_00->trans.y = this->originalTextY + 38.0f;
        this->textBox_00->scale.x = 0.72f;
        this->textBox_00->scale.z = 0.72f;
    }
}

bool CtrlRaceDrinkCounter::HasStarted() {
    Raceinfo *raceInfo = Raceinfo::sInstance;
    return raceInfo != nullptr
      && raceInfo->timerMgr != nullptr
      && raceInfo->timerMgr->hasRaceStarted;
}

bool CtrlRaceDrinkCounter::IsInactive() {
    if (CtrlRaceBase::IsInactive())
        return true;

    Raceinfo *raceInfo = Raceinfo::sInstance;
    if (raceInfo == nullptr || raceInfo->timerMgr == nullptr)
        return true;

    return !raceInfo->timerMgr->hasRaceStarted;
}

void CtrlRaceDrinkCounter::OnUpdate() {
    // Let the native control handle its normal HUD positioning/animation first.
    CtrlRaceWifiStartMessage::OnUpdate();

    const Race::DrinkRaceStats &stats = Race::GetDrinkRaceStats();
    const int total = stats.GetTotal();

    if (stats.finishPosition > 0) {
        swprintf(
          this->textBuffer,
          sizeof(this->textBuffer) / sizeof(this->textBuffer[0]),
          L"Drink pts %d   Place +%d   B +%d   Bomb +%d   Fall +%d   Shock -%d",
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
          L"Drink pts %d   B +%d   Bomb +%d   Fall +%d   Shock -%d",
          total,
          stats.blueHits * Race::DrinkRules::BLUE_PENALTY,
          stats.bombHits * Race::DrinkRules::BOMB_PENALTY,
          stats.falls * Race::DrinkRules::FALL_PENALTY,
          stats.shockDodges * Race::DrinkRules::SHOCK_DODGE_REWARD);
    }

    Text::Info info;
    info.strings[0] = this->textBuffer;
    this->SetMessage(BMG_TEXT, &info);
}

}  // namespace UI
}  // namespace Pulsar
