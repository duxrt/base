// Copyright 2022 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "combat.h"

#include "game.h"
#include "weapons.h"
#include "configmanager.h"
#include "events.h"

extern Game g_game;
extern Weapons* g_weapons;
extern ConfigManager g_config;
extern Events* g_events;

namespace {

MatrixArea createArea(const std::vector<uint32_t>& vec, uint32_t rows)
{
	uint32_t cols;
	if (rows == 0) {
		cols = 0;
	} else {
		cols = vec.size() / rows;
	}

	MatrixArea area{rows, cols};

	uint32_t x = 0;
	uint32_t y = 0;

	for (uint32_t value : vec) {
		if (value == 1 || value == 3) {
			area(y, x) = true;
		}

		if (value == 2 || value == 3) {
			area.setCenter(y, x);
		}

		++x;

		if (cols == x) {
			x = 0;
			++y;
		}
	}
	return area;
}

std::vector<Tile*> getList(const MatrixArea& area, const Position& targetPos, const Direction dir)
{
	auto casterPos = getNextPosition(dir, targetPos);

	std::vector<Tile*> vec;

	auto center = area.getCenter();

	Position tmpPos(targetPos.x - center.first, targetPos.y - center.second, targetPos.z);
	for (uint32_t row = 0; row < area.getRows(); ++row, ++tmpPos.y) {
		for (uint32_t col = 0; col < area.getCols(); ++col, ++tmpPos.x) {
			if (area(row, col)) {
				if (g_game.isSightClear(casterPos, tmpPos, true)) {
					Tile* tile = g_game.map.getTile(tmpPos);
					if (!tile) {
						tile = new StaticTile(tmpPos.x, tmpPos.y, tmpPos.z);
						g_game.map.setTile(tmpPos, tile);
					}
					vec.push_back(tile);
				}
			}
		}
		tmpPos.x -= area.getCols();
	}
	return vec;
}

std::vector<Tile*> getCombatArea(const Position& centerPos, const Position& targetPos, const AreaCombat* area)
{
	if (targetPos.z >= MAP_MAX_LAYERS) {
		return {};
	}

	if (area) {
		return getList(area->getArea(centerPos, targetPos), targetPos, getDirectionTo(targetPos, centerPos));
	}

	Tile* tile = g_game.map.getTile(targetPos);
	if (!tile) {
		tile = new StaticTile(targetPos.x, targetPos.y, targetPos.z);
		g_game.map.setTile(targetPos, tile);
	}
	return {tile};
}

}

CombatDamage Combat::getCombatDamage(Creature* creature, Creature* target) const
{
	CombatDamage damage;
	damage.origin = params.origin;
	damage.primary.type = params.combatType;
	if (formulaType == COMBAT_FORMULA_DAMAGE) {
		damage.primary.value = normal_random(
			static_cast<int32_t>(mina),
			static_cast<int32_t>(maxa)
		);
	} else if (creature) {
		int32_t min, max;
		if (creature->getCombatValues(min, max)) {
			damage.primary.value = normal_random(min, max);
		} else if (Player* player = creature->getPlayer()) {
			if (params.valueCallback) {
				params.valueCallback->getMinMaxValues(player, damage);
			} else if (formulaType == COMBAT_FORMULA_LEVELMAGIC) {
				int32_t levelFormula = player->getLevel() * 2 + player->getMagicLevel() * 3;
				damage.primary.value = normal_random(std::fma(levelFormula, mina, minb), std::fma(levelFormula, maxa, maxb));
			} else if (formulaType == COMBAT_FORMULA_SKILL) {
				Item* tool = player->getWeapon();
				const Weapon* weapon = g_weapons->getWeapon(tool);
				if (weapon) {
					damage.primary.value = normal_random(minb, std::fma(weapon->getWeaponDamage(player, target, tool, true), maxa, maxb));
					damage.secondary.type = weapon->getElementType();
					damage.secondary.value = weapon->getElementDamage(player, target, tool);
				} else {
					damage.primary.value = normal_random(minb, maxb);
				}
			}
		}
	}
	return damage;
}

CombatType_t Combat::ConditionToDamageType(ConditionType_t type)
{
	switch (type) {
		case CONDITION_FIRE:
			return COMBAT_FIREDAMAGE;

		case CONDITION_ENERGY:
			return COMBAT_ENERGYDAMAGE;

		case CONDITION_BLEEDING:
			return COMBAT_PHYSICALDAMAGE;

		case CONDITION_DROWN:
			return COMBAT_DROWNDAMAGE;

		case CONDITION_POISON:
			return COMBAT_EARTHDAMAGE;

		case CONDITION_FREEZING:
			return COMBAT_ICEDAMAGE;

		case CONDITION_DAZZLED:
			return COMBAT_HOLYDAMAGE;

		case CONDITION_CURSED:
			return COMBAT_DEATHDAMAGE;

		case CONDITION_BEWITCHED:
			return COMBAT_ARCANEDAMAGE;
		
		case CONDITION_SPLASHED:
			return COMBAT_WATERDAMAGE;

		default:
			break;
	}

	return COMBAT_NONE;
}

ConditionType_t Combat::DamageToConditionType(CombatType_t type)
{
	switch (type) {
		case COMBAT_FIREDAMAGE:
			return CONDITION_FIRE;

		case COMBAT_ENERGYDAMAGE:
			return CONDITION_ENERGY;

		case COMBAT_DROWNDAMAGE:
			return CONDITION_DROWN;

		case COMBAT_EARTHDAMAGE:
			return CONDITION_POISON;

		case COMBAT_ICEDAMAGE:
			return CONDITION_FREEZING;

		case COMBAT_HOLYDAMAGE:
			return CONDITION_DAZZLED;

		case COMBAT_DEATHDAMAGE:
			return CONDITION_CURSED;

		case COMBAT_WATERDAMAGE:
			return CONDITION_SPLASHED;

		case COMBAT_ARCANEDAMAGE:
			return CONDITION_BEWITCHED;

		case COMBAT_PHYSICALDAMAGE:
			return CONDITION_BLEEDING;

		default:
			return CONDITION_NONE;
	}
}

bool Combat::isPlayerCombat(const Creature* target)
{
	if (target->getPlayer()) {
		return true;
	}

	if (target->isSummon() && target->getMaster()->getPlayer()) {
		return true;
	}

	return false;
}

