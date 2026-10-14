#include <Race/DrinkTracker.hpp>

#include <MarioKartWii/Item/Obj/ItemEVENT.hpp>
#include <MarioKartWii/Kart/KartManager.hpp>
#include <MarioKartWii/RKNet/EVENT.hpp>
#include <MarioKartWii/Race/RaceData.hpp>
#include <MarioKartWii/Race/RaceInfo/RaceInfo.hpp>

namespace Pulsar {
namespace Race {

namespace {

struct EventStamp {
    bool initialized;
    u8 state;
    u8 itemObjId;
    u8 action;
    u16 time;
    u8 data0;
    u8 data1;
    u8 data2;
};

DrinkRaceStats stats;
EventStamp eventStamps[13][24];  // row 0 = send queue, rows 1-12 = received AIDs

bool eventsPrimed = false;
bool wasOob = false;
u8 pendingShockFrames = 0;
bool shockObserved = false;
u32 lastRaceFrame = 0xffffffff;

static u8 FindLocalPlayerId() {
    // Prefer the kart status flag. This is more reliable than assuming HUD slot 0
    // always maps directly to the player index in every race type.
    Kart::Manager *manager = Kart::Manager::sInstance;
    if (manager != nullptr) {
        for (u32 i = 0; i < manager->playerCount; ++i) {
            if (manager->players[i] == nullptr)
                continue;

            Kart::Status *status = manager->players[i]->pointers.kartStatus;
            if (status != nullptr && (status->bitfield4 & 0x2) != 0)
                return static_cast<u8>(i);  // real local player
        }
    }

    // Fallback for the short window before Kart::Status is available.
    const Racedata *racedata = Racedata::sInstance;
    if (racedata == nullptr || racedata->racesScenario.localPlayerCount == 0)
        return 0xff;

    const u8 playerId = racedata->racesScenario.settings.hudPlayerIds[0];
    if (playerId >= racedata->racesScenario.playerCount)
        return 0xff;
    return playerId;
}

static Kart::Status *GetLocalStatus() {
    const u8 playerId = FindLocalPlayerId();
    Kart::Manager *manager = Kart::Manager::sInstance;

    if (manager == nullptr || playerId == 0xff || playerId >= manager->playerCount)
        return nullptr;
    if (manager->players[playerId] == nullptr)
        return nullptr;

    return manager->players[playerId]->pointers.kartStatus;
}

static void CopyStamp(EventStamp &stamp, const RKNet::EVENTEntry &entry) {
    stamp.initialized = true;
    stamp.state = entry.state;
    stamp.itemObjId = entry.itemObjId;
    stamp.action = entry.action;
    stamp.time = entry.time;
    stamp.data0 = entry.data[0];
    stamp.data1 = entry.data[1];
    stamp.data2 = entry.data[2];
}

static bool IsSameStamp(const EventStamp &stamp, const RKNet::EVENTEntry &entry) {
    return stamp.initialized
      && stamp.state == entry.state
      && stamp.itemObjId == entry.itemObjId
      && stamp.action == entry.action
      && stamp.time == entry.time
      && stamp.data0 == entry.data[0]
      && stamp.data1 == entry.data[1]
      && stamp.data2 == entry.data[2];
}

static void PrimeEvents() {
    RKNet::EVENTHandler *handler = RKNet::EVENTHandler::sInstance;
    if (handler == nullptr)
        return;

    for (u32 i = 0; i < 24; ++i)
        CopyStamp(eventStamps[0][i], handler->toSendEntries[i]);

    for (u32 aid = 0; aid < 12; ++aid) {
        for (u32 i = 0; i < 24; ++i)
            CopyStamp(eventStamps[aid + 1][i], handler->receivedEntries[aid][i]);
    }

    eventsPrimed = true;
}

static void ProcessEvent(const RKNet::EVENTEntry &entry) {
    if (entry.state != RKNet::EVENTENTRYSTATE_FULL)
        return;

    const u8 localPlayerId = FindLocalPlayerId();
    if (localPlayerId == 0xff)
        return;

    if (entry.action == RKNet::EVENTACTION_HITFREE) {
        // HitFreeEntry packs the collision player in the upper nibble.
        const u8 hitPlayerId = (entry.data[2] >> 4) & 0x0f;
        if (hitPlayerId != localPlayerId)
            return;

        if (entry.itemObjId == OBJ_BLUE_SHELL)
            ++stats.blueHits;
        else if (entry.itemObjId == OBJ_BOBOMB)
            ++stats.bombHits;

        return;
    }

    if (entry.action == RKNet::EVENTACTION_USE && entry.itemObjId == OBJ_LIGHTNING) {
        const u8 shockUserId = entry.data[0];

        // Using your own shock is not a dodge.
        if (shockUserId == localPlayerId || shockUserId >= 12)
            return;

        pendingShockFrames = 8;
        shockObserved = false;

        Kart::Status *status = GetLocalStatus();
        if (status != nullptr && (status->bitfield2 & 0x80) != 0)
            shockObserved = true;
    }
}

static void ScanEntry(const RKNet::EVENTEntry &entry, EventStamp &stamp) {
    if (IsSameStamp(stamp, entry))
        return;

    CopyStamp(stamp, entry);
    ProcessEvent(entry);
}

static void ScanEvents() {
    RKNet::EVENTHandler *handler = RKNet::EVENTHandler::sInstance;
    if (handler == nullptr)
        return;

    if (!eventsPrimed) {
        PrimeEvents();
        return;
    }

    for (u32 i = 0; i < 24; ++i)
        ScanEntry(handler->toSendEntries[i], eventStamps[0][i]);

    for (u32 aid = 0; aid < 12; ++aid) {
        for (u32 i = 0; i < 24; ++i)
            ScanEntry(handler->receivedEntries[aid][i], eventStamps[aid + 1][i]);
    }
}

}  // namespace

void ResetDrinkTracker() {
    stats.finishPosition = 0;
    stats.blueHits = 0;
    stats.bombHits = 0;
    stats.falls = 0;
    stats.shockDodges = 0;

    wasOob = false;
    pendingShockFrames = 0;
    shockObserved = false;
    lastRaceFrame = 0xffffffff;
    eventsPrimed = false;

    for (u32 row = 0; row < 13; ++row) {
        for (u32 i = 0; i < 24; ++i)
            eventStamps[row][i].initialized = false;
    }

    PrimeEvents();
}

void UpdateDrinkTracker() {
    Raceinfo *raceInfo = Raceinfo::sInstance;
    Racedata *racedata = Racedata::sInstance;

    if (raceInfo == nullptr || racedata == nullptr)
        return;
    if (raceInfo->stage < RACESTAGE_RACE)
        return;

    // Protect against accidental multiple updates in one game frame.
    if (lastRaceFrame == raceInfo->raceFrames)
        return;
    lastRaceFrame = raceInfo->raceFrames;

    const u8 localPlayerId = FindLocalPlayerId();
    if (localPlayerId == 0xff || localPlayerId >= racedata->racesScenario.playerCount)
        return;

    Kart::Status *status = GetLocalStatus();

    // Count the transition into OOB instead of relying on a one-frame respawn bit.
    // Status::bitfield0 0x10 is "OOB, before being respawned".
    if (status != nullptr) {
        const bool isOob = (status->bitfield0 & 0x10) != 0;
        if (isOob && !wasOob)
            ++stats.falls;
        wasOob = isOob;
    } else {
        wasOob = false;
    }

    ScanEvents();

    // Give the lightning event a few frames to apply its shocked state.
    if (pendingShockFrames > 0 && status != nullptr) {
        if ((status->bitfield2 & 0x80) != 0)
            shockObserved = true;

        --pendingShockFrames;
        if (pendingShockFrames == 0 && !shockObserved)
            ++stats.shockDodges;
    }

    if (stats.finishPosition == 0) {
        u8 finishPos = racedata->racesScenario.players[localPlayerId].finishPos;

        // finishPos can lag by a frame in some modes. Once the race is finishing,
        // RaceinfoPlayer::position is a safe fallback for the local finisher.
        if (finishPos == 0 && raceInfo->stage >= RACESTAGE_IS_FINISHING
          && raceInfo->players != nullptr && raceInfo->players[localPlayerId] != nullptr) {
            finishPos = raceInfo->players[localPlayerId]->position;
        }

        if (finishPos > 0)
            stats.finishPosition = finishPos;
    }
}

u8 GetDrinkPlayerId() {
    return FindLocalPlayerId();
}

const DrinkRaceStats &GetDrinkRaceStats() {
    return stats;
}

// Tracking must not depend on whether the HUD control happens to be active.
// Reset at race load and sample once per race frame.
static RaceLoadHook DRINK_TRACKER_RESET_HOOK(ResetDrinkTracker);
static RaceFrameHook DRINK_TRACKER_UPDATE_HOOK(UpdateDrinkTracker);

}  // namespace Race
}  // namespace Pulsar
