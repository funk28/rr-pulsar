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
    void SyncRaceBoundary();
    void ApplySafePosition();

    wchar_t textBuffer[0x100];
    bool wasRaceStarted;
    u32 lastRaceTimerFrame;
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

    ControlLoader loader(this);
    loader.Load("game_image", "CTInfo", "CTInfo", nullptr);
    this->textBox_00 = this->layout.GetPaneByName("TextBox_00");

    this->originalTextX = 0.0f;
    this->originalTextY = 0.0f;
    if (this->textBox_00 != nullptr) {
        this->originalTextX = this->textBox_00->trans.x;
        this->originalTextY = this->textBox_00->trans.y;
    }

    this->wasRaceStarted = false;
    this->lastRaceTimerFrame = 0xffffffff;
    Race::ResetDrinkTracker();

    this->textBuffer[0] = L'\0';
    Text::Info info;
    info.strings[0] = this->textBuffer;
    this->SetMessage(BMG_TEXT, &info);
}

void CtrlRaceDrinkCounter::SyncRaceBoundary() {
    Raceinfo *raceInfo = Raceinfo::sInstance;
    if (raceInfo == nullptr || raceInfo->timerMgr == nullptr) {
        this->wasRaceStarted = false;
        this->lastRaceTimerFrame = 0xffffffff;
        return;
    }

    const bool started = raceInfo->timerMgr->hasRaceStarted;
    const u32 timerFrame = raceInfo->timerMgr->raceFrameCounter;
    const bool timerRolledBack = this->lastRaceTimerFrame != 0xffffffff
      && timerFrame < this->lastRaceTimerFrame;

    if ((started && !this->wasRaceStarted) || timerRolledBack)
        Race::ResetDrinkTracker();

    this->wasRaceStarted = started;
    this->lastRaceTimerFrame = timerFrame;
}

void CtrlRaceDrinkCounter::ApplySafePosition() {
    if (this->textBox_00 == nullptr)
        return;

    // Keep the counter above the lower-left position HUD, but leave enough
    // room from the screen edge that the whole label stays visible.
    this->textBox_00->trans.x = this->originalTextX - 55.0f;
    this->textBox_00->trans.y = this->originalTextY + 95.0f;
}

bool CtrlRaceDrinkCounter::HasStarted() {
    Raceinfo *raceInfo = Raceinfo::sInstance;
    return raceInfo != nullptr
      && raceInfo->timerMgr != nullptr
      && raceInfo->timerMgr->hasRaceStarted;
}

bool CtrlRaceDrinkCounter::IsInactive() {
    this->SyncRaceBoundary();

    if (CtrlRaceBase::IsInactive())
        return true;

    Raceinfo *raceInfo = Raceinfo::sInstance;
    if (raceInfo == nullptr || raceInfo->timerMgr == nullptr)
        return true;

    if (!raceInfo->timerMgr->hasRaceStarted)
        return true;

    Race::UpdateDrinkTracker();
    return false;
}

void CtrlRaceDrinkCounter::OnUpdate() {
    this->SyncRaceBoundary();
    Race::UpdateDrinkTracker();

    const Race::DrinkRaceStats &stats = Race::GetDrinkRaceStats();

    swprintf(
      this->textBuffer,
      sizeof(this->textBuffer) / sizeof(this->textBuffer[0]),
      L"SIPS: %d",
      stats.GetTotal());

    Text::Info info;
    info.strings[0] = this->textBuffer;
    this->SetMessage(BMG_TEXT, &info);

    CtrlRaceWifiStartMessage::OnUpdate();
    this->ApplySafePosition();
}

}  // namespace UI
}  // namespace Pulsar