ReturnValue Combat::canTargetCreature(Player* attacker, Creature* target)
{
	if (attacker == target) {
		return RETURNVALUE_YOUMAYNOTATTACKTHISPLAYER;
	}

	if (!attacker->hasFlag(PlayerFlag_IgnoreProtectionZone)) {
		//pz-zone
		if (attacker->getZone() == ZONE_PROTECTION) {
			return RETURNVALUE_ACTIONNOTPERMITTEDINPROTECTIONZONE;
		}

		if (target->getZone() == ZONE_PROTECTION) {
			return RETURNVALUE_ACTIONNOTPERMITTEDINPROTECTIONZONE;
		}

		//nopvp-zone
		if (isPlayerCombat(target)) {
			if (attacker->getZone() == ZONE_NOPVP) {
				return RETURNVALUE_ACTIONNOTPERMITTEDINANOPVPZONE;
			}

			if (target->getZone() == ZONE_NOPVP) {
				return RETURNVALUE_YOUMAYNOTATTACKAPERSONINPROTECTIONZONE;
			}
		}
	}

	if (attacker->hasFlag(PlayerFlag_CannotUseCombat) || !target->isAttackable()) {
		if (target->getPlayer()) {
			return RETURNVALUE_YOUMAYNOTATTACKTHISPLAYER;
		} else {
			return RETURNVALUE_YOUMAYNOTATTACKTHISCREATURE;
		}
	}

	if (target->getPlayer()) {
		if (isProtected(attacker, target->getPlayer())) {
			return RETURNVALUE_YOUMAYNOTATTACKTHISPLAYER;
		}

		if (attacker->hasSecureMode() && !Combat::isInPvpZone(attacker, target) && attacker->getSkullClient(target->getPlayer()) == SKULL_NONE) {
			return RETURNVALUE_TURNSECUREMODETOATTACKUNMARKEDPLAYERS;
		}
	}

	return Combat::canDoCombat(attacker, target);
}

ReturnValue Combat::canDoCombat(Creature* caster, Tile* tile, bool aggressive)
{
	if (tile->hasProperty(CONST_PROP_BLOCKPROJECTILE)) {
		return RETURNVALUE_NOTENOUGHROOM;
	}

	if (tile->hasFlag(TILESTATE_FLOORCHANGE)) {
		return RETURNVALUE_NOTENOUGHROOM;
	}

	if (tile->getTeleportItem()) {
		return RETURNVALUE_NOTENOUGHROOM;
	}

	if (caster) {
		const Position& casterPosition = caster->getPosition();
		const Position& tilePosition = tile->getPosition();
		if (casterPosition.z < tilePosition.z) {
			return RETURNVALUE_FIRSTGODOWNSTAIRS;
		} else if (casterPosition.z > tilePosition.z) {
			return RETURNVALUE_FIRSTGOUPSTAIRS;
		}

		if (const Player* player = caster->getPlayer()) {
			if (player->hasFlag(PlayerFlag_IgnoreProtectionZone)) {
				return RETURNVALUE_NOERROR;
			}
		}
	}

	//pz-zone
	if (aggressive && tile->hasFlag(TILESTATE_PROTECTIONZONE)) {
		return RETURNVALUE_ACTIONNOTPERMITTEDINPROTECTIONZONE;
	}

	return g_events->eventCreatureOnAreaCombat(caster, tile, aggressive);
}

bool Combat::isInPvpZone(const Creature* attacker, const Creature* target)
{
	return attacker->getZone() == ZONE_PVP && target->getZone() == ZONE_PVP;
}

bool Combat::isProtected(const Player* attacker, const Player* target)
{
	uint32_t protectionLevel = g_config.getNumber(ConfigManager::PROTECTION_LEVEL);
	if (target->getLevel() < protectionLevel || attacker->getLevel() < protectionLevel) {
		return true;
	}

	if (!attacker->getVocation()->allowsPvp() || !target->getVocation()->allowsPvp()) {
		return true;
	}

	if (attacker->getSkull() == SKULL_BLACK && attacker->getSkullClient(target) == SKULL_NONE) {
		return true;
	}

	return false;
}

ReturnValue Combat::canDoCombat(Creature* attacker, Creature* target)
{
	if (!attacker) {
		return g_events->eventCreatureOnTargetCombat(attacker, target);
	}

	if (const Player* targetPlayer = target->getPlayer()) {
		if (targetPlayer->hasFlag(PlayerFlag_CannotBeAttacked)) {
			return RETURNVALUE_YOUMAYNOTATTACKTHISPLAYER;
		}

		if (const Player* attackerPlayer = attacker->getPlayer()) {
			if (attackerPlayer->hasFlag(PlayerFlag_CannotAttackPlayer)) {
				return RETURNVALUE_YOUMAYNOTATTACKTHISPLAYER;
			}

			if (isProtected(attackerPlayer, targetPlayer)) {
				return RETURNVALUE_YOUMAYNOTATTACKTHISPLAYER;
			}

			//nopvp-zone
			const Tile* targetPlayerTile = targetPlayer->getTile();
			if (targetPlayerTile->hasFlag(TILESTATE_NOPVPZONE)) {
				return RETURNVALUE_ACTIONNOTPERMITTEDINANOPVPZONE;
			} else if (attackerPlayer->getTile()->hasFlag(TILESTATE_NOPVPZONE) && !targetPlayerTile->hasFlag(TILESTATE_NOPVPZONE | TILESTATE_PROTECTIONZONE)) {
				return RETURNVALUE_ACTIONNOTPERMITTEDINANOPVPZONE;
			}
		}

		if (attacker->isSummon()) {
			if (const Player* masterAttackerPlayer = attacker->getMaster()->getPlayer()) {
				if (masterAttackerPlayer->hasFlag(PlayerFlag_CannotAttackPlayer)) {
					return RETURNVALUE_YOUMAYNOTATTACKTHISPLAYER;
				}

				if (targetPlayer->getTile()->hasFlag(TILESTATE_NOPVPZONE)) {
					return RETURNVALUE_ACTIONNOTPERMITTEDINANOPVPZONE;
				}

				if (isProtected(masterAttackerPlayer, targetPlayer)) {
					return RETURNVALUE_YOUMAYNOTATTACKTHISPLAYER;
				}
			}
		}
	} else if (target->getMonster()) {
		if (const Player* attackerPlayer = attacker->getPlayer()) {
			if (attackerPlayer->hasFlag(PlayerFlag_CannotAttackMonster)) {
				return RETURNVALUE_YOUMAYNOTATTACKTHISCREATURE;
			}

			if (target->isSummon() && target->getMaster()->getPlayer() && target->getZone() == ZONE_NOPVP) {
				return RETURNVALUE_ACTIONNOTPERMITTEDINANOPVPZONE;
			}
		} else if (attacker->getMonster()) {
			const Creature* targetMaster = target->getMaster();

			if (!targetMaster || !targetMaster->getPlayer()) {
				const Creature* attackerMaster = attacker->getMaster();

				if (!attackerMaster || !attackerMaster->getPlayer()) {
					return RETURNVALUE_YOUMAYNOTATTACKTHISCREATURE;
				}
			}
		}
	}

	if (g_game.getWorldType() == WORLD_TYPE_NO_PVP) {
		if (attacker->getPlayer() || (attacker->isSummon() && attacker->getMaster()->getPlayer())) {
			if (target->getPlayer()) {
				if (!isInPvpZone(attacker, target)) {
					return RETURNVALUE_YOUMAYNOTATTACKTHISPLAYER;
				}
			}

			if (target->isSummon() && target->getMaster()->getPlayer()) {
				if (!isInPvpZone(attacker, target)) {
					return RETURNVALUE_YOUMAYNOTATTACKTHISCREATURE;
				}
			}
		}
	}
	return g_events->eventCreatureOnTargetCombat(attacker, target);
}

