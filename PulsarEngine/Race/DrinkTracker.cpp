#include <Race/DrinkTracker.hpp>

#include <MarioKartWii/Item/ItemManager.hpp>
#include <MarioKartWii/Item/ItemPlayer.hpp>
#include <MarioKartWii/Item/Obj/Bomb.hpp>
#include <MarioKartWii/Item/Obj/ItemEVENT.hpp>
#include <MarioKartWii/Item/Obj/KouraTogezo.hpp>
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

struct ObjectWatch {
    const Item::Obj *object;
    u16 eventBitfield;
    bool counted;
};

DrinkRaceStats stats;
EventStamp eventStamps[13][24];
ObjectWatch blueWatches[32];
ObjectWatch bombWatches[32];
bool shockStateInitialized[12];
bool wasShocked[12];

bool eventsPrimed = false;
bool wasOob = false;
u8 pendingShockFrames = 0;
bool shockObserved = false;
u8 localShockUseFrames = 0;
ItemId previousLocalInventoryItem = ITEM_NONE;
u32 lastRaceFrame = 0xffffffff;
u8 trackedPlayerId = 0xff;
Raceinfo *trackedRaceInfo = nullptr;

static u8 ResolveCurrentLocalPlayerId() {
    Racedata *racedata = Racedata::sInstance;
    if (racedata == nullptr || racedata->racesScenario.localPlayerCount == 0)
        return 0xff;

    const u8 playerId = racedata->GetPlayerIdOfLocalPlayer(0);
    if (playerId < racedata->racesScenario.playerCount && playerId < 12)
        return playerId;

    const u8 hudPlayerId = racedata->racesScenario.settings.hudPlayerIds[0];
    if (hudPlayerId < racedata->racesScenario.playerCount && hudPlayerId < 12)
        return hudPlayerId;

    return 0xff;
}

static u8 GetLocalPlayerId() {
    if (trackedPlayerId < 12)
        return trackedPlayerId;

    trackedPlayerId = ResolveCurrentLocalPlayerId();
    return trackedPlayerId;
}

static Kart::Status *GetStatusForPlayer(u8 playerId) {
    Kart::Manager *manager = Kart::Manager::sInstance;

    if (manager == nullptr || manager->players == nullptr
      || playerId == 0xff || playerId >= manager->playerCount || playerId >= 12)
        return nullptr;
    if (manager->players[playerId] == nullptr)
        return nullptr;

    return manager->players[playerId]->pointers.kartStatus;
}

static Kart::Status *GetLocalStatus() {
    return GetStatusForPlayer(GetLocalPlayerId());
}

static void BeginShockCheck() {
    if (pendingShockFrames != 0 || localShockUseFrames != 0)
        return;

    pendingShockFrames = 10;
    shockObserved = false;

    Kart::Status *status = GetLocalStatus();
    if (status != nullptr && (status->bitfield2 & 0x80) != 0)
        shockObserved = true;
}

