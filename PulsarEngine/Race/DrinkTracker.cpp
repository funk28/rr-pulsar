#include <Race/DrinkTracker.hpp>

#include <MarioKartWii/Item/ItemManager.hpp>
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

bool eventsPrimed = false;
bool wasOob = false;
u8 pendingShockFrames = 0;
bool shockObserved = false;
u32 lastRaceFrame = 0xffffffff;

static u8 GetLocalPlayerId() {
    const Racedata *racedata = Racedata::sInstance;
    if (racedata == nullptr || racedata->racesScenario.localPlayerCount == 0)
        return 0xff;

    const u8 playerId = racedata->racesScenario.settings.hudPlayerIds[0];
    if (playerId >= racedata->racesScenario.playerCount || playerId >= 12)
        return 0xff;
    return playerId;
}

static Kart::Status *GetLocalStatus() {
    const u8 playerId = GetLocalPlayerId();
    Kart::Manager *manager = Kart::Manager::sInstance;

    if (manager == nullptr || manager->players == nullptr
      || playerId == 0xff || playerId >= manager->playerCount || playerId >= 12)
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

    // Blue-shell and Bob-omb hits are tracked directly from the live item
    // objects so they also work in offline races. EVENT is retained for online
    // lightning-use detection.
    if (entry.action == RKNet::EVENTACTION_USE && entry.itemObjId == OBJ_LIGHTNING) {
        const u8 localPlayerId = GetLocalPlayerId();
        if (localPlayerId == 0xff)
            return;

        const u8 shockUserId = entry.data[0];
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

            // playerCollisionId catches splash hits. targetedPlayerId plus the
            // kart's item-hit flag catches the normal direct blue-shell hit.
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

    wasOob = false;
    pendingShockFrames = 0;
    shockObserved = false;
    lastRaceFrame = 0xffffffff;
    eventsPrimed = false;

    for (u32 row = 0; row < 13; ++row) {
        for (u32 i = 0; i < 24; ++i)
            eventStamps[row][i].initialized = false;
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
    if (raceInfo->stage < RACESTAGE_RACE)
        return;

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

    ScanExplosiveItemHits(localPlayerId, status);
    ScanEvents();

    if (pendingShockFrames > 0 && status != nullptr) {
        if ((status->bitfield2 & 0x80) != 0)
            shockObserved = true;

        --pendingShockFrames;
        if (pendingShockFrames == 0 && !shockObserved)
            ++stats.shockDodges;
    }

    if (stats.finishPosition == 0) {
        const u8 scenarioFinishPos = racedata->racesScenario.players[localPlayerId].finishPos;
        if (scenarioFinishPos > 0) {
            stats.finishPosition = scenarioFinishPos;
        } else if (raceInfo->players != nullptr && raceInfo->players[localPlayerId] != nullptr) {
            RaceinfoPlayer *playerInfo = raceInfo->players[localPlayerId];
            if ((playerInfo->stateFlags & 0x20) != 0 && playerInfo->position > 0)
                stats.finishPosition = playerInfo->position;
        }
    }
}

void SetDrinkFinishPosition(u8 finishPosition) {
    if (finishPosition > 0 && finishPosition <= 12)
        stats.finishPosition = finishPosition;
}

u8 GetDrinkPlayerId() {
    return GetLocalPlayerId();
}

const DrinkRaceStats &GetDrinkRaceStats() {
    return stats;
}

}  // namespace Race
}  // namespace Pulsar