void Combat::setPlayerCombatValues(formulaType_t formulaType, double mina, double minb, double maxa, double maxb)
{
	this->formulaType = formulaType;
	this->mina = mina;
	this->minb = minb;
	this->maxa = maxa;
	this->maxb = maxb;
}

bool Combat::setParam(CombatParam_t param, uint32_t value)
{
	switch (param) {
		case COMBAT_PARAM_TYPE: {
			params.combatType = static_cast<CombatType_t>(value);
			return true;
		}

		case COMBAT_PARAM_EFFECT: {
			params.impactEffect = static_cast<uint16_t>(value);
			return true;
		}

		case COMBAT_PARAM_DISTANCEEFFECT: {
			params.distanceEffect = static_cast<uint8_t>(value);
			return true;
		}

		case COMBAT_PARAM_BLOCKARMOR: {
			params.blockedByArmor = (value != 0);
			return true;
		}

		case COMBAT_PARAM_BLOCKSHIELD: {
			params.blockedByShield = (value != 0);
			return true;
		}

		case COMBAT_PARAM_TARGETCASTERORTOPMOST: {
			params.targetCasterOrTopMost = (value != 0);
			return true;
		}

		case COMBAT_PARAM_CREATEITEM: {
			params.itemId = value;
			return true;
		}

		case COMBAT_PARAM_AGGRESSIVE: {
			params.aggressive = (value != 0);
			return true;
		}

		case COMBAT_PARAM_DISPEL: {
			params.dispelType = static_cast<ConditionType_t>(value);
			return true;
		}

		case COMBAT_PARAM_USECHARGES: {
			params.useCharges = (value != 0);
			return true;
		}
	}
	return false;
}

int32_t Combat::getParam(CombatParam_t param)
{
	switch (param) {
		case COMBAT_PARAM_TYPE:
			return static_cast<int32_t>(params.combatType);

		case COMBAT_PARAM_EFFECT:
			return static_cast<int32_t>(params.impactEffect);

		case COMBAT_PARAM_DISTANCEEFFECT:
			return static_cast<int32_t>(params.distanceEffect);

		case COMBAT_PARAM_BLOCKARMOR:
			return params.blockedByArmor ? 1 : 0;

		case COMBAT_PARAM_BLOCKSHIELD:
			return params.blockedByShield ? 1 : 0;

		case COMBAT_PARAM_TARGETCASTERORTOPMOST:
			return params.targetCasterOrTopMost ? 1 : 0;

		case COMBAT_PARAM_CREATEITEM:
			return params.itemId;

		case COMBAT_PARAM_AGGRESSIVE:
			return params.aggressive ? 1 : 0;

		case COMBAT_PARAM_DISPEL:
			return static_cast<int32_t>(params.dispelType);

		case COMBAT_PARAM_USECHARGES:
			return params.useCharges ? 1 : 0;

		default:
			return std::numeric_limits<int32_t>().max();
	}
}

bool Combat::setCallback(CallBackParam_t key)
{
	switch (key) {
		case CALLBACK_PARAM_LEVELMAGICVALUE: {
			params.valueCallback.reset(new ValueCallback(COMBAT_FORMULA_LEVELMAGIC));
			return true;
		}

		case CALLBACK_PARAM_SKILLVALUE: {
			params.valueCallback.reset(new ValueCallback(COMBAT_FORMULA_SKILL));
			return true;
		}

		case CALLBACK_PARAM_TARGETTILE: {
			params.tileCallback.reset(new TileCallback());
			return true;
		}

		case CALLBACK_PARAM_TARGETCREATURE: {
			params.targetCallback.reset(new TargetCallback());
			return true;
		}
	}
	return false;
}

CallBack* Combat::getCallback(CallBackParam_t key)
{
	switch (key) {
		case CALLBACK_PARAM_LEVELMAGICVALUE:
		case CALLBACK_PARAM_SKILLVALUE: {
			return params.valueCallback.get();
		}

		case CALLBACK_PARAM_TARGETTILE: {
			return params.tileCallback.get();
		}

		case CALLBACK_PARAM_TARGETCREATURE: {
			return params.targetCallback.get();
		}
	}
	return nullptr;
}

void Combat::combatTileEffects(const SpectatorVec& spectators, Creature* caster, Tile* tile, const CombatParams& params)
{
	if (params.itemId != 0) {
		uint16_t itemId = params.itemId;
		switch (itemId) {
			case ITEM_FIREFIELD_PERSISTENT_FULL:
				itemId = ITEM_FIREFIELD_PVP_FULL;
				break;

			case ITEM_FIREFIELD_PERSISTENT_MEDIUM:
				itemId = ITEM_FIREFIELD_PVP_MEDIUM;
				break;

			case ITEM_FIREFIELD_PERSISTENT_SMALL:
				itemId = ITEM_FIREFIELD_PVP_SMALL;
				break;

			case ITEM_ENERGYFIELD_PERSISTENT:
				itemId = ITEM_ENERGYFIELD_PVP;
				break;

			case ITEM_POISONFIELD_PERSISTENT:
				itemId = ITEM_POISONFIELD_PVP;
				break;

			case ITEM_MAGICWALL_PERSISTENT:
				itemId = ITEM_MAGICWALL;
				break;

			case ITEM_WILDGROWTH_PERSISTENT:
				itemId = ITEM_WILDGROWTH;
				break;

			default:
				break;
		}

		if (caster) {
			Player* casterPlayer;
			if (caster->isSummon()) {
				casterPlayer = caster->getMaster()->getPlayer();
			} else {
				casterPlayer = caster->getPlayer();
			}

			if (casterPlayer) {
				if (g_game.getWorldType() == WORLD_TYPE_NO_PVP || tile->hasFlag(TILESTATE_NOPVPZONE)) {
					if (itemId == ITEM_FIREFIELD_PVP_FULL) {
						itemId = ITEM_FIREFIELD_NOPVP;
					} else if (itemId == ITEM_POISONFIELD_PVP) {
						itemId = ITEM_POISONFIELD_NOPVP;
					} else if (itemId == ITEM_ENERGYFIELD_PVP) {
						itemId = ITEM_ENERGYFIELD_NOPVP;
					} else if (itemId == ITEM_MAGICWALL) {
						itemId = ITEM_MAGICWALL_NOPVP;
					} else if (itemId == ITEM_WILDGROWTH) {
						itemId = ITEM_WILDGROWTH_NOPVP;
					}
				} else if (itemId == ITEM_FIREFIELD_PVP_FULL || itemId == ITEM_POISONFIELD_PVP || itemId == ITEM_ENERGYFIELD_PVP) {
					casterPlayer->addInFightTicks();
				}
			}
		}

		Item* item = Item::CreateItem(itemId);
		if (caster) {
			item->setOwner(caster->getID());
		}

		ReturnValue ret = g_game.internalAddItem(tile, item);
		if (ret == RETURNVALUE_NOERROR) {
			g_game.startDecay(item);
		} else {
			delete item;
		}
	}

	if (params.tileCallback) {
		params.tileCallback->onTileCombat(caster, tile);
	}

	if (params.impactEffect != CONST_ME_NONE) {
		Game::addMagicEffect(spectators, tile->getPosition(), params.impactEffect);
	}
}

