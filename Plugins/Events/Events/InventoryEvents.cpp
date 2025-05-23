#include "Events.hpp"
#include "API/CAppManager.hpp"
#include "API/CServerExoApp.hpp"
#include "API/CNWSPlayer.hpp"
#include "API/CNWSMessage.hpp"
#include "API/CNWSPlayerInventoryGUI.hpp"
#include "API/CItemRepository.hpp"
#include "API/CNWSItem.hpp"
#include "API/CNWSCreature.hpp"

namespace Events {

using namespace NWNXLib;
using namespace NWNXLib::API;
using namespace NWNXLib::API::Constants;

static NWNXLib::Hooks::Hook s_HandlePlayerToServerGuiInventoryMessageHook;
static NWNXLib::Hooks::Hook s_AddItemHook;
static NWNXLib::Hooks::Hook s_RemoveItemHook;
static NWNXLib::Hooks::Hook s_AddGoldHook;
static NWNXLib::Hooks::Hook s_RemoveGoldHook;

static int32_t HandlePlayerToServerGuiInventoryMessageHook(CNWSMessage*, CNWSPlayer*, uint8_t);
static int32_t AddItemHook(CItemRepository*, CNWSItem**, uint8_t, uint8_t, int32_t, int32_t);
static int32_t RemoveItemHook(CItemRepository*, CNWSItem*);
static void AddGoldHook(CNWSCreature*, int32_t, int32_t);
static void RemoveGoldHook(CNWSCreature*, int32_t, int32_t);

void InventoryEvents() __attribute__((constructor));
void InventoryEvents()
{
    InitOnFirstSubscribe("NWNX_ON_INVENTORY_(SELECT|OPEN)_.*", []()
    {
        s_HandlePlayerToServerGuiInventoryMessageHook = Hooks::HookFunction(
                &CNWSMessage::HandlePlayerToServerGuiInventoryMessage,
                &HandlePlayerToServerGuiInventoryMessageHook, Hooks::Order::Early);
    });

    InitOnFirstSubscribe("NWNX_ON_INVENTORY_ADD_ITEM_.*", []() {
        s_AddItemHook = Hooks::HookFunction(&CItemRepository::AddItem,
                                     &AddItemHook, Hooks::Order::Early);
    });

    InitOnFirstSubscribe("NWNX_ON_INVENTORY_REMOVE_ITEM_.*", []() {
        s_RemoveItemHook = Hooks::HookFunction(&CItemRepository::RemoveItem,
                                        &RemoveItemHook, Hooks::Order::Earliest);
    });

    InitOnFirstSubscribe("NWNX_ON_INVENTORY_ADD_GOLD_.*", []() {
        s_AddGoldHook = Hooks::HookFunction(&CNWSCreature::AddGold,
                                     &AddGoldHook, Hooks::Order::Early);
    });

    InitOnFirstSubscribe("NWNX_ON_INVENTORY_REMOVE_GOLD_.*", []() {
        s_RemoveGoldHook = Hooks::HookFunction(&CNWSCreature::RemoveGold,
                                        &RemoveGoldHook, Hooks::Order::Early);
    });
}

int32_t HandlePlayerToServerGuiInventoryMessageHook(CNWSMessage *thisPtr, CNWSPlayer *pPlayer, uint8_t nMinor)
{
    int32_t retVal;

    if (!pPlayer)
        return s_HandlePlayerToServerGuiInventoryMessageHook->CallOriginal<int32_t>(thisPtr, pPlayer, nMinor);

    switch (nMinor)
    {
        case MessageGuiInventoryMinor::Status:
        {
            auto target = Utils::PeekMessage<ObjectID>(thisPtr, 0) & 0x7FFFFFFF;
            auto open = (bool)(Utils::PeekMessage<uint8_t>(thisPtr, 4) & 0x10);

            if (open)
            {
                auto PushAndSignal = [&](const std::string& ev) -> bool
                {
                    PushEventData("TARGET_INVENTORY", Utils::ObjectIDToString(target));
                    return SignalEvent(ev, pPlayer->m_oidNWSObject);
                };

                if (PushAndSignal("NWNX_ON_INVENTORY_OPEN_BEFORE"))
                {
                    retVal = s_HandlePlayerToServerGuiInventoryMessageHook->CallOriginal<int32_t>(thisPtr, pPlayer, nMinor);
                }
                else
                {
                    Utils::ClearReadMessage();

                    CNWSPlayerInventoryGUI *pInventory = pPlayer->m_oidNWSObject == target ? pPlayer->m_pInventoryGUI :
                                                         pPlayer->m_pOtherInventoryGUI;

                    auto *pMessage = static_cast<CNWSMessage*>(Globals::AppManager()->m_pServerExoApp->GetNWSMessage());
                    if (pMessage)
                    {
                        pMessage->SendPlayerToServerGuiInventory_Status(pPlayer, false, target);
                        pInventory->SetOpen(false, false/*bClientDirected*/);
                    }

                    retVal = false;
                }

                PushAndSignal("NWNX_ON_INVENTORY_OPEN_AFTER");
            }
            else
            {
                retVal = s_HandlePlayerToServerGuiInventoryMessageHook->CallOriginal<int32_t>(thisPtr, pPlayer, nMinor);
            }
            break;
        }

        case MessageGuiInventoryMinor::SelectPanel:
        {
            auto selectedPanel = Utils::PeekMessage<uint8_t>(thisPtr, 0);
            auto ownInventory = (bool)(Utils::PeekMessage<uint8_t>(thisPtr, 1) & 0x10);

            if (ownInventory)
            {
                uint8_t currentPanel = pPlayer->m_pInventoryGUI->m_nSelectedInventoryPanel;

                auto PushAndSignal = [&](const std::string& ev) -> bool
                {
                    PushEventData("CURRENT_PANEL", std::to_string(currentPanel));
                    PushEventData("SELECTED_PANEL", std::to_string(selectedPanel));

                    return SignalEvent(ev, pPlayer->m_oidNWSObject);
                };

                if (PushAndSignal("NWNX_ON_INVENTORY_SELECT_PANEL_BEFORE"))
                {
                    retVal = s_HandlePlayerToServerGuiInventoryMessageHook->CallOriginal<int32_t>(thisPtr, pPlayer,
                                                                                                  nMinor);
                }
                else
                {
                    Utils::ClearReadMessage();

                    auto *pMessage = static_cast<CNWSMessage*>(Globals::AppManager()->m_pServerExoApp->GetNWSMessage());
                    if (pMessage)
                    {
                        pMessage->SendServerToPlayerInventory_SelectPanel(pPlayer->m_nPlayerID, currentPanel);
                    }

                    retVal = false;
                }

                PushAndSignal("NWNX_ON_INVENTORY_SELECT_PANEL_AFTER");
            }
            else
            {
                retVal = s_HandlePlayerToServerGuiInventoryMessageHook->CallOriginal<int32_t>(thisPtr, pPlayer, nMinor);
            }
            break;
        }

        default:
            retVal = s_HandlePlayerToServerGuiInventoryMessageHook->CallOriginal<int32_t>(thisPtr, pPlayer, nMinor);
            break;
    }

    return retVal;
}

int32_t AddItemHook(CItemRepository* thisPtr, CNWSItem** ppItem, uint8_t x, uint8_t y, int32_t bAllowEncumbrance, int32_t bMergeItem)
{
    int32_t retVal;

    auto *pContainer = Globals::AppManager()->m_pServerExoApp->GetGameObject(thisPtr->m_oidParent);

    // Early out if parent isn't an item or placeable or Bad Things(tm) happen
    if(!Utils::AsNWSItem(pContainer) && !Utils::AsNWSPlaceable(pContainer))
    {
        return s_AddItemHook->CallOriginal<int32_t>(thisPtr, ppItem, x, y, bAllowEncumbrance, bMergeItem);
    }

    auto PushAndSignal = [&](const std::string& ev) -> bool {
        PushEventData("ITEM", Utils::ObjectIDToString(ppItem && *ppItem ? (**ppItem).m_idSelf : OBJECT_INVALID));
        return SignalEvent(ev, thisPtr->m_oidParent);
    };

    if (PushAndSignal("NWNX_ON_INVENTORY_ADD_ITEM_BEFORE"))
    {
        retVal = s_AddItemHook->CallOriginal<int32_t>(thisPtr, ppItem, x, y, bAllowEncumbrance, bMergeItem);
    }
    else
    {
        retVal = false;
    }

    PushAndSignal("NWNX_ON_INVENTORY_ADD_ITEM_AFTER");

    return retVal;
}

int32_t RemoveItemHook(CItemRepository* thisPtr, CNWSItem* pItem)
{
    auto *pContainer = Globals::AppManager()->m_pServerExoApp->GetGameObject(thisPtr->m_oidParent);

    // Early out if parent isn't an item or placeable or Bad Things(tm) happen
    if(!Utils::AsNWSItem(pContainer) && !Utils::AsNWSPlaceable(pContainer))
    {
        return s_RemoveItemHook->CallOriginal<int32_t>(thisPtr, pItem);
    }

    PushEventData("ITEM", Utils::ObjectIDToString(pItem ? pItem->m_idSelf : OBJECT_INVALID));
    SignalEvent("NWNX_ON_INVENTORY_REMOVE_ITEM_BEFORE", thisPtr->m_oidParent);
    auto retVal = s_RemoveItemHook->CallOriginal<int32_t>(thisPtr, pItem);
    PushEventData("ITEM", Utils::ObjectIDToString(pItem ? pItem->m_idSelf : OBJECT_INVALID));
    SignalEvent("NWNX_ON_INVENTORY_REMOVE_ITEM_AFTER", thisPtr->m_oidParent);

    return retVal;
}

void AddGoldHook(CNWSCreature *pCreature, int32_t nGold, int32_t bDisplayFeedBack)
{
    auto PushAndSignal = [&](const std::string &ev) -> bool {
        PushEventData("GOLD", std::to_string(nGold));
        return SignalEvent(ev, pCreature->m_idSelf);
    };

    if (PushAndSignal("NWNX_ON_INVENTORY_ADD_GOLD_BEFORE"))
    {
        s_AddGoldHook->CallOriginal<void>(pCreature, nGold, bDisplayFeedBack);
    }

    PushAndSignal("NWNX_ON_INVENTORY_ADD_GOLD_AFTER");
}

void RemoveGoldHook(CNWSCreature *pCreature, int32_t nGold, int32_t bDisplayFeedBack)
{
    auto PushAndSignal = [&](const std::string &ev) -> bool {
        PushEventData("GOLD", std::to_string(nGold));
        return SignalEvent(ev, pCreature->m_idSelf);
    };

    if (PushAndSignal("NWNX_ON_INVENTORY_REMOVE_GOLD_BEFORE"))
    {
        s_RemoveGoldHook->CallOriginal<void>(pCreature, nGold, bDisplayFeedBack);
    }

    PushAndSignal("NWNX_ON_INVENTORY_REMOVE_GOLD_AFTER");
}

}
