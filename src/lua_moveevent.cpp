// Copyright 2022 Xynera.net. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "luascript.h"

#include "movement.h"
#include "script.h"

extern MoveEvents* g_moveEvents;
extern Scripts* g_scripts;

int LuaScriptInterface::luaCreateMoveEvent(lua_State* L)
{
	// MoveEvent()
	if (getScriptEnv()->getScriptInterface() != &g_scripts->getScriptInterface()) {
		reportErrorFunc(L, "MoveEvents can only be registered in the Scripts interface.");
		lua_pushnil(L);
		return 1;
	}

	MoveEvent* moveevent = new MoveEvent(getScriptEnv()->getScriptInterface());
	if (moveevent) {
		moveevent->fromLua = true;
		pushUserdata<MoveEvent>(L, moveevent);
		setMetatable(L, -1, "MoveEvent");
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMoveEventType(lua_State* L)
{
	// moveevent:type(callback)
	MoveEvent* moveevent = getUserdata<MoveEvent>(L, 1);
	if (moveevent) {
		std::string typeName = getString(L, 2);
		std::string tmpStr = asLowerCaseString(typeName);
		if (tmpStr == "stepin") {
			moveevent->setEventType(MOVE_EVENT_STEP_IN);
			moveevent->stepFunction = moveevent->StepInField;
		} else if (tmpStr == "stepout") {
			moveevent->setEventType(MOVE_EVENT_STEP_OUT);
			moveevent->stepFunction = moveevent->StepOutField;
		} else if (tmpStr == "equip") {
			moveevent->setEventType(MOVE_EVENT_EQUIP);
			moveevent->equipFunction = moveevent->EquipItem;
		} else if (tmpStr == "deequip") {
			moveevent->setEventType(MOVE_EVENT_DEEQUIP);
			moveevent->equipFunction = moveevent->DeEquipItem;
		} else if (tmpStr == "additem") {
			moveevent->setEventType(MOVE_EVENT_ADD_ITEM);
			moveevent->moveFunction = moveevent->AddItemField;
		} else if (tmpStr == "removeitem") {
			moveevent->setEventType(MOVE_EVENT_REMOVE_ITEM);
			moveevent->moveFunction = moveevent->RemoveItemField;
		} else {
			console::reportError("MoveEvent:type", "Invalid event type \"" + typeName + "\"!");
			pushBoolean(L, false);
		}
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMoveEventRegister(lua_State* L)
{
	// moveevent:register()
	MoveEvent* moveevent = getUserdata<MoveEvent>(L, 1);
	if (moveevent) {
		if ((moveevent->getEventType() == MOVE_EVENT_EQUIP || moveevent->getEventType() == MOVE_EVENT_DEEQUIP) && moveevent->getSlot() == SLOTP_WHEREEVER) {
			uint32_t id = moveevent->getItemIdRange().at(0);
			ItemType& it = Item::items.getItemType(id);
			moveevent->setSlot(it.slotPosition);
		}
		if (!moveevent->isScripted()) {
			pushBoolean(L, g_moveEvents->registerLuaFunction(moveevent));
			return 1;
		}
		pushBoolean(L, g_moveEvents->registerLuaEvent(moveevent));
		moveevent->clearItemIdRange();
		moveevent->clearActionIdRange();
		moveevent->clearUniqueIdRange();
		moveevent->clearPosList();
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMoveEventOnCallback(lua_State* L)
{
	// moveevent:onEquip / deEquip / etc. (callback)
	MoveEvent* moveevent = getUserdata<MoveEvent>(L, 1);
	if (moveevent) {
		if (!moveevent->loadCallback()) {
			pushBoolean(L, false);
			return 1;
		}
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMoveEventSlot(lua_State* L)
{
	// moveevent:slot(slot)
	MoveEvent* moveevent = getUserdata<MoveEvent>(L, 1);
	if (!moveevent) {
		lua_pushnil(L);
		return 1;
	}

	if (moveevent->getEventType() == MOVE_EVENT_EQUIP || moveevent->getEventType() == MOVE_EVENT_DEEQUIP) {
		std::string slotName = asLowerCaseString(getString(L, 2));
		if (slotName == "head") {
			moveevent->setSlot(SLOTP_HEAD);
		} else if (slotName == "necklace") {
			moveevent->setSlot(SLOTP_NECKLACE);
		} else if (slotName == "backpack") {
			moveevent->setSlot(SLOTP_BACKPACK);
		} else if (slotName == "armor" || slotName == "body") {
			moveevent->setSlot(SLOTP_ARMOR);
		} else if (slotName == "right-hand") {
			moveevent->setSlot(SLOTP_RIGHT);
		} else if (slotName == "left-hand") {
			moveevent->setSlot(SLOTP_LEFT);
		} else if (slotName == "hand" || slotName == "shield") {
			moveevent->setSlot(SLOTP_RIGHT | SLOTP_LEFT);
		} else if (slotName == "legs") {
			moveevent->setSlot(SLOTP_LEGS);
		} else if (slotName == "feet") {
			moveevent->setSlot(SLOTP_FEET);
		} else if (slotName == "ring") {
			moveevent->setSlot(SLOTP_RING);
		} else if (slotName == "ammo") {
			moveevent->setSlot(SLOTP_AMMO);
		} else {
			console::reportError("MoveEvent:slot", "Unknown slot type \"" + slotName + "\"!");
			pushBoolean(L, false);
			return 1;
		}
	}

	pushBoolean(L, true);
	return 1;
}

int LuaScriptInterface::luaMoveEventLevel(lua_State* L)
{
	// moveevent:level(lvl)
	MoveEvent* moveevent = getUserdata<MoveEvent>(L, 1);
	if (moveevent) {
		moveevent->setRequiredLevel(getNumber<uint32_t>(L, 2));
		moveevent->setWieldInfo(WIELDINFO_LEVEL);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMoveEventMagLevel(lua_State* L)
{
	// moveevent:magicLevel(lvl)
	MoveEvent* moveevent = getUserdata<MoveEvent>(L, 1);
	if (moveevent) {
		moveevent->setRequiredMagLevel(getNumber<uint32_t>(L, 2));
		moveevent->setWieldInfo(WIELDINFO_MAGLV);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMoveEventPremium(lua_State* L)
{
	// moveevent:premium(bool)
	MoveEvent* moveevent = getUserdata<MoveEvent>(L, 1);
	if (moveevent) {
		moveevent->setNeedPremium(getBoolean(L, 2));
		moveevent->setWieldInfo(WIELDINFO_PREMIUM);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMoveEventVocation(lua_State* L)
{
	// moveevent:vocation(vocName[, showInDescription = false, lastVoc = false])
	MoveEvent* moveevent = getUserdata<MoveEvent>(L, 1);
	if (moveevent) {
		moveevent->addVocEquipMap(getString(L, 2));
		moveevent->setWieldInfo(WIELDINFO_VOCREQ);
		std::string tmp;
		bool showInDescription = false;
		bool lastVoc = false;
		if (getBoolean(L, 3)) {
			showInDescription = getBoolean(L, 3);
		}
		if (getBoolean(L, 4)) {
			lastVoc = getBoolean(L, 4);
		}
		if (showInDescription) {
			std::string vocName = asLowerCaseString(getString(L, 2)) + "s";
			if (vocName.compare("nones") == 0) {
				vocName = "players without vocation";
			}

			if (moveevent->getVocationString().empty()) {
				tmp = vocName;
				moveevent->setVocationString(tmp);
			} else {
				tmp = moveevent->getVocationString();
				if (lastVoc) {
					tmp += " and ";
				} else {
					tmp += ", ";
				}
				tmp += vocName;
				moveevent->setVocationString(tmp);
			}
		}
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMoveEventTileItem(lua_State* L)
{
	// moveevent:tileItem(bool)
	MoveEvent* moveevent = getUserdata<MoveEvent>(L, 1);
	if (moveevent) {
		moveevent->setTileItem(getBoolean(L, 2));
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMoveEventItemId(lua_State* L)
{
	// moveevent:id(ids)
	MoveEvent* moveevent = getUserdata<MoveEvent>(L, 1);
	if (moveevent) {
		int parameters = lua_gettop(L) - 1; // - 1 because self is a parameter aswell, which we want to skip ofc
		if (parameters > 1) {
			for (int i = 0; i < parameters; ++i) {
				moveevent->addItemId(getNumber<uint32_t>(L, 2 + i));
			}
		} else {
			moveevent->addItemId(getNumber<uint32_t>(L, 2));
		}
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMoveEventActionId(lua_State* L)
{
	// moveevent:aid(ids)
	MoveEvent* moveevent = getUserdata<MoveEvent>(L, 1);
	if (moveevent) {
		int parameters = lua_gettop(L) - 1; // - 1 because self is a parameter aswell, which we want to skip ofc
		if (parameters > 1) {
			for (int i = 0; i < parameters; ++i) {
				moveevent->addActionId(getNumber<uint32_t>(L, 2 + i));
			}
		} else {
			moveevent->addActionId(getNumber<uint32_t>(L, 2));
		}
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMoveEventUniqueId(lua_State* L)
{
	// moveevent:uid(ids)
	MoveEvent* moveevent = getUserdata<MoveEvent>(L, 1);
	if (moveevent) {
		int parameters = lua_gettop(L) - 1; // - 1 because self is a parameter aswell, which we want to skip ofc
		if (parameters > 1) {
			for (int i = 0; i < parameters; ++i) {
				moveevent->addUniqueId(getNumber<uint32_t>(L, 2 + i));
			}
		} else {
			moveevent->addUniqueId(getNumber<uint32_t>(L, 2));
		}
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMoveEventPosition(lua_State* L)
{
	// moveevent:position(positions)
	MoveEvent* moveevent = getUserdata<MoveEvent>(L, 1);
	if (moveevent) {
		int parameters = lua_gettop(L) - 1; // - 1 because self is a parameter aswell, which we want to skip ofc
		if (parameters > 1) {
			for (int i = 0; i < parameters; ++i) {
				moveevent->addPosList(getPosition(L, 2 + i));
			}
		} else {
			moveevent->addPosList(getPosition(L, 2));
		}
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}