void Combat::postCombatEffects(Creature* caster, const Position& pos, const CombatParams& params)
{
	if (caster && params.distanceEffect != CONST_ANI_NONE) {
		addDistanceEffect(caster, caster->getPosition(), pos, params.distanceEffect);
	}
}

void Combat::addDistanceEffect(Creature* caster, const Position& fromPos, const Position& toPos, uint8_t effect)
{
	if (effect == CONST_ANI_WEAPONTYPE) {
		if (!caster) {
			return;
		}

		Player* player = caster->getPlayer();
		if (!player) {
			return;
		}

		switch (player->getWeaponType()) {
			case WEAPON_AXE:
				effect = CONST_ANI_WHIRLWINDAXE;
				break;
			case WEAPON_SWORD:
				effect = CONST_ANI_WHIRLWINDSWORD;
				break;
			case WEAPON_CLUB:
				effect = CONST_ANI_WHIRLWINDCLUB;
				break;
			default:
				effect = CONST_ANI_NONE;
				break;
		}
	}

	if (effect != CONST_ANI_NONE) {
		g_game.addDistanceEffect(fromPos, toPos, effect);
	}
}

void Combat::doCombat(Creature* caster, Creature* target) const
{
	//target combat callback function
	if (params.combatType != COMBAT_NONE) {
		CombatDamage damage = getCombatDamage(caster, target);

		bool canCombat = !params.aggressive || (caster != target && Combat::canDoCombat(caster, target) == RETURNVALUE_NOERROR);
		if ((caster == target || canCombat) && params.impactEffect != CONST_ME_NONE) {
			g_game.addMagicEffect(target->getPosition(), params.impactEffect);
		}

		if (canCombat) {
			doTargetCombat(caster, target, damage, params);
		}
	} else {
		if (!params.aggressive || (caster != target && Combat::canDoCombat(caster, target) == RETURNVALUE_NOERROR)) {
			SpectatorVec spectators;
			g_game.map.getSpectators(spectators, target->getPosition(), true, true);

			if (params.origin != ORIGIN_MELEE) {
				for (const auto& condition : params.conditionList) {
					if (caster == target || !target->isImmune(condition->getType())) {
						Condition* conditionCopy = condition->clone();
						conditionCopy->setParam(CONDITION_PARAM_OWNER, caster->getID());
						target->addCombatCondition(conditionCopy);
					}
				}
			}

			if (params.dispelType == CONDITION_PARALYZE) {
				target->removeCondition(CONDITION_PARALYZE);
			} else {
				target->removeCombatCondition(params.dispelType);
			}

			combatTileEffects(spectators, caster, target->getTile(), params);

			if (params.targetCallback) {
				params.targetCallback->onTargetCombat(caster, target);
			}

			/*
			if (params.impactEffect != CONST_ME_NONE) {
				g_game.addMagicEffect(target->getPosition(), params.impactEffect);
			}
			*/

			if (caster && params.distanceEffect != CONST_ANI_NONE) {
				addDistanceEffect(caster, caster->getPosition(), target->getPosition(), params.distanceEffect);
			}
		}
	}
}

void Combat::doCombat(Creature* caster, const Position& position) const
{
	//area combat callback function
	if (params.combatType != COMBAT_NONE) {
		CombatDamage damage = getCombatDamage(caster, nullptr);
		doAreaCombat(caster, position, area.get(), damage, params);
	} else {
		auto tiles = caster ? getCombatArea(caster->getPosition(), position, area.get()) : getCombatArea(position, position, area.get());

		SpectatorVec spectators;
		uint32_t maxX = 0;
		uint32_t maxY = 0;

		//calculate the max viewable range
		for (Tile* tile : tiles) {
			const Position& tilePos = tile->getPosition();

			uint32_t diff = Position::getDistanceX(tilePos, position);
			if (diff > maxX) {
				maxX = diff;
			}

			diff = Position::getDistanceY(tilePos, position);
			if (diff > maxY) {
				maxY = diff;
			}
		}

		const int32_t rangeX = maxX + Map::maxViewportX;
		const int32_t rangeY = maxY + Map::maxViewportY;
		g_game.map.getSpectators(spectators, position, true, true, rangeX, rangeX, rangeY, rangeY);

		postCombatEffects(caster, position, params);

		for (Tile* tile : tiles) {
			if (canDoCombat(caster, tile, params.aggressive) != RETURNVALUE_NOERROR) {
				continue;
			}

			combatTileEffects(spectators, caster, tile, params);

			if (CreatureVector* creatures = tile->getCreatures()) {
				const Creature* topCreature = tile->getTopCreature();
				for (Creature* creature : *creatures) {
					if (params.targetCasterOrTopMost) {
						if (caster && caster->getTile() == tile) {
							if (creature != caster) {
								continue;
							}
						} else if (creature != topCreature) {
							continue;
						}
					}

					if (!params.aggressive || (caster != creature && Combat::canDoCombat(caster, creature) == RETURNVALUE_NOERROR)) {
						for (const auto& condition : params.conditionList) {
							if (caster == creature || !creature->isImmune(condition->getType())) {
								Condition* conditionCopy = condition->clone();
								if (caster) {
									conditionCopy->setParam(CONDITION_PARAM_OWNER, caster->getID());
								}

								//TODO: infight condition until all aggressive conditions has ended
								creature->addCombatCondition(conditionCopy);
							}
						}
					}

					if (params.dispelType == CONDITION_PARALYZE) {
						creature->removeCondition(CONDITION_PARALYZE);
					} else {
						creature->removeCombatCondition(params.dispelType);
					}

					if (params.targetCallback) {
						params.targetCallback->onTargetCombat(caster, creature);
					}

					if (params.targetCasterOrTopMost) {
						break;
					}
				}
			}
		}
	}
}