static void UpdateLocalShockUseState(u8 localPlayerId) {
    Item::Manager *manager = Item::Manager::sInstance;
    if (manager == nullptr || manager->players == nullptr || localPlayerId >= manager->playerCount) {
        previousLocalInventoryItem = ITEM_NONE;
        if (localShockUseFrames > 0)
            --localShockUseFrames;
        return;
    }

    Item::Player *itemPlayer = &manager->players[localPlayerId];
    const ItemId currentItem = itemPlayer->inventory.currentItemId;
    const bool isUsingItem = (itemPlayer->bitfield & 0x2000) != 0;

    if ((isUsingItem && currentItem == LIGHTNING)
      || (previousLocalInventoryItem == LIGHTNING && currentItem != LIGHTNING)) {
        localShockUseFrames = 20;
        pendingShockFrames = 0;
        shockObserved = false;
    } else if (localShockUseFrames > 0) {
        --localShockUseFrames;
    }

    previousLocalInventoryItem = currentItem;
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
    if (handler == nullptr || !handler->isPrepared)
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

    if (entry.action == RKNet::EVENTACTION_USE && entry.itemObjId == OBJ_LIGHTNING) {
        const u8 localPlayerId = GetLocalPlayerId();
        if (localPlayerId == 0xff)
            return;

        const u8 shockUserId = entry.data[0] & 0x0f;
        if (shockUserId >= 12)
            return;

        if (shockUserId == localPlayerId) {
            localShockUseFrames = 20;
            pendingShockFrames = 0;
            shockObserved = false;
            return;
        }

        BeginShockCheck();
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
    if (handler == nullptr || !handler->isPrepared)
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

static void ScanShockStateTransitions(u8 localPlayerId) {
    Kart::Manager *manager = Kart::Manager::sInstance;
    if (manager == nullptr || manager->players == nullptr)
        return;

    const u8 playerCount = manager->playerCount < 12 ? manager->playerCount : 12;
    u8 validOpponentCount = 0;
    u8 newlyShockedOpponents = 0;

    for (u8 playerId = 0; playerId < playerCount; ++playerId) {
        Kart::Status *status = GetStatusForPlayer(playerId);
        if (status == nullptr)
            continue;

        const bool isShocked = (status->bitfield2 & 0x80) != 0;

        if (playerId != localPlayerId) {
            ++validOpponentCount;
            if (shockStateInitialized[playerId] && isShocked && !wasShocked[playerId])
                ++newlyShockedOpponents;
        }

        wasShocked[playerId] = isShocked;
        shockStateInitialized[playerId] = true;
    }

    if (newlyShockedOpponents == 0 || localShockUseFrames != 0)
        return;

    const u8 requiredTransitions = validOpponentCount <= 1 ? 1 : 2;
    if (newlyShockedOpponents >= requiredTransitions)
        BeginShockCheck();
}

static void RefreshWatch(ObjectWatch &watch, const Item::Obj *object) {
    if (object == nullptr) {
        watch.object = nullptr;
        watch.eventBitfield = 0;
        watch.counted = false;
        return;
    }

    if (watch.object != object || watch.eventBitfield != object->eventBitfield) {
        watch.object = object;
        watch.eventBitfield = object->eventBitfield;
        watch.counted = false;
    }
}

static void ScanExplosiveItemHits(u8 localPlayerId, Kart::Status *status) {
    Item::Manager *manager = Item::Manager::sInstance;
    if (manager == nullptr)
        return;

    const bool localHitThisFrame = status != nullptr && (status->bitfield1 & 0x1) != 0;

    Item::ObjHolder &blueHolder = manager->itemObjHolders[OBJ_BLUE_SHELL];
    if (blueHolder.itemObj != nullptr) {
        const u32 count = blueHolder.capacity < 32 ? blueHolder.capacity : 32;
        for (u32 i = 0; i < count; ++i) {
            Item::Obj *base = blueHolder.itemObj[i];
            RefreshWatch(blueWatches[i], base);
            if (base == nullptr || blueWatches[i].counted || base->duration == 0)
                continue;

            Item::ObjKouraTogezo *blue = static_cast<Item::ObjKouraTogezo *>(base);
            if (blue->state != Item::ObjKouraTogezo::STATE_EXPLODING)
                continue;

            if (blue->playerCollisionId == localPlayerId
              || (blue->targetedPlayerId == localPlayerId && localHitThisFrame)) {
                ++stats.blueHits;
                blueWatches[i].counted = true;
            }
        }
    }

    Item::ObjHolder &bombHolder = manager->itemObjHolders[OBJ_BOBOMB];
    if (bombHolder.itemObj != nullptr) {
        const u32 count = bombHolder.capacity < 32 ? bombHolder.capacity : 32;
        for (u32 i = 0; i < count; ++i) {
            Item::Obj *base = bombHolder.itemObj[i];
            RefreshWatch(bombWatches[i], base);
            if (base == nullptr || bombWatches[i].counted || base->duration == 0)
                continue;

            Item::ObjBomb *bomb = static_cast<Item::ObjBomb *>(base);
            if (bomb->state == Item::ObjBomb::STATE_EXPLODING
              && bomb->playerCollisionId == localPlayerId) {
                ++stats.bombHits;
                bombWatches[i].counted = true;
            }
        }
    }
}

}  // namespace

void ResetDrinkTracker() {
    stats.finishPosition = 0;
    stats.blueHits = 0;
    stats.bombHits = 0;
    stats.falls = 0;
    stats.shockDodges = 0;

    trackedPlayerId = ResolveCurrentLocalPlayerId();
    trackedRaceInfo = Raceinfo::sInstance;

    wasOob = false;
    pendingShockFrames = 0;
    shockObserved = false;
    localShockUseFrames = 0;
    previousLocalInventoryItem = ITEM_NONE;
    lastRaceFrame = 0xffffffff;
    eventsPrimed = false;

    for (u32 row = 0; row < 13; ++row) {
        for (u32 i = 0; i < 24; ++i)
            eventStamps[row][i].initialized = false;
    }

    for (u32 i = 0; i < 12; ++i) {
        shockStateInitialized[i] = false;
        wasShocked[i] = false;
    }

    for (u32 i = 0; i < 32; ++i) {
        blueWatches[i].object = nullptr;
        blueWatches[i].eventBitfield = 0;
        blueWatches[i].counted = false;
        bombWatches[i].object = nullptr;
        bombWatches[i].eventBitfield = 0;
        bombWatches[i].counted = false;
    }
}

void UpdateDrinkTracker() {
    Raceinfo *raceInfo = Raceinfo::sInstance;
    Racedata *racedata = Racedata::sInstance;

    if (raceInfo == nullptr || racedata == nullptr)
        return;

    if (trackedRaceInfo != raceInfo
      || (lastRaceFrame != 0xffffffff && raceInfo->raceFrames < lastRaceFrame)) {
        ResetDrinkTracker();
    }

    if (raceInfo->stage < RACESTAGE_RACE)
        return;

    if (trackedPlayerId == 0xff)
        trackedPlayerId = ResolveCurrentLocalPlayerId();

    if (lastRaceFrame == raceInfo->raceFrames)
        return;
    lastRaceFrame = raceInfo->raceFrames;

    const u8 localPlayerId = GetLocalPlayerId();
    if (localPlayerId == 0xff)
        return;

    Kart::Status *status = GetLocalStatus();

    if (status != nullptr) {
        const bool isOob = (status->bitfield0 & 0x10) != 0;
        if (isOob && !wasOob)
            ++stats.falls;
        wasOob = isOob;
    } else {
        wasOob = false;
    }

    UpdateLocalShockUseState(localPlayerId);
    ScanExplosiveItemHits(localPlayerId, status);
    ScanEvents();
    ScanShockStateTransitions(localPlayerId);

    if (localShockUseFrames != 0) {
        pendingShockFrames = 0;
        shockObserved = false;
    } else if (pendingShockFrames > 0 && status != nullptr) {
        if ((status->bitfield2 & 0x80) != 0)
            shockObserved = true;

        --pendingShockFrames;
        if (pendingShockFrames == 0 && !shockObserved)
            ++stats.shockDodges;
    }

    // Do not read racesScenario.players[].finishPos here. Retro Rewind can retain
    // the previous race's value into the next race, which made stale placement
    // points reappear after a reset. The finalized leaderboard sets placement via
    // SetDrinkFinishPosition() when the race actually ends.
}

void SetDrinkFinishPosition(u8 finishPosition) {
    if (finishPosition > 0 && finishPosition <= 12)
        stats.finishPosition = finishPosition;
}

u8 GetDrinkPlayerId() {
    return trackedPlayerId;
}

const DrinkRaceStats &GetDrinkRaceStats() {
    return stats;
}

}  // namespace Race
}  // namespace Pulsar