void Combat::doTargetCombat(Creature* caster, Creature* target, CombatDamage& damage, const CombatParams& params)
{
	if (caster && target && params.distanceEffect != CONST_ANI_NONE) {
		addDistanceEffect(caster, caster->getPosition(), target->getPosition(), params.distanceEffect);
	}

	Player* casterPlayer = caster ? caster->getPlayer() : nullptr;

	bool success = false;
	if (damage.primary.type != COMBAT_MANADRAIN) {
		if (casterPlayer) {
			if (damage.primary.type == COMBAT_PHYSICALDAMAGE) {
				auto strength = casterPlayer->getCharacterStat(CHARSTAT_STRENGTH);
				if (strength != 0) {
					damage.primary.value += static_cast<int32_t>(round(damage.primary.value * (strength / 100.)));
				}
			}
			else {
				auto intelligence = casterPlayer->getCharacterStat(CHARSTAT_INTELLIGENCE);
				if (intelligence != 0) {
					damage.primary.value += static_cast<int32_t>(round(damage.primary.value * (intelligence / 100.)));
				}
			}

			if (damage.secondary.type == COMBAT_PHYSICALDAMAGE) {
				auto strength = casterPlayer->getCharacterStat(CHARSTAT_STRENGTH);
				if (strength != 0) {
					damage.secondary.value += static_cast<int32_t>(round(damage.secondary.value * (strength / 100.)));
				}
			}
			else {
				auto intelligence = casterPlayer->getCharacterStat(CHARSTAT_INTELLIGENCE);
				if (intelligence != 0) {
					damage.secondary.value += static_cast<int32_t>(round(damage.secondary.value * (intelligence / 100.)));
				}
			}
		}
		if (g_game.combatBlockHit(damage, caster, target, params.blockedByShield, params.blockedByArmor, params.itemId != 0, params.ignoreResistances)) {
			return;
		}

		if (casterPlayer) {
			Player* targetPlayer = target ? target->getPlayer() : nullptr;
			if (targetPlayer && casterPlayer != targetPlayer && targetPlayer->getSkull() != SKULL_BLACK && damage.primary.type != COMBAT_HEALING) {
				damage.primary.value /= 2;
				damage.secondary.value /= 2;
			}

			if (!damage.critical && damage.primary.type != COMBAT_HEALING && damage.origin != ORIGIN_CONDITION) {
				uint16_t chance = casterPlayer->getSpecialSkill(SPECIALSKILL_CRITICALHITCHANCE);
				uint16_t skill = casterPlayer->getSpecialSkill(SPECIALSKILL_CRITICALHITAMOUNT);
				if (chance > 0 && skill > 0 && normal_random(1, 100) <= chance) {
					damage.primary.value += std::round(damage.primary.value * (skill / 100.));
					damage.secondary.value += std::round(damage.secondary.value * (skill / 100.));
					damage.critical = true;
				}
			}
		}

		success = g_game.combatChangeHealth(caster, target, damage);
	} else {
		success = g_game.combatChangeMana(caster, target, damage);
	}

	if (success) {
		if (damage.blockType == BLOCK_NONE || damage.blockType == BLOCK_ARMOR) {
			for (const auto& condition : params.conditionList) {
				if (caster == target || !target->isImmune(condition->getType())) {
					Condition* conditionCopy = condition->clone();
					if (caster) {
						conditionCopy->setParam(CONDITION_PARAM_OWNER, caster->getID());
					}

					//TODO: infight condition until all aggressive conditions has ended
					target->addCombatCondition(conditionCopy);
				}
			}
		}

		if (damage.critical) {
			g_game.addMagicEffect(target->getPosition(), CONST_ME_CRITICAL_DAMAGE);
		}

		if (!damage.leeched && damage.primary.type != COMBAT_HEALING && casterPlayer && target != caster && damage.origin != ORIGIN_CONDITION) {
			CombatDamage leechCombat;
			leechCombat.origin = ORIGIN_NONE;
			leechCombat.leeched = true;

			int32_t totalDamage = std::abs(damage.primary.value + damage.secondary.value);

			if (casterPlayer->getHealth() < casterPlayer->getMaxHealth()) {
				uint16_t chance = casterPlayer->getSpecialSkill(SPECIALSKILL_LIFELEECHCHANCE);
				uint16_t skill = casterPlayer->getSpecialSkill(SPECIALSKILL_LIFELEECHAMOUNT);
				if (chance > 0 && skill > 0 && normal_random(1, 100) <= chance) {
					leechCombat.primary.value = std::round(totalDamage * (skill / 100.));
					g_game.combatChangeHealth(nullptr, casterPlayer, leechCombat);
					casterPlayer->sendMagicEffect(casterPlayer->getPosition(), CONST_ME_MAGIC_RED);
				}
			}

			if (casterPlayer->getMana() < casterPlayer->getMaxMana()) {
				uint16_t chance = casterPlayer->getSpecialSkill(SPECIALSKILL_MANALEECHCHANCE);
				uint16_t skill = casterPlayer->getSpecialSkill(SPECIALSKILL_MANALEECHAMOUNT);
				if (chance > 0 && skill > 0 && normal_random(1, 100) <= chance) {
					leechCombat.primary.value = std::round(totalDamage * (skill / 100.));
					g_game.combatChangeMana(nullptr, casterPlayer, leechCombat);
					casterPlayer->sendMagicEffect(casterPlayer->getPosition(), CONST_ME_MAGIC_BLUE);
				}
			}
		}

		if (params.dispelType == CONDITION_PARALYZE) {
			target->removeCondition(CONDITION_PARALYZE);
		} else {
			target->removeCombatCondition(params.dispelType);
		}
	}

	if (params.targetCallback) {
		params.targetCallback->onTargetCombat(caster, target);
	}
}

void Combat::doAreaCombat(Creature* caster, const Position& position, const AreaCombat* area, CombatDamage& damage, const CombatParams& params)
{
	auto tiles = caster ? getCombatArea(caster->getPosition(), position, area) : getCombatArea(position, position, area);

	Player* casterPlayer = caster ? caster->getPlayer() : nullptr;
	int32_t criticalPrimary = 0;
	int32_t criticalSecondary = 0;
	if (!damage.critical && damage.primary.type != COMBAT_HEALING && casterPlayer && damage.origin != ORIGIN_CONDITION) {
		uint16_t chance = casterPlayer->getSpecialSkill(SPECIALSKILL_CRITICALHITCHANCE);
		uint16_t skill = casterPlayer->getSpecialSkill(SPECIALSKILL_CRITICALHITAMOUNT);
		if (chance > 0 && skill > 0 && uniform_random(1, 100) <= chance) {
			criticalPrimary = std::round(damage.primary.value * (skill / 100.0));
			criticalSecondary = std::round(damage.secondary.value * (skill / 100.0));
			damage.critical = true;
		}
	}
	uint32_t maxX = 0;
	uint32_t maxY = 0;
	for (Tile* tile : tiles) {
		const Position& tilePos = tile->getPosition();
		uint32_t diffX = Position::getDistanceX(tilePos, position);
		uint32_t diffY = Position::getDistanceY(tilePos, position);
		if (diffX > maxX) {
			maxX = diffX;
		}
		if (diffY > maxY) {
			maxY = diffY;
		}
	}
	const int32_t rangeX = maxX + Map::maxViewportX;
	const int32_t rangeY = maxY + Map::maxViewportY;
	SpectatorVec spectators;
	g_game.map.getSpectators(spectators, position, true, true, rangeX, rangeX, rangeY, rangeY);
	postCombatEffects(caster, position, params);
	std::vector<Creature*> toDamageCreatures;
	toDamageCreatures.reserve(100);
	for (Tile* tile : tiles) {
		if (canDoCombat(caster, tile, params.aggressive) != RETURNVALUE_NOERROR) {
			continue;
		}
		combatTileEffects(spectators, caster, tile, params);
		if (CreatureVector* creatures = tile->getCreatures()) {
			const Creature* topCreature = tile->getTopCreature();
			for (Creature* creature : *creatures) {
				if (params.targetCasterOrTopMost) {
					if (caster && caster->getTile() == tile) {
						if (creature != caster) {
							continue;
						}
					}
					else if (creature != topCreature) {
						continue;
					}
				}
				if (!params.aggressive || (caster != creature && Combat::canDoCombat(caster, creature) == RETURNVALUE_NOERROR)) {
					toDamageCreatures.push_back(creature);
					if (params.targetCasterOrTopMost) {
						break;
					}
				}
			}
		}
	}
	int32_t maxDamageFound = 0;
	uint32_t totalTargets = 0;
	for (Creature* creature : toDamageCreatures) {
		CombatDamage damageCopy = damage;
		if (casterPlayer) {
			if (damageCopy.primary.type == COMBAT_PHYSICALDAMAGE) {
				auto strength = casterPlayer->getCharacterStat(CHARSTAT_STRENGTH);
				if (strength != 0) {
					damageCopy.primary.value += static_cast<int32_t>(std::round(damageCopy.primary.value * (strength / 230.)));
				}
			}
			else {
				auto intelligence = casterPlayer->getCharacterStat(CHARSTAT_INTELLIGENCE);
				if (intelligence != 0) {
					damageCopy.primary.value += static_cast<int32_t>(std::round(damageCopy.primary.value * (intelligence / 230.)));
				}
			}
			if (damageCopy.secondary.type == COMBAT_PHYSICALDAMAGE) {
				auto strength = casterPlayer->getCharacterStat(CHARSTAT_STRENGTH);
				if (strength != 0) {
					damageCopy.secondary.value += static_cast<int32_t>(std::round(damageCopy.secondary.value * (strength / 230.)));
				}
			}
			else {
				auto intelligence = casterPlayer->getCharacterStat(CHARSTAT_INTELLIGENCE);
				if (intelligence != 0) {
					damageCopy.secondary.value += static_cast<int32_t>(std::round(damageCopy.secondary.value * (intelligence / 230.)));
				}
			}
		}
		bool playerCombatReduced = false;
		if ((damageCopy.primary.value < 0 || damageCopy.secondary.value < 0) && caster) {
			Player* targetPlayer = creature->getPlayer();
			if (casterPlayer && targetPlayer && casterPlayer != targetPlayer && targetPlayer->getSkull() != SKULL_BLACK) {
				damageCopy.primary.value /= 2;
				damageCopy.secondary.value /= 2;
				playerCombatReduced = true;
			}
		}
		if (damageCopy.critical) {
			damageCopy.primary.value += playerCombatReduced ? (criticalPrimary / 2) : criticalPrimary;
			damageCopy.secondary.value += playerCombatReduced ? (criticalSecondary / 2) : criticalSecondary;
			g_game.addMagicEffect(creature->getPosition(), CONST_ME_CRITICAL_DAMAGE);
		}
		bool success = false;
		if (damageCopy.primary.type != COMBAT_MANADRAIN) {
			if (g_game.combatBlockHit(damageCopy, caster, creature,
				params.blockedByShield, params.blockedByArmor,
				params.itemId != 0, params.ignoreResistances))
			{
				continue;
			}
			success = g_game.combatChangeHealth(caster, creature, damageCopy);
		}
		else {
			success = g_game.combatChangeMana(caster, creature, damageCopy);
		}
		if (success) {
			int32_t dmgDealt = std::abs(damageCopy.primary.value + damageCopy.secondary.value);
			if (dmgDealt > maxDamageFound) {
				maxDamageFound = dmgDealt;
			}
			++totalTargets;
			if (damage.blockType == BLOCK_NONE || damage.blockType == BLOCK_ARMOR) {
				for (const auto& condition : params.conditionList) {
					if (caster == creature || !creature->isImmune(condition->getType())) {
						Condition* conditionCopy = condition->clone();
						if (caster) {
							conditionCopy->setParam(CONDITION_PARAM_OWNER, caster->getID());
						}
						creature->addCombatCondition(conditionCopy);
					}
				}
			}
			if (params.dispelType == CONDITION_PARALYZE) {
				creature->removeCondition(CONDITION_PARALYZE);
			}
			else {
				creature->removeCombatCondition(params.dispelType);
			}
			if (params.targetCallback) {
				params.targetCallback->onTargetCombat(caster, creature);
			}
		}
		//      Apply leech using the highest single damage + 10% per extra target
		//      If we found a maxDamageFound, we do:
		//      Base = maxDamageFound * (skill%).
		//      Extras = (totalTargets - 1) * (Base * 0.10).
		//      finalLeech = round(Base + Extras).
		//      And we obtain the correct base formula of: MaxDamageFound / Life/Mana Leech Amount and adding 10% extra for additional targets of the maxDamageFound 
		//      calculated from the base of life and mana leech of the player ex: 10% Life amount and 10% mana amount from highest damage 2000 of 10 shoots 
		//      with exevo gran mas vis should be: 200 + 20 + 20 ... and so on.
		if (casterPlayer && !damage.leeched &&
			damage.primary.type != COMBAT_HEALING &&
			damage.origin != ORIGIN_CONDITION &&
			totalTargets > 0 && maxDamageFound > 0)
		{
			damage.leeched = true;
			auto calcLeechValue = [&](uint16_t skillPercent, double extraPercentForOthers) -> int32_t {
				double base = maxDamageFound * (static_cast<double>(skillPercent) / 100.0);
				uint32_t extrasCount = (totalTargets > 1) ? (totalTargets - 1) : 0;
				double extras = base * (extraPercentForOthers / 100.0) * extrasCount;
				return static_cast<int32_t>(std::round(base + extras));
				};
			CombatDamage leechCombat;
			leechCombat.origin = ORIGIN_NONE;
			leechCombat.leeched = true;
			if (casterPlayer->getHealth() < casterPlayer->getMaxHealth()) {
				uint16_t chance = casterPlayer->getSpecialSkill(SPECIALSKILL_LIFELEECHCHANCE);
				uint16_t skill = casterPlayer->getSpecialSkill(SPECIALSKILL_LIFELEECHAMOUNT);
				if (chance > 0 && skill > 0 && normal_random(1, 100) <= chance) {
					int32_t finalLeech = calcLeechValue(skill, 10.0);
					leechCombat.primary.value = finalLeech;
					if (finalLeech > 0) {
						g_game.combatChangeHealth(nullptr, casterPlayer, leechCombat);
						casterPlayer->sendMagicEffect(casterPlayer->getPosition(), CONST_ME_MAGIC_RED);
					}
				}
			}
			if (casterPlayer->getMana() < casterPlayer->getMaxMana()) {
				uint16_t chance = casterPlayer->getSpecialSkill(SPECIALSKILL_MANALEECHCHANCE);
				uint16_t skill = casterPlayer->getSpecialSkill(SPECIALSKILL_MANALEECHAMOUNT);
				if (chance > 0 && skill > 0 && normal_random(1, 100) <= chance) {
					int32_t finalLeech = calcLeechValue(skill, 5.0);
					leechCombat.primary.value = finalLeech;
					if (finalLeech > 0) {
						g_game.combatChangeMana(nullptr, casterPlayer, leechCombat);
						casterPlayer->sendMagicEffect(casterPlayer->getPosition(), CONST_ME_MAGIC_BLUE);
					}
				}
			}
		}
	}
}

//**********************************************************//

void ValueCallback::getMinMaxValues(Player* player, CombatDamage& damage) const
{
	//onGetPlayerMinMaxValues(...)
	if (!scriptInterface->reserveScriptEnv()) {
		std::cout << "[Error - ValueCallback::getMinMaxValues] Call stack overflow" << std::endl;
		return;
	}

	ScriptEnvironment* env = scriptInterface->getScriptEnv();
	if (!env->setCallbackId(scriptId, scriptInterface)) {
		scriptInterface->resetScriptEnv();
		return;
	}

	lua_State* L = scriptInterface->getLuaState();

	scriptInterface->pushFunction(scriptId);

	LuaScriptInterface::pushUserdata<Player>(L, player);
	LuaScriptInterface::setMetatable(L, -1, "Player");

	int parameters = 1;
	switch (type) {
		case COMBAT_FORMULA_LEVELMAGIC: {
			//onGetPlayerMinMaxValues(player, level, maglevel)
			lua_pushnumber(L, player->getLevel());
			lua_pushnumber(L, player->getMagicLevel());
			parameters += 2;
			break;
		}

		case COMBAT_FORMULA_SKILL: {
			//onGetPlayerMinMaxValues(player, attackSkill, attackValue, attackFactor)
			Item* tool = player->getWeapon();
			const Weapon* weapon = g_weapons->getWeapon(tool);
			Item* item = nullptr;

			int32_t attackValue = 7;
			if (weapon) {
				attackValue = tool->getAttack();
				if (tool->getWeaponType() == WEAPON_AMMO) {
					item = player->getWeapon(true);
					if (item) {
						attackValue += item->getAttack();
					}
				}

				damage.secondary.type = weapon->getElementType();
				damage.secondary.value = weapon->getElementDamage(player, nullptr, tool);
			}

			lua_pushnumber(L, player->getWeaponSkill(item ? item : tool));
			lua_pushnumber(L, attackValue);
			lua_pushnumber(L, player->getAttackFactor());
			parameters += 3;
			break;
		}

		default: {
			std::cout << "ValueCallback::getMinMaxValues - unknown callback type" << std::endl;
			scriptInterface->resetScriptEnv();
			return;
		}
	}

	int size0 = lua_gettop(L);
	if (lua_pcall(L, parameters, 2, 0) != 0) {
		LuaScriptInterface::reportError(nullptr, LuaScriptInterface::popString(L));
	} else {
		damage.primary.value = normal_random(
			LuaScriptInterface::getNumber<int32_t>(L, -2),
			LuaScriptInterface::getNumber<int32_t>(L, -1)
		);
		lua_pop(L, 2);
	}

	if ((lua_gettop(L) + parameters + 1) != size0) {
		LuaScriptInterface::reportError(nullptr, "Stack size changed!");
	}

	scriptInterface->resetScriptEnv();
}

//**********************************************************//

void TileCallback::onTileCombat(Creature* creature, Tile* tile) const
{
	//onTileCombat(creature, pos)
	if (!scriptInterface->reserveScriptEnv()) {
		std::cout << "[Error - TileCallback::onTileCombat] Call stack overflow" << std::endl;
		return;
	}

	ScriptEnvironment* env = scriptInterface->getScriptEnv();
	if (!env->setCallbackId(scriptId, scriptInterface)) {
		scriptInterface->resetScriptEnv();
		return;
	}

	lua_State* L = scriptInterface->getLuaState();

	scriptInterface->pushFunction(scriptId);
	if (creature) {
		LuaScriptInterface::pushUserdata<Creature>(L, creature);
		LuaScriptInterface::setCreatureMetatable(L, -1, creature);
	} else {
		lua_pushnil(L);
	}
	LuaScriptInterface::pushPosition(L, tile->getPosition());

	scriptInterface->callFunction(2);
}

//**********************************************************//

void TargetCallback::onTargetCombat(Creature* creature, Creature* target) const
{
	//onTargetCombat(creature, target)
	if (!scriptInterface->reserveScriptEnv()) {
		std::cout << "[Error - TargetCallback::onTargetCombat] Call stack overflow" << std::endl;
		return;
	}

	ScriptEnvironment* env = scriptInterface->getScriptEnv();
	if (!env->setCallbackId(scriptId, scriptInterface)) {
		scriptInterface->resetScriptEnv();
		return;
	}

	lua_State* L = scriptInterface->getLuaState();

	scriptInterface->pushFunction(scriptId);

	if (creature) {
		LuaScriptInterface::pushUserdata<Creature>(L, creature);
		LuaScriptInterface::setCreatureMetatable(L, -1, creature);
	} else {
		lua_pushnil(L);
	}

	if (target) {
		LuaScriptInterface::pushUserdata<Creature>(L, target);
		LuaScriptInterface::setCreatureMetatable(L, -1, target);
	} else {
		lua_pushnil(L);
	}

	int size0 = lua_gettop(L);

	if (lua_pcall(L, 2, 0 /*nReturnValues*/, 0) != 0) {
		LuaScriptInterface::reportError(nullptr, LuaScriptInterface::popString(L));
	}

	if ((lua_gettop(L) + 2 /*nParams*/ + 1) != size0) {
		LuaScriptInterface::reportError(nullptr, "Stack size changed!");
	}

	scriptInterface->resetScriptEnv();
}

//**********************************************************//

MatrixArea MatrixArea::flip() const {
	Container newArr(arr.size());
	for (uint32_t i = 0; i < rows; ++i) {
		// assign rows, top to bottom, to the current rows, bottom to top
		newArr[std::slice(i * cols, cols, 1)] = arr[std::slice((rows - i - 1) * cols, cols, 1)];
	}
	return {{cols - center.first - 1, center.second}, rows, cols, std::move(newArr)};
}

MatrixArea MatrixArea::mirror() const {
	Container newArr(arr.size());
	for (uint32_t i = 0; i < cols; ++i) {
		// assign cols, left to right, to the current rows, right to left
		newArr[std::slice(i, cols, rows)] = arr[std::slice(cols - i - 1, cols, rows)];
	}
	return {{center.first, rows - center.second - 1}, rows, cols, std::move(newArr)};
}

MatrixArea MatrixArea::transpose() const {
	return {{center.second, center.first}, rows, cols, arr[std::gslice(0, {cols, rows}, {1, cols})]};
}

MatrixArea MatrixArea::rotate90() const {
	Container newArr(arr.size());
	for (uint32_t i = 0; i < rows; ++i) {
		// assign rows, top to bottom, to the current cols, right to left
		newArr[std::slice(i, cols, rows)] = arr[std::slice((rows - i - 1) * cols, cols, 1)];
	}
	return {{rows - center.second - 1, center.first}, cols, rows, std::move(newArr)};
}

MatrixArea MatrixArea::rotate180() const {
	Container newArr(arr.size());
	std::reverse_copy(std::begin(arr), std::end(arr), std::begin(newArr));
	return {{cols - center.first - 1, rows - center.second - 1}, rows, cols, std::move(newArr)};
}

MatrixArea MatrixArea::rotate270() const {
	Container newArr(arr.size());
	for (uint32_t i = 0; i < cols; ++i) {
		// assign cols, left to right, to the current rows, bottom to top
		newArr[std::slice(i * rows, rows, 1)] = arr[std::slice(cols - i - 1, rows, cols)];
	}
	return {{center.second, cols - center.first - 1}, cols, rows, std::move(newArr)};
}

const MatrixArea& AreaCombat::getArea(const Position& centerPos, const Position& targetPos) const {
	int32_t dx = Position::getOffsetX(targetPos, centerPos);
	int32_t dy = Position::getOffsetY(targetPos, centerPos);

	Direction dir;
	if (dx < 0) {
		dir = DIRECTION_WEST;
	} else if (dx > 0) {
		dir = DIRECTION_EAST;
	} else if (dy < 0) {
		dir = DIRECTION_NORTH;
	} else {
		dir = DIRECTION_SOUTH;
	}

	if (hasExtArea) {
		if (dx < 0 && dy < 0) {
			dir = DIRECTION_NORTHWEST;
		} else if (dx > 0 && dy < 0) {
			dir = DIRECTION_NORTHEAST;
		} else if (dx < 0 && dy > 0) {
			dir = DIRECTION_SOUTHWEST;
		} else if (dx > 0 && dy > 0) {
			dir = DIRECTION_SOUTHEAST;
		}
	}

	if (dir >= areas.size()) {
		// this should not happen. it means we forgot to call setupArea.
		static MatrixArea empty;
		return empty;
	}
	return areas[dir];
}

void AreaCombat::setupArea(const std::vector<uint32_t>& vec, uint32_t rows)
{
	auto area = createArea(vec, rows);
	if (areas.size() == 0) {
		areas.resize(4);
	}

	areas[DIRECTION_EAST] = area.rotate90();
	areas[DIRECTION_SOUTH] = area.rotate180();
	areas[DIRECTION_WEST] = area.rotate270();
	areas[DIRECTION_NORTH] = std::move(area);
}

void AreaCombat::setupArea(int32_t length, int32_t spread)
{
	uint32_t rows = length;
	int32_t cols = 1;

	if (spread != 0) {
		cols = ((length - (length % spread)) / spread) * 2 + 1;
	}

	int32_t colSpread = cols;

	std::vector<uint32_t> vec;
	vec.reserve(rows * cols);
	for (uint32_t y = 1; y <= rows; ++y) {
		int32_t mincol = cols - colSpread + 1;
		int32_t maxcol = cols - (cols - colSpread);

		for (int32_t x = 1; x <= cols; ++x) {
			if (y == rows && x == ((cols - (cols % 2)) / 2) + 1) {
				vec.push_back(3);
			} else if (x >= mincol && x <= maxcol) {
				vec.push_back(1);
			} else {
				vec.push_back(0);
			}
		}

		if (spread > 0 && y % spread == 0) {
			--colSpread;
		}
	}

	setupArea(vec, rows);
}

void AreaCombat::setupArea(int32_t radius)
{
	int32_t area[13][13] = {
		{0, 0, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0},
		{0, 0, 0, 0, 8, 8, 7, 8, 8, 0, 0, 0, 0},
		{0, 0, 0, 8, 7, 6, 6, 6, 7, 8, 0, 0, 0},
		{0, 0, 8, 7, 6, 5, 5, 5, 6, 7, 8, 0, 0},
		{0, 8, 7, 6, 5, 4, 4, 4, 5, 6, 7, 8, 0},
		{0, 8, 6, 5, 4, 3, 2, 3, 4, 5, 6, 8, 0},
		{8, 7, 6, 5, 4, 2, 1, 2, 4, 5, 6, 7, 8},
		{0, 8, 6, 5, 4, 3, 2, 3, 4, 5, 6, 8, 0},
		{0, 8, 7, 6, 5, 4, 4, 4, 5, 6, 7, 8, 0},
		{0, 0, 8, 7, 6, 5, 5, 5, 6, 7, 8, 0, 0},
		{0, 0, 0, 8, 7, 6, 6, 6, 7, 8, 0, 0, 0},
		{0, 0, 0, 0, 8, 8, 7, 8, 8, 0, 0, 0, 0},
		{0, 0, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0}
	};

	std::vector<uint32_t> vec;
	vec.reserve(13 * 13);
	for (auto& row : area) {
		for (int cell : row) {
			if (cell == 1) {
				vec.push_back(3);
			} else if (cell > 0 && cell <= radius) {
				vec.push_back(1);
			} else {
				vec.push_back(0);
			}
		}
	}

	setupArea(vec, 13);
}

void AreaCombat::setupExtArea(const std::vector<uint32_t>& vec, uint32_t rows)
{
	if (vec.empty()) {
		return;
	}

	hasExtArea = true;
	auto area = createArea(vec, rows);
	areas.resize(8);
	areas[DIRECTION_NORTHEAST] = area.mirror();
	areas[DIRECTION_SOUTHWEST] = area.flip();
	areas[DIRECTION_SOUTHEAST] = area.transpose();
	areas[DIRECTION_NORTHWEST] = std::move(area);
}

//**********************************************************//

void MagicField::onStepInField(Creature* creature)
{
	//remove magic walls/wild growth
	if (id == ITEM_MAGICWALL || id == ITEM_WILDGROWTH || id == ITEM_MAGICWALL_SAFE || id == ITEM_WILDGROWTH_SAFE || isBlocking()) {
		if (!creature->isInGhostMode()) {
			g_game.internalRemoveItem(this, 1);
		}

		return;
	}

	//remove magic walls/wild growth (only nopvp tiles/world)
	if (id == ITEM_MAGICWALL_NOPVP || id == ITEM_WILDGROWTH_NOPVP) {
		if (g_game.getWorldType() == WORLD_TYPE_NO_PVP || getTile()->hasFlag(TILESTATE_NOPVPZONE)) {
			g_game.internalRemoveItem(this, 1);
		}
		return;
	}

	const ItemType& it = items[getID()];
	if (it.conditionDamage) {
		Condition* conditionCopy = it.conditionDamage->clone();
		uint32_t ownerId = getOwner();
		if (ownerId) {
			bool harmfulField = true;

			if (g_game.getWorldType() == WORLD_TYPE_NO_PVP || getTile()->hasFlag(TILESTATE_NOPVPZONE)) {
				Creature* owner = g_game.getCreatureByID(ownerId);
				if (owner) {
					if (owner->getPlayer() || (owner->isSummon() && owner->getMaster()->getPlayer())) {
						harmfulField = false;
					}
				}
			}

			Player* targetPlayer = creature->getPlayer();
			if (targetPlayer) {
				Player* attackerPlayer = g_game.getPlayerByID(ownerId);
				if (attackerPlayer) {
					if (Combat::isProtected(attackerPlayer, targetPlayer)) {
						harmfulField = false;
					}
				}
			}

			if (!harmfulField || (OTSYS_TIME() - createTime <= 5000) || creature->hasBeenAttacked(ownerId)) {
				conditionCopy->setParam(CONDITION_PARAM_OWNER, ownerId);
			}
		}

		creature->addCondition(conditionCopy);
	}
}
