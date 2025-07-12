// Copyright 2022 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "item.h"
#include "container.h"
#include "teleport.h"
#include "trashholder.h"
#include "mailbox.h"
#include "house.h"
#include "game.h"
#include "bed.h"
#include <vector>
#include <list>
#include <tuple>
#include <string>
#include <random>
#include <algorithm>
#include <cstdint>

#include "actions.h"
#include "spells.h"

extern Game g_game;
extern Spells* g_spells;
extern Vocations g_vocations;

Items Item::items;

struct RarityAttributes {
    int numAbsorbs;
    int numSkills;
    int numSpecials;
    int numElements;
};

void Item::applyRarityEffects(Item* item) {
    const auto rarityAttr = item->getCustomAttribute("rarity");
    if (!rarityAttr) {
        return;
    }

    int rarityId = 0;
    const auto& value = rarityAttr->value;
    if (value.type() == typeid(int64_t)) {
        rarityId = boost::get<int64_t>(value);
    }
    if (rarityId <= 0) {
        return;
    }

    struct Rarity {
        int value;
        double chance;
        int minLevel;
        int maxLevel;
        int minBonus;
        int maxBonus;
    };
    std::vector<Rarity> rarities = {
        {1,   15.0,   1,   10,   1,   1},
        {2,   14.0,  11,   20,   1,   2},
        {3,   13.0,  21,   30,   1,   2},
        {4,   12.0,  31,   40,   1,   3},
        {5,   10.0,  41,   50,   1,   3},
        {6,    8.0,  51,   60,   2,   4},
        {7,    7.0,  61,   70,   2,   5},
        {8,    6.0,  71,   80,   3,   6},
        {9,    5.0,  81,   90,   3,   7},
        {10,   4.5,  91,  100,   4,   8},
        {11,   3.3, 101,  110,   4,   9},
        {12,   2.2, 111,  120,   5,  10},
        {13,   1.8, 121,  130,   6,  10},
        {14,   1.3, 131,  140,   7,  12},
        {15,   1.0, 141,  150,   9,  15}
    };

    std::vector<RarityAttributes> rarityAttributes = {
        {1, 1, 0, 0}, // Rarity 1
        {1, 1, 0, 1}, // Rarity 2
        {1, 1, 0, 1}, // Rarity 3
        {1, 1, 1, 1}, // Rarity 4
        {1, 1, 1, 1}, // Rarity 5
        {1, 2, 1, 1}, // Rarity 6
        {1, 2, 1, 1}, // Rarity 7
        {1, 2, 2, 1}, // Rarity 8
        {2, 2, 2, 1}, // Rarity 9
        {2, 2, 2, 1}, // Rarity 10
        {2, 2, 2, 1}, // Rarity 11
        {2, 2, 2, 1}, // Rarity 12
        {2, 2, 2, 1}, // Rarity 13
        {3, 3, 2, 1}, // Rarity 14
        {3, 3, 2, 1}  // Rarity 15
    };

    std::vector<std::pair<int, int>> absorptionBonuses = {
        {1, 1}, {1, 1}, {1, 1}, {1, 2}, {1, 2},
        {1, 3}, {1, 3}, {1, 4}, {1, 4}, {2, 5},
        {2, 5}, {2, 5}, {3, 6}, {4, 6}, {5, 6}
    };

    std::vector<std::pair<int, int>> elementBonuses = {
        {1, 3}, {3, 6}, {4, 8}, {5, 10}, {6, 12},
        {7, 14}, {8, 16}, {9, 18}, {10, 20}, {12, 22},
        {14, 24}, {16, 26}, {18, 28}, {20, 30}, {25, 40}
    };

    const ItemType& it = Item::items[item->getID()];

    if (item->getCustomAttribute("combatPowerLevel"))
        return;

    const uint32_t VALID_EQUIP_SLOTS =
        SLOTP_HEAD |
        SLOTP_NECKLACE |
        SLOTP_ARMOR |
        SLOTP_RIGHT |
        SLOTP_LEFT |
        SLOTP_LEGS |
        SLOTP_FEET |
        SLOTP_RING |
        SLOTP_DECKBAD |
        SLOTP_BELT |
        SLOTP_GLOVES;

    if ((it.slotPosition & VALID_EQUIP_SLOTS) == 0)
        return;

    bool isWeapon = (it.weaponType != WEAPON_NONE);

    int finalLevel = 0, minBonus = 0, maxBonus = 0;
    for (const Rarity& r : rarities) {
        if (r.value == rarityId) {
            finalLevel = uniform_random(r.minLevel, r.maxLevel);
            minBonus = r.minBonus;
            maxBonus = r.maxBonus;
            break;
        }
    }
    int bonusRange = maxBonus - minBonus + 1;
    int finalBonus = minBonus + (finalLevel * bonusRange / 100);

    if (it.attack > 0) {
        int64_t newAttack = it.attack + finalBonus;
        item->setIntAttr(ITEM_ATTRIBUTE_ATTACK, newAttack);
    }
    if (it.defense > 0) {
        int64_t newDefense = it.defense + finalBonus;
        item->setIntAttr(ITEM_ATTRIBUTE_DEFENSE, newDefense);
    }
    if (it.armor > 0) {
        int64_t newArmor = it.armor + finalBonus;
        item->setIntAttr(ITEM_ATTRIBUTE_ARMOR, newArmor);
    }
    if (it.hitChance > 0) {
        int64_t newHitChance = it.hitChance + finalBonus;
        item->setIntAttr(ITEM_ATTRIBUTE_HITCHANCE, newHitChance);
    }

    std::string levelKey = "combatPowerLevel";
        item->setCustomAttribute(levelKey, static_cast<int64_t>(finalLevel));

        RarityAttributes attrCounts = rarityAttributes[rarityId - 1];

        std::vector<std::tuple<CombatType_t, std::string, std::string>> absorptionTypes = {
            {COMBAT_PHYSICALDAMAGE, "rarity_physicalAbsorb", "Physical"},
            {COMBAT_ENERGYDAMAGE, "rarity_energyAbsorb", "Energy"},
            {COMBAT_EARTHDAMAGE, "rarity_earthAbsorb", "Earth"},
            {COMBAT_FIREDAMAGE, "rarity_fireAbsorb", "Fire"},
            {COMBAT_DROWNDAMAGE, "rarity_drownAbsorb", "Drown"},
            {COMBAT_ICEDAMAGE, "rarity_iceAbsorb", "Ice"},
            {COMBAT_HOLYDAMAGE, "rarity_holyAbsorb", "Holy"},
            {COMBAT_DEATHDAMAGE, "rarity_deathAbsorb", "Death"},
            {COMBAT_WATERDAMAGE, "rarity_waterAbsorb", "Water"},
            {COMBAT_ARCANEDAMAGE, "rarity_arcaneAbsorb", "Arcane"}
        };

        std::vector<std::tuple<int, std::string, std::string>> skillTypes = {
            {SKILL_FIST, "rarity_fistSkill", "Fist"},
            {SKILL_CLUB, "rarity_clubSkill", "Club"},
            {SKILL_SWORD, "rarity_swordSkill", "Sword"},
            {SKILL_AXE, "rarity_axeSkill", "Axe"},
            {SKILL_DISTANCE, "rarity_distanceSkill", "Distance"},
            {SKILL_SHIELD, "rarity_shieldSkill", "Shielding"},
            {SKILL_FISHING, "rarity_fishingSkill", "Fishing"},
            {SKILL_CRAFTING, "rarity_craftingSkill", "Crafting"},
            {SKILL_WOODCUTTING, "rarity_woodcuttingSkill", "Woodcutting"},
            {SKILL_MINING, "rarity_miningSkill", "Mining"},
            {SKILL_HERBALIST, "rarity_herbalistSkill", "Herbalism"},
            {SKILL_ARMORSMITH, "rarity_armorsmithSkill", "Armorsmithing"},
            {SKILL_WEAPONSMITH, "rarity_weaponsmithSkill", "Weaponsmithing"},
            {SKILL_JEWELSMITH, "rarity_jewelsmithSkill", "Jewelsmithing"},
            {SKILL_MAGLEVEL, "rarity_maglevelSkill", "Magic"}
        };

        std::vector<std::tuple<int, std::string, std::string>> specialSkillTypes = {
            {SPECIALSKILL_CRITICALHITCHANCE, "rarity_criticalHitChance", "Critical Chance"},
            {SPECIALSKILL_CRITICALHITAMOUNT, "rarity_criticalHitAmount", "Critical Hit"},
            {SPECIALSKILL_MANALEECHCHANCE, "rarity_manaLeechChance", "Mana Chance"},
            {SPECIALSKILL_MANALEECHAMOUNT, "rarity_manaLeechAmount", "Mana Amount"},
            {SPECIALSKILL_LIFELEECHCHANCE, "rarity_lifeLeechChance", "Life Chance"},
            {SPECIALSKILL_LIFELEECHAMOUNT, "rarity_lifeLeechAmount", "Life Amount"}
        };

        std::vector<std::tuple<CombatType_t, std::string, std::string>> elementTypes = {
            {COMBAT_FIREDAMAGE,  "rarity_elementfire",  "Fire"},
            {COMBAT_ICEDAMAGE,   "rarity_elementice",   "Ice"},
            {COMBAT_ENERGYDAMAGE,"rarity_elementenergy", "Energy"},
            {COMBAT_DEATHDAMAGE, "rarity_elementdeath", "Death"},
            {COMBAT_EARTHDAMAGE, "rarity_elementearth", "Earth"},
            {COMBAT_WATERDAMAGE, "rarity_elementwater", "Water"},
            {COMBAT_ARCANEDAMAGE,"rarity_elementarcane", "Arcane"},
            {COMBAT_HOLYDAMAGE,  "rarity_elementholy",  "Holy"}
        };

        std::string absorptionDesc;
        if (attrCounts.numAbsorbs > 0) {
            std::mt19937 rng(std::random_device{}());
            std::shuffle(absorptionTypes.begin(), absorptionTypes.end(), rng);
            for (int i = 0; i < attrCounts.numAbsorbs && i < (int)absorptionTypes.size(); ++i) {
                auto [ctype, key, name] = absorptionTypes[i];
                const auto& range = absorptionBonuses[rarityId - 1];
                int bonus = uniform_random(range.first, range.second);
                std::string attrKey = key;
                item->setCustomAttribute(attrKey, static_cast<int64_t>(bonus));
                absorptionDesc += (absorptionDesc.empty() ? "" : ", ") + name + " +" + std::to_string(bonus) + "%";
            }
        }

        std::string skillsDesc;
        if (attrCounts.numSkills > 0) {
            std::mt19937 rng(std::random_device{}());
            std::shuffle(skillTypes.begin(), skillTypes.end(), rng);
            for (int i = 0; i < attrCounts.numSkills && i < (int)skillTypes.size(); ++i) {
                auto [skillId, key, name] = skillTypes[i];
                const auto& range = absorptionBonuses[rarityId - 1];
                int bonus = uniform_random(range.first, range.second);
                std::string attrKey = key;
                item->setCustomAttribute(attrKey, static_cast<int64_t>(bonus));
                skillsDesc += (skillsDesc.empty() ? "" : ", ") + name + " +" + std::to_string(bonus);
            }
        }

        std::string specialsDesc;
		bool criticalApplied = false;
		bool manaApplied = false;
		bool lifeApplied = false;

		for (int i = 0; i < attrCounts.numSpecials && i < (int)specialSkillTypes.size(); ++i) {
    		auto [specialId, key, name] = specialSkillTypes[i];
    	if (specialId == SPECIALSKILL_CRITICALHITCHANCE || specialId == SPECIALSKILL_CRITICALHITAMOUNT) {
        	if (!criticalApplied) {
            	const auto& range = absorptionBonuses[rarityId - 1];
            	int bonus = uniform_random(range.first, range.second);
            	std::string critChanceKey = "rarity_criticalHitChance";
            	std::string critHitKey    = "rarity_criticalHitAmount";
            	item->setCustomAttribute(critChanceKey, static_cast<int64_t>(bonus));
            	item->setCustomAttribute(critHitKey,    static_cast<int64_t>(bonus));
            	specialsDesc += (specialsDesc.empty() ? "" : ", ") +
                            std::string("Critical Chance +") + std::to_string(bonus) + "%";
            	specialsDesc += ", " +
                            std::string("Critical Hit +") + std::to_string(bonus) + "%";
            	criticalApplied = true;
        	}
        	continue;
    	}
    	if (specialId == SPECIALSKILL_MANALEECHCHANCE || specialId == SPECIALSKILL_MANALEECHAMOUNT) {
        	if (!manaApplied) {
            	const auto& range = absorptionBonuses[rarityId - 1];
            	int bonus = uniform_random(range.first, range.second);
            	std::string manaChanceKey = "rarity_manaLeechChance";
            	std::string manaAmountKey = "rarity_manaLeechAmount";
            	item->setCustomAttribute(manaChanceKey, static_cast<int64_t>(bonus));
            	item->setCustomAttribute(manaAmountKey, static_cast<int64_t>(bonus));
            	specialsDesc += (specialsDesc.empty() ? "" : ", ") +
                            std::string("Mana Chance +") + std::to_string(bonus) + "%";
            	specialsDesc += ", " +
                            std::string("Mana Amount +") + std::to_string(bonus) + "%";
            	manaApplied = true;
        	}
        	continue;
    	}
    	if (specialId == SPECIALSKILL_LIFELEECHCHANCE || specialId == SPECIALSKILL_LIFELEECHAMOUNT) {
        	if (!lifeApplied) {
            	const auto& range = absorptionBonuses[rarityId - 1];
            	int bonus = uniform_random(range.first, range.second);
            	std::string lifeChanceKey = "rarity_lifeLeechChance";
            	std::string lifeAmountKey = "rarity_lifeLeechAmount";
            	item->setCustomAttribute(lifeChanceKey, static_cast<int64_t>(bonus));
            	item->setCustomAttribute(lifeAmountKey, static_cast<int64_t>(bonus));
            	specialsDesc += (specialsDesc.empty() ? "" : ", ") +
                            std::string("Life Chance +") + std::to_string(bonus) + "%";
            	specialsDesc += ", " +
                            std::string("Life Amount +") + std::to_string(bonus) + "%";
            	lifeApplied = true;
        	}
        	continue;
    	}
    	const auto& range = absorptionBonuses[rarityId - 1];
    	int bonus = uniform_random(range.first, range.second);
    	std::string attrKey = key;
    	item->setCustomAttribute(attrKey, static_cast<int64_t>(bonus));
    	specialsDesc += (specialsDesc.empty() ? "" : ", ") + name + " +" + std::to_string(bonus) + "%";
		}

        std::string elementDesc;
		if (isWeapon && it.attack > 0 && attrCounts.numElements > 0) {
    		if (!it.abilities || it.abilities->elementDamage <= 0) {
        		std::mt19937 rng(std::random_device{}());
        		std::shuffle(elementTypes.begin(), elementTypes.end(), rng);
        		for (int i = 0; i < attrCounts.numElements && i < (int)elementTypes.size(); ++i) {
            		auto [ctype, key, name] = elementTypes[i];
            		const auto& range = elementBonuses[rarityId - 1];
            		int bonus = uniform_random(range.first, range.second);
            		std::string attrKey = key;
            		item->setCustomAttribute(attrKey, static_cast<int64_t>(bonus));
            		elementDesc += (elementDesc.empty() ? "" : ", ") + name + " +" + std::to_string(bonus) + "% dmg";
        		}
    		}
		}

        std::string description = item->getStrAttr(ITEM_ATTRIBUTE_DESCRIPTION);
        if (!description.empty()) {
            description += ", ";
        }
        description += "Level: " + std::to_string(finalLevel);
        if (!absorptionDesc.empty()) {
            description += ", " + absorptionDesc;
        }
        if (!skillsDesc.empty()) {
            description += ", " + skillsDesc;
        }
        if (!specialsDesc.empty()) {
            description += ", " + specialsDesc;
        }
        if (!elementDesc.empty()) {
            description += ", Element: " + elementDesc;
        }
        item->setStrAttr(ITEM_ATTRIBUTE_DESCRIPTION, description);
}

Item* Item::CreateItemWithRarity(const uint16_t type, uint16_t count, int rarityId) {
    if (rarityId <= 0) {
        return CreateItem(type, count);
    }
    Item* newItem = CreateItem(type, count);
    if (newItem) {
        std::string key = "rarity";
        newItem->setCustomAttribute(key, static_cast<int64_t>(rarityId));
        applyRarityEffects(newItem);
    }
    return newItem;
}

Item* Item::CreateItem(const uint16_t type, uint16_t count /*= 0*/)
{
	Item* newItem = nullptr;

	const ItemType& it = Item::items[type];
	if (it.group == ITEM_GROUP_DEPRECATED) {
		return nullptr;
	}

	if (it.stackable && count == 0) {
		count = 1;
	}

	if (it.id != 0) {
		if (it.isDepot()) {
			newItem = new DepotLocker(type);
		} else if (it.isContainer()) {
			newItem = new Container(type);
		} else if (it.isTeleport()) {
			newItem = new Teleport(type);
		} else if (it.isMagicField()) {
			newItem = new MagicField(type);
		} else if (it.isDoor()) {
			newItem = new Door(type);
		} else if (it.isTrashHolder()) {
			newItem = new TrashHolder(type);
		} else if (it.isMailbox()) {
			newItem = new Mailbox(type);
		} else if (it.isBed()) {
			newItem = new BedItem(type);
		} else if (it.id >= 2210 && it.id <= 2212) { // magic rings
			newItem = new Item(type - 3, count);
		} else if (it.id == 2215 || it.id == 2216) { // magic rings
			newItem = new Item(type - 2, count);
		} else if (it.id >= 2202 && it.id <= 2206) { // magic rings
			newItem = new Item(type - 37, count);
		} else if (it.id == 2640) { // soft boots
			newItem = new Item(6132, count);
		} else if (it.id == 6301) { // death ring
			newItem = new Item(6300, count);
		} else if (it.id == 18528) { // prismatic ring
			newItem = new Item(18408, count);
		} else {
			newItem = new Item(type, count);
		}
		if(newItem && newItem->getCustomAttribute("rarity")) {
    		applyRarityEffects(newItem);
		}

		newItem->incrementReferenceCounter();
	}

	return newItem;
}

Container* Item::CreateItemAsContainer(const uint16_t type, uint16_t size)
{
	const ItemType& it = Item::items[type];
	if (it.id == 0 || it.group == ITEM_GROUP_DEPRECATED || it.stackable || it.useable || it.moveable || it.pickupable || it.isDepot() || it.isSplash() || it.isDoor()) {
		return nullptr;
	}

	Container* newItem = new Container(type, size);
	newItem->incrementReferenceCounter();
	return newItem;
}

Item* Item::CreateItem(PropStream& propStream)
{
	uint16_t id;
	if (!propStream.read<uint16_t>(id)) {
		return nullptr;
	}

	switch (id) {
		case ITEM_FIREFIELD_PVP_FULL:
			id = ITEM_FIREFIELD_PERSISTENT_FULL;
			break;

		case ITEM_FIREFIELD_PVP_MEDIUM:
			id = ITEM_FIREFIELD_PERSISTENT_MEDIUM;
			break;

		case ITEM_FIREFIELD_PVP_SMALL:
			id = ITEM_FIREFIELD_PERSISTENT_SMALL;
			break;

		case ITEM_ENERGYFIELD_PVP:
			id = ITEM_ENERGYFIELD_PERSISTENT;
			break;

		case ITEM_POISONFIELD_PVP:
			id = ITEM_POISONFIELD_PERSISTENT;
			break;

		case ITEM_MAGICWALL:
			id = ITEM_MAGICWALL_PERSISTENT;
			break;

		case ITEM_WILDGROWTH:
			id = ITEM_WILDGROWTH_PERSISTENT;
			break;

		default:
			break;
	}

	return Item::CreateItem(id, 0);
}

Item::Item(const uint16_t type, uint16_t count /*= 0*/) :
	id(type)
{
	const ItemType& it = items[id];

	if (it.isFluidContainer() || it.isSplash()) {
		setFluidType(count);
	} else if (it.stackable) {
		if (count != 0) {
			setItemCount(count);
		} else if (it.charges != 0) {
			setItemCount(it.charges);
		}
	} else if (it.charges != 0) {
		if (count != 0) {
			setCharges(count);
		} else {
			setCharges(it.charges);
		}
	}

	setDefaultDuration();
}

Item::Item(const Item& i) :
	Thing(), id(i.id), count(i.count), loadedFromMap(i.loadedFromMap)
{
	if (i.attributes) {
		attributes.reset(new ItemAttributes(*i.attributes));
	}
}

Item* Item::clone() const
{
	Item* item = Item::CreateItem(id, count);
	if (attributes) {
		item->attributes.reset(new ItemAttributes(*attributes));
		if (item->getDuration() > 0) {
			item->incrementReferenceCounter();
			item->setDecaying(DECAYING_TRUE);
			g_game.toDecayItems.push_front(item);
		}
	}
	return item;
}

bool Item::equals(const Item* otherItem) const
{
	if (!otherItem || id != otherItem->id) {
		return false;
	}

	const auto& otherAttributes = otherItem->attributes;
	if (!attributes) {
		return !otherAttributes || (otherAttributes->attributeBits == 0);
	} else if (!otherAttributes) {
		return (attributes->attributeBits == 0);
	}

	if (attributes->attributeBits != otherAttributes->attributeBits) {
		return false;
	}

	const auto& attributeList = attributes->attributes;
	const auto& otherAttributeList = otherAttributes->attributes;
	for (const auto& attribute : attributeList) {
		if (ItemAttributes::isIntAttrType(attribute.type)) {
			for (const auto& otherAttribute : otherAttributeList) {
				if (attribute.type == otherAttribute.type && attribute.value.integer != otherAttribute.value.integer) {
					return false;
				}
			}
		} else if (ItemAttributes::isStrAttrType(attribute.type)) {
			for (const auto& otherAttribute : otherAttributeList) {
				if (attribute.type == otherAttribute.type && *attribute.value.string != *otherAttribute.value.string) {
					return false;
				}
			}
		} else {
			for (const auto& otherAttribute : otherAttributeList) {
				if (attribute.type == otherAttribute.type && *attribute.value.custom != *otherAttribute.value.custom) {
					return false;
				}
			}
		}
	}
	return true;
}

void Item::setDefaultSubtype()
{
	const ItemType& it = items[id];

	setItemCount(1);

	if (it.charges != 0) {
		if (it.stackable) {
			setItemCount(it.charges);
		} else {
			setCharges(it.charges);
		}
	}
}

void Item::onRemoved()
{
	ScriptEnvironment::removeTempItem(this);

	if (hasAttribute(ITEM_ATTRIBUTE_UNIQUEID)) {
		g_game.removeUniqueItem(getUniqueId());
	}
}

void Item::setID(uint16_t newid)
{
	const ItemType& prevIt = Item::items[id];
	id = newid;

	const ItemType& it = Item::items[newid];
	uint32_t newDuration = it.decayTime * 1000;

	if (newDuration == 0 && !it.stopTime && it.decayTo < 0) {
		removeAttribute(ITEM_ATTRIBUTE_DECAYSTATE);
		removeAttribute(ITEM_ATTRIBUTE_DURATION);
	}

	removeAttribute(ITEM_ATTRIBUTE_CORPSEOWNER);

	if (newDuration > 0 && (!prevIt.stopTime || !hasAttribute(ITEM_ATTRIBUTE_DURATION))) {
		setDecaying(DECAYING_FALSE);
		setDuration(newDuration);
	}
}

Cylinder* Item::getTopParent()
{
	Cylinder* aux = getParent();
	Cylinder* prevaux = dynamic_cast<Cylinder*>(this);
	if (!aux) {
		return prevaux;
	}

	while (aux->getParent() != nullptr) {
		prevaux = aux;
		aux = aux->getParent();
	}

	if (prevaux) {
		return prevaux;
	}
	return aux;
}

const Cylinder* Item::getTopParent() const
{
	const Cylinder* aux = getParent();
	const Cylinder* prevaux = dynamic_cast<const Cylinder*>(this);
	if (!aux) {
		return prevaux;
	}

	while (aux->getParent() != nullptr) {
		prevaux = aux;
		aux = aux->getParent();
	}

	if (prevaux) {
		return prevaux;
	}
	return aux;
}

Tile* Item::getTile()
{
	Cylinder* cylinder = getTopParent();
	//get root cylinder
	if (cylinder && cylinder->getParent()) {
		cylinder = cylinder->getParent();
	}
	return dynamic_cast<Tile*>(cylinder);
}

const Tile* Item::getTile() const
{
	const Cylinder* cylinder = getTopParent();
	//get root cylinder
	if (cylinder && cylinder->getParent()) {
		cylinder = cylinder->getParent();
	}
	return dynamic_cast<const Tile*>(cylinder);
}

uint16_t Item::getSubType() const
{
	const ItemType& it = items[id];
	if (it.isFluidContainer() || it.isSplash()) {
		return getFluidType();
	} else if (it.stackable) {
		return count;
	} else if (it.charges != 0) {
		return getCharges();
	}
	return count;
}

Player* Item::getHoldingPlayer() const
{
	Cylinder* p = getParent();
	while (p) {
		if (p->getCreature()) {
			return p->getCreature()->getPlayer();
		}

		p = p->getParent();
	}
	return nullptr;
}

void Item::setSubType(uint16_t n)
{
	const ItemType& it = items[id];
	if (it.isFluidContainer() || it.isSplash()) {
		setFluidType(n);
	} else if (it.stackable) {
		setItemCount(n);
	} else if (it.charges != 0) {
		setCharges(n);
	} else {
		setItemCount(n);
	}
}

Attr_ReadValue Item::readAttr(AttrTypes_t attr, PropStream& propStream)
{
	switch (attr) {
		case ATTR_COUNT:
		case ATTR_RUNE_CHARGES: {
			uint8_t count;
			if (!propStream.read<uint8_t>(count)) {
				return ATTR_READ_ERROR;
			}

			setSubType(count);
			break;
		}

		case ATTR_ACTION_ID: {
			uint16_t actionId;
			if (!propStream.read<uint16_t>(actionId)) {
				return ATTR_READ_ERROR;
			}

			setActionId(actionId);
			break;
		}

		case ATTR_UNIQUE_ID: {
			uint16_t uniqueId;
			if (!propStream.read<uint16_t>(uniqueId)) {
				return ATTR_READ_ERROR;
			}

			setUniqueId(uniqueId);
			break;
		}

		case ATTR_TEXT: {
			std::string text;
			if (!propStream.readString(text)) {
				return ATTR_READ_ERROR;
			}

			setText(text);
			break;
		}

		case ATTR_WRITTENDATE: {
			uint32_t writtenDate;
			if (!propStream.read<uint32_t>(writtenDate)) {
				return ATTR_READ_ERROR;
			}

			setDate(writtenDate);
			break;
		}

		case ATTR_WRITTENBY: {
			std::string writer;
			if (!propStream.readString(writer)) {
				return ATTR_READ_ERROR;
			}

			setWriter(writer);
			break;
		}

		case ATTR_DESC: {
			std::string text;
			if (!propStream.readString(text)) {
				return ATTR_READ_ERROR;
			}

			setSpecialDescription(text);
			break;
		}

		case ATTR_CHARGES: {
			uint16_t charges;
			if (!propStream.read<uint16_t>(charges)) {
				return ATTR_READ_ERROR;
			}

			setSubType(charges);
			break;
		}

		case ATTR_DURATION: {
			int32_t duration;
			if (!propStream.read<int32_t>(duration)) {
				return ATTR_READ_ERROR;
			}

			setDuration(std::max<int32_t>(0, duration));
			break;
		}

		case ATTR_DECAYING_STATE: {
			uint8_t state;
			if (!propStream.read<uint8_t>(state)) {
				return ATTR_READ_ERROR;
			}

			if (state != DECAYING_FALSE) {
				setDecaying(DECAYING_PENDING);
			}
			break;
		}

		case ATTR_NAME: {
			std::string name;
			if (!propStream.readString(name)) {
				return ATTR_READ_ERROR;
			}

			setStrAttr(ITEM_ATTRIBUTE_NAME, name);
			break;
		}

		case ATTR_ARTICLE: {
			std::string article;
			if (!propStream.readString(article)) {
				return ATTR_READ_ERROR;
			}

			setStrAttr(ITEM_ATTRIBUTE_ARTICLE, article);
			break;
		}

		case ATTR_PLURALNAME: {
			std::string pluralName;
			if (!propStream.readString(pluralName)) {
				return ATTR_READ_ERROR;
			}

			setStrAttr(ITEM_ATTRIBUTE_PLURALNAME, pluralName);
			break;
		}

		case ATTR_WEIGHT: {
			uint32_t weight;
			if (!propStream.read<uint32_t>(weight)) {
				return ATTR_READ_ERROR;
			}

			setIntAttr(ITEM_ATTRIBUTE_WEIGHT, weight);
			break;
		}

		case ATTR_ATTACK: {
			int32_t attack;
			if (!propStream.read<int32_t>(attack)) {
				return ATTR_READ_ERROR;
			}

			setIntAttr(ITEM_ATTRIBUTE_ATTACK, attack);
			break;
		}

		case ATTR_ATTACK_SPEED: {
			uint32_t attackSpeed;
			if (!propStream.read<uint32_t>(attackSpeed)) {
				return ATTR_READ_ERROR;
			}

			setIntAttr(ITEM_ATTRIBUTE_ATTACK_SPEED, attackSpeed);
			break;
		}

		case ATTR_DEFENSE: {
			int32_t defense;
			if (!propStream.read<int32_t>(defense)) {
				return ATTR_READ_ERROR;
			}

			setIntAttr(ITEM_ATTRIBUTE_DEFENSE, defense);
			break;
		}

		case ATTR_EXTRADEFENSE: {
			int32_t extraDefense;
			if (!propStream.read<int32_t>(extraDefense)) {
				return ATTR_READ_ERROR;
			}

			setIntAttr(ITEM_ATTRIBUTE_EXTRADEFENSE, extraDefense);
			break;
		}

		case ATTR_ARMOR: {
			int32_t armor;
			if (!propStream.read<int32_t>(armor)) {
				return ATTR_READ_ERROR;
			}

			setIntAttr(ITEM_ATTRIBUTE_ARMOR, armor);
			break;
		}

		case ATTR_RARITY: {
    		int32_t rarity;
    		if (!propStream.read<int32_t>(rarity)) {
        		return ATTR_READ_ERROR;
    		}
    		setIntAttr(ITEM_ATTRIBUTE_RARITY, rarity);
    		break;
		}

		case ATTR_HITCHANCE: {
			int8_t hitChance;
			if (!propStream.read<int8_t>(hitChance)) {
				return ATTR_READ_ERROR;
			}

			setIntAttr(ITEM_ATTRIBUTE_HITCHANCE, hitChance);
			break;
		}

		case ATTR_SHOOTRANGE: {
			uint8_t shootRange;
			if (!propStream.read<uint8_t>(shootRange)) {
				return ATTR_READ_ERROR;
			}

			setIntAttr(ITEM_ATTRIBUTE_SHOOTRANGE, shootRange);
			break;
		}

		case ATTR_DECAYTO: {
			int32_t decayTo;
			if (!propStream.read<int32_t>(decayTo)) {
				return ATTR_READ_ERROR;
			}

			setIntAttr(ITEM_ATTRIBUTE_DECAYTO, decayTo);
			break;
		}

		case ATTR_WRAPID: {
			uint16_t wrapId;
			if (!propStream.read<uint16_t>(wrapId)) {
				return ATTR_READ_ERROR;
			}

			setIntAttr(ITEM_ATTRIBUTE_WRAPID, wrapId);
			break;
		}

		case ATTR_STOREITEM: {
			uint8_t storeItem;
			if (!propStream.read<uint8_t>(storeItem)) {
				return ATTR_READ_ERROR;
			}

			setIntAttr(ITEM_ATTRIBUTE_STOREITEM, storeItem);
			break;
		}

		//12+ compatibility
		case ATTR_OPENCONTAINER:
		case ATTR_TIER: {
			if (!propStream.skip(1)) {
				return ATTR_READ_ERROR;
			}
			break;
		}

		case ATTR_PODIUMOUTFIT: {
			if (!propStream.skip(15)) {
				return ATTR_READ_ERROR;
			}
			break;
		}

		//these should be handled through derived classes
		//If these are called then something has changed in the items.xml since the map was saved
		//just read the values

		//Depot class
		case ATTR_DEPOT_ID: {
			if (!propStream.skip(2)) {
				return ATTR_READ_ERROR;
			}
			break;
		}

		//Door class
		case ATTR_HOUSEDOORID: {
			if (!propStream.skip(1)) {
				return ATTR_READ_ERROR;
			}
			break;
		}

		//Bed class
		case ATTR_SLEEPERGUID: {
			if (!propStream.skip(4)) {
				return ATTR_READ_ERROR;
			}
			break;
		}

		case ATTR_SLEEPSTART: {
			if (!propStream.skip(4)) {
				return ATTR_READ_ERROR;
			}
			break;
		}

		//Teleport class
		case ATTR_TELE_DEST: {
			if (!propStream.skip(5)) {
				return ATTR_READ_ERROR;
			}
			break;
		}

		//Container class
		case ATTR_CONTAINER_ITEMS: {
			return ATTR_READ_ERROR;
		}

		case ATTR_CUSTOM_ATTRIBUTES: {
			uint64_t size;
			if (!propStream.read<uint64_t>(size)) {
				return ATTR_READ_ERROR;
			}

			for (uint64_t i = 0; i < size; i++) {
				// Unserialize key type and value
				std::string key;
				if (!propStream.readString(key)) {
					return ATTR_READ_ERROR;
				};

				// Unserialize value type and value
				ItemAttributes::CustomAttribute val;
				if (!val.unserialize(propStream)) {
					return ATTR_READ_ERROR;
				}

				setCustomAttribute(key, val);
			}
			break;
		}

		default:
			return ATTR_READ_ERROR;
	}

	return ATTR_READ_CONTINUE;
}

bool Item::unserializeAttr(PropStream& propStream)
{
	uint8_t attr_type;
	while (propStream.read<uint8_t>(attr_type) && attr_type != 0) {
		Attr_ReadValue ret = readAttr(static_cast<AttrTypes_t>(attr_type), propStream);
		if (ret == ATTR_READ_ERROR) {
			return false;
		} else if (ret == ATTR_READ_END) {
			return true;
		}
	}
	return true;
}

bool Item::unserializeItemNode(OTB::Loader&, const OTB::Node&, PropStream& propStream)
{
	return unserializeAttr(propStream);
}

void Item::serializeAttr(PropWriteStream& propWriteStream) const
{
	const ItemType& it = items[id];
	if (it.stackable || it.isFluidContainer() || it.isSplash()) {
		propWriteStream.write<uint8_t>(ATTR_COUNT);
		propWriteStream.write<uint8_t>(getSubType());
	}

	uint16_t charges = getCharges();
	if (charges != 0) {
		propWriteStream.write<uint8_t>(ATTR_CHARGES);
		propWriteStream.write<uint16_t>(charges);
	}

	if (it.moveable) {
		uint16_t actionId = getActionId();
		if (actionId != 0) {
			propWriteStream.write<uint8_t>(ATTR_ACTION_ID);
			propWriteStream.write<uint16_t>(actionId);
		}
	}

	const std::string& text = getText();
	if (!text.empty()) {
		propWriteStream.write<uint8_t>(ATTR_TEXT);
		propWriteStream.writeString(text);
	}

	const time_t writtenDate = getDate();
	if (writtenDate != 0) {
		propWriteStream.write<uint8_t>(ATTR_WRITTENDATE);
		propWriteStream.write<uint32_t>(writtenDate);
	}

	const std::string& writer = getWriter();
	if (!writer.empty()) {
		propWriteStream.write<uint8_t>(ATTR_WRITTENBY);
		propWriteStream.writeString(writer);
	}

	const std::string& specialDesc = getSpecialDescription();
	if (!specialDesc.empty()) {
		propWriteStream.write<uint8_t>(ATTR_DESC);
		propWriteStream.writeString(specialDesc);
	}

	if (hasAttribute(ITEM_ATTRIBUTE_DURATION)) {
		propWriteStream.write<uint8_t>(ATTR_DURATION);
		propWriteStream.write<uint32_t>(getIntAttr(ITEM_ATTRIBUTE_DURATION));
	}

	ItemDecayState_t decayState = getDecaying();
	if (decayState == DECAYING_TRUE || decayState == DECAYING_PENDING) {
		propWriteStream.write<uint8_t>(ATTR_DECAYING_STATE);
		propWriteStream.write<uint8_t>(decayState);
	}

	if (hasAttribute(ITEM_ATTRIBUTE_NAME)) {
		propWriteStream.write<uint8_t>(ATTR_NAME);
		propWriteStream.writeString(getStrAttr(ITEM_ATTRIBUTE_NAME));
	}

	if (hasAttribute(ITEM_ATTRIBUTE_ARTICLE)) {
		propWriteStream.write<uint8_t>(ATTR_ARTICLE);
		propWriteStream.writeString(getStrAttr(ITEM_ATTRIBUTE_ARTICLE));
	}

	if (hasAttribute(ITEM_ATTRIBUTE_PLURALNAME)) {
		propWriteStream.write<uint8_t>(ATTR_PLURALNAME);
		propWriteStream.writeString(getStrAttr(ITEM_ATTRIBUTE_PLURALNAME));
	}

	if (hasAttribute(ITEM_ATTRIBUTE_WEIGHT)) {
		propWriteStream.write<uint8_t>(ATTR_WEIGHT);
		propWriteStream.write<uint32_t>(getIntAttr(ITEM_ATTRIBUTE_WEIGHT));
	}

	if (hasAttribute(ITEM_ATTRIBUTE_ATTACK)) {
		propWriteStream.write<uint8_t>(ATTR_ATTACK);
		propWriteStream.write<int32_t>(getIntAttr(ITEM_ATTRIBUTE_ATTACK));
	}

	if (hasAttribute(ITEM_ATTRIBUTE_ATTACK_SPEED)) {
		propWriteStream.write<uint8_t>(ATTR_ATTACK_SPEED);
		propWriteStream.write<uint32_t>(getIntAttr(ITEM_ATTRIBUTE_ATTACK_SPEED));
	}

	if (hasAttribute(ITEM_ATTRIBUTE_DEFENSE)) {
		propWriteStream.write<uint8_t>(ATTR_DEFENSE);
		propWriteStream.write<int32_t>(getIntAttr(ITEM_ATTRIBUTE_DEFENSE));
	}

	if (hasAttribute(ITEM_ATTRIBUTE_EXTRADEFENSE)) {
		propWriteStream.write<uint8_t>(ATTR_EXTRADEFENSE);
		propWriteStream.write<int32_t>(getIntAttr(ITEM_ATTRIBUTE_EXTRADEFENSE));
	}

	if (hasAttribute(ITEM_ATTRIBUTE_ARMOR)) {
		propWriteStream.write<uint8_t>(ATTR_ARMOR);
		propWriteStream.write<int32_t>(getIntAttr(ITEM_ATTRIBUTE_ARMOR));
	}

	if (hasAttribute(ITEM_ATTRIBUTE_RARITY)) {
		propWriteStream.write<uint8_t>(ATTR_RARITY);
		propWriteStream.write<int32_t>(getIntAttr(ITEM_ATTRIBUTE_RARITY));
	}

	if (hasAttribute(ITEM_ATTRIBUTE_HITCHANCE)) {
		propWriteStream.write<uint8_t>(ATTR_HITCHANCE);
		propWriteStream.write<int8_t>(getIntAttr(ITEM_ATTRIBUTE_HITCHANCE));
	}

	if (hasAttribute(ITEM_ATTRIBUTE_SHOOTRANGE)) {
		propWriteStream.write<uint8_t>(ATTR_SHOOTRANGE);
		propWriteStream.write<uint8_t>(getIntAttr(ITEM_ATTRIBUTE_SHOOTRANGE));
	}

	if (hasAttribute(ITEM_ATTRIBUTE_DECAYTO)) {
		propWriteStream.write<uint8_t>(ATTR_DECAYTO);
		propWriteStream.write<int32_t>(getIntAttr(ITEM_ATTRIBUTE_DECAYTO));
	}

	if (hasAttribute(ITEM_ATTRIBUTE_WRAPID)) {
		propWriteStream.write<uint8_t>(ATTR_WRAPID);
		propWriteStream.write<uint16_t>(getIntAttr(ITEM_ATTRIBUTE_WRAPID));
	}

	if (hasAttribute(ITEM_ATTRIBUTE_STOREITEM)) {
		propWriteStream.write<uint8_t>(ATTR_STOREITEM);
		propWriteStream.write<uint8_t>(getIntAttr(ITEM_ATTRIBUTE_STOREITEM));
	}

	if (hasAttribute(ITEM_ATTRIBUTE_CUSTOM)) {
		const ItemAttributes::CustomAttributeMap* customAttrMap = attributes->getCustomAttributeMap();
		propWriteStream.write<uint8_t>(ATTR_CUSTOM_ATTRIBUTES);
		propWriteStream.write<uint64_t>(static_cast<uint64_t>(customAttrMap->size()));
		for (const auto &entry : *customAttrMap) {
			// Serializing key type and value
			propWriteStream.writeString(entry.first);

			// Serializing value type and value
			entry.second.serialize(propWriteStream);
		}
	}
}

bool Item::hasProperty(ITEMPROPERTY prop) const
{
	const ItemType& it = items[id];
	switch (prop) {
		case CONST_PROP_BLOCKSOLID: return it.blockSolid;
		case CONST_PROP_MOVEABLE: return it.moveable && !hasAttribute(ITEM_ATTRIBUTE_UNIQUEID);
		case CONST_PROP_HASHEIGHT: return it.hasHeight;
		case CONST_PROP_BLOCKPROJECTILE: return it.blockProjectile;
		case CONST_PROP_BLOCKPATH: return it.blockPathFind;
		case CONST_PROP_ISVERTICAL: return it.isVertical;
		case CONST_PROP_ISHORIZONTAL: return it.isHorizontal;
		case CONST_PROP_IMMOVABLEBLOCKSOLID: return it.blockSolid && (!it.moveable || hasAttribute(ITEM_ATTRIBUTE_UNIQUEID));
		case CONST_PROP_IMMOVABLEBLOCKPATH: return it.blockPathFind && (!it.moveable || hasAttribute(ITEM_ATTRIBUTE_UNIQUEID));
		case CONST_PROP_IMMOVABLENOFIELDBLOCKPATH: return !it.isMagicField() && it.blockPathFind && (!it.moveable || hasAttribute(ITEM_ATTRIBUTE_UNIQUEID));
		case CONST_PROP_NOFIELDBLOCKPATH: return !it.isMagicField() && it.blockPathFind;
		case CONST_PROP_SUPPORTHANGABLE: return it.isHorizontal || it.isVertical;
		default: return false;
	}
}

uint32_t Item::getWeight() const
{
	uint32_t weight = getBaseWeight();
	if (isStackable()) {
		return weight * std::max<uint32_t>(1, getItemCount());
	}
	return weight;
}

std::string Item::getDescription(const ItemType& it, int32_t lookDistance,
                                 const Item* item /*= nullptr*/, int32_t subType /*= -1*/, bool addArticle /*= true*/)
{
	const std::string* text = nullptr;

	std::ostringstream s;
	s << getNameDescription(it, item, subType, addArticle);

	if (item) {
		subType = item->getSubType();
	}

	if (it.isRune()) {
		if (it.runeLevel > 0 || it.runeMagLevel > 0) {
			if (RuneSpell* rune = g_spells->getRuneSpell(it.id)) {
				int32_t tmpSubType = subType;
				if (item) {
					tmpSubType = item->getSubType();
				}
				s << " (\"" << it.runeSpellName << "\"). " << (it.stackable && tmpSubType > 1 ? "They" : "It") << " can only be used by ";

				const VocSpellMap& vocMap = rune->getVocMap();
				std::vector<Vocation*> showVocMap;

				// vocations are usually listed with the unpromoted and promoted version, the latter being
				// hidden from description, so `total / 2` is most likely the amount of vocations to be shown.
				showVocMap.reserve(vocMap.size() / 2);
				for (const auto& voc : vocMap) {
					if (voc.second) {
						showVocMap.push_back(g_vocations.getVocation(voc.first));
					}
				}

				if (!showVocMap.empty()) {
					auto vocIt = showVocMap.begin(), vocLast = (showVocMap.end() - 1);
					while (vocIt != vocLast) {
						s << asLowerCaseString((*vocIt)->getVocName()) << "s";
						if (++vocIt == vocLast) {
							s << " and ";
						} else {
							s << ", ";
						}
					}
					s << asLowerCaseString((*vocLast)->getVocName()) << "s";
				} else {
					s << "players";
				}

				s << " with";

				if (it.runeLevel > 0) {
					s << " level " << it.runeLevel;
				}

				if (it.runeMagLevel > 0) {
					if (it.runeLevel > 0) {
						s << " and";
					}

					s << " magic level " << it.runeMagLevel;
				}

				s << " or higher";
			}
		}
	} else if (it.weaponType != WEAPON_NONE) {
		bool begin = true;
		if (it.weaponType == WEAPON_DISTANCE && it.ammoType != AMMO_NONE) {
			s << " (Range:" << static_cast<uint16_t>(item ? item->getShootRange() : it.shootRange);

			int32_t attack;
			int8_t hitChance;
			if (item) {
				attack = item->getAttack();
				hitChance = item->getHitChance();
			} else {
				attack = it.attack;
				hitChance = it.hitChance;
			}

			if (attack != 0) {
				s << ", Atk" << std::showpos << attack << std::noshowpos;
			}

			if (hitChance != 0) {
				s << ", Hit%" << std::showpos << static_cast<int16_t>(hitChance) << std::noshowpos;
			}

			begin = false;
		} else if (it.weaponType != WEAPON_AMMO) {
			int32_t attack, defense, extraDefense;
			if (item) {
				attack = item->getAttack();
				defense = item->getDefense();
				extraDefense = item->getExtraDefense();
			} else {
				attack = it.attack;
				defense = it.defense;
				extraDefense = it.extraDefense;
			}

			if (attack != 0) {
				begin = false;
				s << " (Atk:" << attack;

				if (it.abilities && it.abilities->elementType != COMBAT_NONE && it.abilities->elementDamage != 0) {
					s << " physical + " << it.abilities->elementDamage << ' ' << getCombatName(it.abilities->elementType);
				}
			}

			uint32_t attackSpeed = item ? item->getAttackSpeed() : it.attackSpeed;
			if (attackSpeed) {
				if (begin) {
					begin = false;
					s << " (";
				} else {
					s << ", ";
				}

				s << "Atk Spd:" << (attackSpeed / 1000.) << "s";
			}

			if (defense != 0 || extraDefense != 0) {
				if (begin) {
					begin = false;
					s << " (";
				} else {
					s << ", ";
				}

				s << "Def:" << defense;
				if (extraDefense != 0) {
					s << ' ' << std::showpos << extraDefense << std::noshowpos;
				}
			}
		}

		if (it.abilities) {
			for (uint8_t i = SKILL_FIRST; i <= SKILL_LAST; i++) {
				if (!it.abilities->skills[i]) {
					continue;
				}

				if (begin) {
					begin = false;
					s << " (";
				} else {
					s << ", ";
				}

				s << getSkillName(i) << ' ' << std::showpos << it.abilities->skills[i] << std::noshowpos;
			}

			for (uint8_t i = SPECIALSKILL_FIRST; i <= SPECIALSKILL_LAST; i++) {
				if (!it.abilities->specialSkills[i]) {
					continue;
				}

				if (begin) {
					begin = false;
					s << " (";
				} else {
					s << ", ";
				}

				s << getSpecialSkillName(i) << ' ' << std::showpos << it.abilities->specialSkills[i] << '%' << std::noshowpos;
			}

			if (it.abilities->stats[STAT_MAGICPOINTS]) {
				if (begin) {
					begin = false;
					s << " (";
				} else {
					s << ", ";
				}

				s << "magic level " << std::showpos << it.abilities->stats[STAT_MAGICPOINTS] << std::noshowpos;
			}

			int16_t show = it.abilities->absorbPercent[0];
			if (show != 0) {
				for (size_t i = 1; i < COMBAT_COUNT; ++i) {
					if (it.abilities->absorbPercent[i] != show) {
						show = 0;
						break;
					}
				}
			}

			if (show == 0) {
				bool tmp = true;

				for (size_t i = 0; i < COMBAT_COUNT; ++i) {
					if (it.abilities->absorbPercent[i] == 0) {
						continue;
					}

					if (tmp) {
						tmp = false;

						if (begin) {
							begin = false;
							s << " (";
						} else {
							s << ", ";
						}

						s << "protection ";
					} else {
						s << ", ";
					}

					s << getCombatName(indexToCombatType(i)) << ' ' << std::showpos << it.abilities->absorbPercent[i] << std::noshowpos << '%';
				}
			} else {
				if (begin) {
					begin = false;
					s << " (";
				} else {
					s << ", ";
				}

				s << "protection all " << std::showpos << show << std::noshowpos << '%';
			}

			show = it.abilities->fieldAbsorbPercent[0];
			if (show != 0) {
				for (size_t i = 1; i < COMBAT_COUNT; ++i) {
					if (it.abilities->absorbPercent[i] != show) {
						show = 0;
						break;
					}
				}
			}

			if (show == 0) {
				bool tmp = true;

				for (size_t i = 0; i < COMBAT_COUNT; ++i) {
					if (it.abilities->fieldAbsorbPercent[i] == 0) {
						continue;
					}

					if (tmp) {
						tmp = false;

						if (begin) {
							begin = false;
							s << " (";
						} else {
							s << ", ";
						}

						s << "protection ";
					} else {
						s << ", ";
					}

					s << getCombatName(indexToCombatType(i)) << " field " << std::showpos << it.abilities->fieldAbsorbPercent[i] << std::noshowpos << '%';
				}
			} else {
				if (begin) {
					begin = false;
					s << " (";
				} else {
					s << ", ";
				}

				s << "protection all fields " << std::showpos << show << std::noshowpos << '%';
			}

			if (it.abilities->speed) {
				if (begin) {
					begin = false;
					s << " (";
				} else {
					s << ", ";
				}

				s << "speed " << std::showpos << (it.abilities->speed >> 1) << std::noshowpos;
			}
		}

		if (!begin) {
			s << ')';
		}
	} else if (it.armor != 0 || (item && item->getArmor() != 0) || it.showAttributes) {
		bool begin = true;

		int32_t armor = (item ? item->getArmor() : it.armor);
		if (armor != 0) {
			s << " (Arm:" << armor;
			begin = false;
		}

		if (it.abilities) {
			for (uint8_t i = SKILL_FIRST; i <= SKILL_LAST; i++) {
				if (!it.abilities->skills[i]) {
					continue;
				}

				if (begin) {
					begin = false;
					s << " (";
				} else {
					s << ", ";
				}

				s << getSkillName(i) << ' ' << std::showpos << it.abilities->skills[i] << std::noshowpos;
			}

			if (it.abilities->stats[STAT_MAGICPOINTS]) {
				if (begin) {
					begin = false;
					s << " (";
				} else {
					s << ", ";
				}

				s << "magic level " << std::showpos << it.abilities->stats[STAT_MAGICPOINTS] << std::noshowpos;
			}

			int16_t show = it.abilities->absorbPercent[0];
			if (show != 0) {
				for (size_t i = 1; i < COMBAT_COUNT; ++i) {
					if (it.abilities->absorbPercent[i] != show) {
						show = 0;
						break;
					}
				}
			}

			if (!show) {
				bool protectionBegin = true;
				for (size_t i = 0; i < COMBAT_COUNT; ++i) {
					if (it.abilities->absorbPercent[i] == 0) {
						continue;
					}

					if (protectionBegin) {
						protectionBegin = false;

						if (begin) {
							begin = false;
							s << " (";
						} else {
							s << ", ";
						}

						s << "protection ";
					} else {
						s << ", ";
					}

					s << getCombatName(indexToCombatType(i)) << ' ' << std::showpos << it.abilities->absorbPercent[i] << std::noshowpos << '%';
				}
			} else {
				if (begin) {
					begin = false;
					s << " (";
				} else {
					s << ", ";
				}

				s << "protection all " << std::showpos << show << std::noshowpos << '%';
			}

			show = it.abilities->fieldAbsorbPercent[0];
			if (show != 0) {
				for (size_t i = 1; i < COMBAT_COUNT; ++i) {
					if (it.abilities->absorbPercent[i] != show) {
						show = 0;
						break;
					}
				}
			}

			if (!show) {
				bool tmp = true;

				for (size_t i = 0; i < COMBAT_COUNT; ++i) {
					if (it.abilities->fieldAbsorbPercent[i] == 0) {
						continue;
					}

					if (tmp) {
						tmp = false;

						if (begin) {
							begin = false;
							s << " (";
						} else {
							s << ", ";
						}

						s << "protection ";
					} else {
						s << ", ";
					}

					s << getCombatName(indexToCombatType(i)) << " field " << std::showpos << it.abilities->fieldAbsorbPercent[i] << std::noshowpos << '%';
				}
			} else {
				if (begin) {
					begin = false;
					s << " (";
				} else {
					s << ", ";
				}

				s << "protection all fields " << std::showpos << show << std::noshowpos << '%';
			}

			if (it.abilities->speed) {
				if (begin) {
					begin = false;
					s << " (";
				} else {
					s << ", ";
				}

				s << "speed " << std::showpos << (it.abilities->speed >> 1) << std::noshowpos;
			}
		}

		if (!begin) {
			s << ')';
		}
	} else if (it.isContainer() || (item && item->getContainer())) {
		uint32_t volume = 0;
		if (!item || !item->hasAttribute(ITEM_ATTRIBUTE_UNIQUEID)) {
			if (it.isContainer()) {
				volume = item->getName() == "Quiver" ? 4 : it.maxItems;
			} else {
				volume = item->getContainer()->capacity();
			}
		}

		if (volume != 0) {
			s << " (Vol:" << volume << ')';
		}
	} else {
		bool found = true;

		if (it.abilities) {
			if (it.abilities->speed > 0) {
				s << " (speed " << std::showpos << (it.abilities->speed / 2) << std::noshowpos << ')';
			} else if (hasBitSet(CONDITION_DRUNK, it.abilities->conditionSuppressions)) {
				s << " (hard drinking)";
			} else if (it.abilities->invisible) {
				s << " (invisibility)";
			} else if (it.abilities->regeneration) {
				s << " (faster regeneration)";
			} else if (it.abilities->manaShield) {
				s << " (mana shield)";
			} else {
				found = false;
			}
		} else {
			found = false;
		}

		if (!found) {
			if (it.isKey()) {
				int32_t keyNumber = (item ? item->getActionId() : 0);
				if (keyNumber != 0) {
					s << " (Key:" << std::setfill('0') << std::setw(4) << keyNumber << ')';
				}
			} else if (it.isFluidContainer()) {
				if (subType > 0) {
					const std::string& itemName = items[subType].name;
					s << " of " << (!itemName.empty() ? itemName : "unknown");
				} else {
					s << ". It is empty";
				}
			} else if (it.isSplash()) {
				s << " of ";

				if (subType > 0 && !items[subType].name.empty()) {
					s << items[subType].name;
				} else {
					s << "unknown";
				}
			} else if (it.allowDistRead && (it.id < 7369 || it.id > 7371)) {
				s << ".\n";

				if (lookDistance <= 4) {
					if (item) {
						text = &item->getText();
						if (!text->empty()) {
							const std::string& writer = item->getWriter();
							if (!writer.empty()) {
								s << writer << " wrote";
								time_t date = item->getDate();
								if (date != 0) {
									s << " on " << formatDateShort(date);
								}
								s << ": ";
							} else {
								s << "You read: ";
							}
							s << *text;
						} else {
							s << "Nothing is written on it";
						}
					} else {
						s << "Nothing is written on it";
					}
				} else {
					s << "You are too far away to read it";
				}
			} else if (it.levelDoor != 0 && item) {
				uint16_t actionId = item->getActionId();
				if (actionId >= it.levelDoor) {
					s << " for level " << (actionId - it.levelDoor);
				}
			}
		}
	}

	if (it.showCharges) {
		s << " that has " << subType << " charge" << (subType != 1 ? "s" : "") << " left";
	}

	if (it.showDuration) {
		if (item && item->hasAttribute(ITEM_ATTRIBUTE_DURATION)) {
			uint32_t duration = item->getDuration() / 1000;
			s << " that will expire in ";

			if (duration >= 86400) {
				uint16_t days = duration / 86400;
				uint16_t hours = (duration % 86400) / 3600;
				s << days << " day" << (days != 1 ? "s" : "");

				if (hours > 0) {
					s << " and " << hours << " hour" << (hours != 1 ? "s" : "");
				}
			} else if (duration >= 3600) {
				uint16_t hours = duration / 3600;
				uint16_t minutes = (duration % 3600) / 60;
				s << hours << " hour" << (hours != 1 ? "s" : "");

				if (minutes > 0) {
					s << " and " << minutes << " minute" << (minutes != 1 ? "s" : "");
				}
			} else if (duration >= 60) {
				uint16_t minutes = duration / 60;
				s << minutes << " minute" << (minutes != 1 ? "s" : "");
				uint16_t seconds = duration % 60;

				if (seconds > 0) {
					s << " and " << seconds << " second" << (seconds != 1 ? "s" : "");
				}
			} else {
				s << duration << " second" << (duration != 1 ? "s" : "");
			}
		} else {
			s << " that is brand-new";
		}
	}

	if (!it.allowDistRead || (it.id >= 7369 && it.id <= 7371)) {
		s << '.';
	} else {
		if (!text && item) {
			text = &item->getText();
		}

		if (!text || text->empty()) {
			s << '.';
		}
	}

	if (it.wieldInfo != 0) {
		s << "\nIt can only be wielded properly by ";

		if (it.wieldInfo & WIELDINFO_PREMIUM) {
			s << "premium ";
		}

		if (!it.vocationString.empty()) {
			s << it.vocationString;
		} else {
			s << "players";
		}

		if (it.wieldInfo & WIELDINFO_LEVEL) {
			s << " of level " << it.minReqLevel << " or higher";
		}

		if (it.wieldInfo & WIELDINFO_MAGLV) {
			if (it.wieldInfo & WIELDINFO_LEVEL) {
				s << " and";
			} else {
				s << " of";
			}

			s << " magic level " << it.minReqMagicLevel << " or higher";
		}

		s << '.';
	}

	if (lookDistance <= 1) {
		if (item) {
			const uint32_t weight = item->getWeight();
			if (weight != 0 && it.pickupable) {
				s << '\n' << getWeightDescription(it, weight, item->getItemCount());
			}
		} else if (it.weight != 0 && it.pickupable) {
			s << '\n' << getWeightDescription(it, it.weight);
		}
	}

	if (item) {
		const std::string& specialDescription = item->getSpecialDescription();
		if (!specialDescription.empty()) {
			s << '\n' << specialDescription;
		} else if (lookDistance <= 1 && !it.description.empty()) {
			s << '\n' << it.description;
		}
	} else if (lookDistance <= 1 && !it.description.empty()) {
		s << '\n' << it.description;
	}

	if (it.allowDistRead && it.id >= 7369 && it.id <= 7371) {
		if (!text && item) {
			text = &item->getText();
		}

		if (text && !text->empty()) {
			s << '\n' << *text;
		}
	}
	return s.str();
}

std::string Item::getDescription(int32_t lookDistance) const
{
	const ItemType& it = items[id];
	return getDescription(it, lookDistance, this);
}

std::string Item::getNameDescription(const ItemType& it, const Item* item /*= nullptr*/, int32_t subType /*= -1*/, bool addArticle /*= true*/)
{
	if (item) {
		subType = item->getSubType();
	}

	std::ostringstream s;

	const std::string& name = (item ? item->getName() : it.name);
	if (!name.empty()) {
		if (it.stackable && subType > 1) {
			if (it.showCount) {
				s << subType << ' ';
			}

			s << (item ? item->getPluralName() : it.getPluralName());
		} else {
			if (addArticle) {
				const std::string& article = (item ? item->getArticle() : it.article);
				if (!article.empty()) {
					s << article << ' ';
				}
			}

			s << name;
		}
	} else {
		if (addArticle) {
			s << "an ";
		}
		s << "item of type " << it.id;
	}
	return s.str();
}

std::string Item::getNameDescription() const
{
	const ItemType& it = items[id];
	return getNameDescription(it, this);
}

std::string Item::getWeightDescription(const ItemType& it, uint32_t weight, uint32_t count /*= 1*/)
{
	std::ostringstream ss;
	if (it.stackable && count > 1 && it.showCount != 0) {
		ss << "They weigh ";
	} else {
		ss << "It weighs ";
	}

	if (weight < 10) {
		ss << "0.0" << weight;
	} else if (weight < 100) {
		ss << "0." << weight;
	} else {
		std::string weightString = std::to_string(weight);
		weightString.insert(weightString.end() - 2, '.');
		ss << weightString;
	}

	ss << " oz.";
	return ss.str();
}

std::string Item::getWeightDescription(uint32_t weight) const
{
	const ItemType& it = Item::items[id];
	return getWeightDescription(it, weight, getItemCount());
}

std::string Item::getWeightDescription() const
{
	uint32_t weight = getWeight();
	if (weight == 0) {
		return std::string();
	}
	return getWeightDescription(weight);
}

void Item::setUniqueId(uint16_t n)
{
	if (hasAttribute(ITEM_ATTRIBUTE_UNIQUEID)) {
		return;
	}

	if (g_game.addUniqueItem(n, this)) {
		getAttributes()->setUniqueId(n);
	}
}

bool Item::canDecay() const
{
	if (isRemoved()) {
		return false;
	}

	const ItemType& it = Item::items[id];
	if (getDecayTo() < 0 || it.decayTime == 0) {
		return false;
	}

	if (hasAttribute(ITEM_ATTRIBUTE_UNIQUEID)) {
		return false;
	}

	return true;
}

uint32_t Item::getWorth() const
{
	switch (id) {
		case ITEM_GOLD_COIN:
			return count;

		case ITEM_PLATINUM_COIN:
			return count * 100;

		case ITEM_CRYSTAL_COIN:
			return count * 10000;
		
		case ITEM_AMBER_COIN:
			return count * 1000000;

		case ITEM_DEMONIAC_COIN:
			return count * 100000000;

		default:
			return 0;
	}
}

LightInfo Item::getLightInfo() const
{
	const ItemType& it = items[id];
	return {it.lightLevel, it.lightColor};
}

std::string ItemAttributes::emptyString;
int64_t ItemAttributes::emptyInt;
double ItemAttributes::emptyDouble;
bool ItemAttributes::emptyBool;

const std::string& ItemAttributes::getStrAttr(itemAttrTypes type) const
{
	if (!isStrAttrType(type)) {
		return emptyString;
	}

	const Attribute* attr = getExistingAttr(type);
	if (!attr) {
		return emptyString;
	}
	return *attr->value.string;
}

void ItemAttributes::setStrAttr(itemAttrTypes type, const std::string& value)
{
	if (!isStrAttrType(type)) {
		return;
	}

	if (value.empty()) {
		return;
	}

	Attribute& attr = getAttr(type);
	delete attr.value.string;
	attr.value.string = new std::string(value);
}

void ItemAttributes::removeAttribute(itemAttrTypes type)
{
	if (!hasAttribute(type)) {
		return;
	}

	auto prev_it = attributes.rbegin();
	if ((*prev_it).type == type) {
		attributes.pop_back();
	} else {
		auto it = prev_it, end = attributes.rend();
		while (++it != end) {
			if ((*it).type == type) {
				(*it) = attributes.back();
				attributes.pop_back();
				break;
			}
		}
	}
	attributeBits &= ~type;
}

int64_t ItemAttributes::getIntAttr(itemAttrTypes type) const
{
	if (!isIntAttrType(type)) {
		return 0;
	}

	const Attribute* attr = getExistingAttr(type);
	if (!attr) {
		return 0;
	}
	return attr->value.integer;
}

void ItemAttributes::setIntAttr(itemAttrTypes type, int64_t value)
{
	if (!isIntAttrType(type)) {
		return;
	}

	if (type == ITEM_ATTRIBUTE_ATTACK_SPEED && value < 100) {
		value = 100;
	}

	getAttr(type).value.integer = value;
}

void ItemAttributes::increaseIntAttr(itemAttrTypes type, int64_t value)
{
	setIntAttr(type, getIntAttr(type) + value);
}

const ItemAttributes::Attribute* ItemAttributes::getExistingAttr(itemAttrTypes type) const
{
	if (hasAttribute(type)) {
		for (const Attribute& attribute : attributes) {
			if (attribute.type == type) {
				return &attribute;
			}
		}
	}
	return nullptr;
}

ItemAttributes::Attribute& ItemAttributes::getAttr(itemAttrTypes type)
{
	if (hasAttribute(type)) {
		for (Attribute& attribute : attributes) {
			if (attribute.type == type) {
				return attribute;
			}
		}
	}

	attributeBits |= type;
	attributes.emplace_back(type);
	return attributes.back();
}

void Item::startDecaying()
{
	g_game.startDecay(this);
}

bool Item::hasMarketAttributes() const
{
	if (attributes == nullptr) {
		return true;
	}

	for (const auto& attr : attributes->getList()) {
		if (attr.type == ITEM_ATTRIBUTE_CHARGES) {
			uint16_t charges = static_cast<uint16_t>(attr.value.integer);
			if (charges != items[id].charges) {
				return false;
			}
		} else if (attr.type == ITEM_ATTRIBUTE_DURATION) {
			uint32_t duration = static_cast<uint32_t>(attr.value.integer);
			if (duration != getDefaultDuration()) {
				return false;
			}
		} else {
			return false;
		}
	}
	return true;
}

void Item::getTooltipStats(const ItemType& it, TooltipDataContainer& tooltipData)
{
	if (!it.abilities) {
		return;
	}

	for (int32_t statId = STAT_FIRST; statId <= STAT_LAST; ++statId) {
		int32_t stats = it.abilities->stats[statId];
		if (stats != 0) {
			tooltipData.push_back(TooltipData(TOOLTIP_ATTRIBUTE_STATS, stats, statId));
		}
	}
}

void Item::getTooltipSkills(const ItemType& it, TooltipDataContainer& tooltipData)
{
	if (!it.abilities) {
		return;
	}

	for (int32_t skillId = SKILL_FIRST; skillId <= SKILL_LAST; ++skillId) {
		int32_t skills = it.abilities->skills[skillId];
		if (skills != 0) {
			tooltipData.push_back(TooltipData(TOOLTIP_ATTRIBUTE_SKILL, skills, skillId));
		}
	}
}

void Item::getTooltipCombats(const ItemType& it, CombatType_t combatType, TooltipDataContainer& tooltipData)
{
	if (!it.abilities) {
		return;
	}

	const int32_t combatId = combatTypeToIndex(combatType);
	int32_t absorbPercent = it.abilities->absorbPercent.at(combatId);
	if (absorbPercent != 0) {
		tooltipData.push_back(TooltipData(TOOLTIP_ATTRIBUTE_RESISTANCES, absorbPercent, combatId));
	}
	int32_t fieldAbsorbPercent = it.abilities->fieldAbsorbPercent.at(combatId);
	if (fieldAbsorbPercent != 0) {
		tooltipData.push_back(TooltipData(TOOLTIP_ATTRIBUTE_FIELD_ABSORB, fieldAbsorbPercent, combatId));
	}
}

void Item::getTooltipOther(const ItemType& it, TooltipDataContainer& tooltipData)
{
	if (!it.abilities) {
		return;
	}

	if (it.attackSpeed != 0) {
		tooltipData.push_back(TooltipData(TOOLTIP_ATTRIBUTE_ATTACK_SPEED, it.attackSpeed));
	}
	if (it.abilities->speed != 0) {
		tooltipData.push_back(TooltipData(TOOLTIP_ATTRIBUTE_SPEED, it.abilities->speed));
	}
	if (it.abilities->specialSkills[SPECIALSKILL_MANALEECHAMOUNT] != 0) {
		tooltipData.push_back(TooltipData(TOOLTIP_ATTRIBUTE_MANA_LEECH_AMOUNT, it.abilities->specialSkills[SPECIALSKILL_MANALEECHAMOUNT]));
	}
	if (it.abilities->specialSkills[SPECIALSKILL_MANALEECHCHANCE] != 0) {
		tooltipData.push_back(TooltipData(TOOLTIP_ATTRIBUTE_MANA_LEECH_CHANCE, it.abilities->specialSkills[SPECIALSKILL_MANALEECHCHANCE]));
	}
	if (it.abilities->specialSkills[SPECIALSKILL_LIFELEECHAMOUNT] != 0) {
		tooltipData.push_back(TooltipData(TOOLTIP_ATTRIBUTE_LIFE_LEECH_AMOUNT, it.abilities->specialSkills[SPECIALSKILL_LIFELEECHAMOUNT]));
	}
	if (it.abilities->specialSkills[SPECIALSKILL_LIFELEECHCHANCE] != 0) {
		tooltipData.push_back(TooltipData(TOOLTIP_ATTRIBUTE_LIFE_LEECH_CHANCE, it.abilities->specialSkills[SPECIALSKILL_LIFELEECHCHANCE]));
	}
	if (it.abilities->specialSkills[SPECIALSKILL_CRITICALHITAMOUNT] != 0) {
		tooltipData.push_back(TooltipData(TOOLTIP_ATTRIBUTE_CRITICALHIT_AMOUNT, it.abilities->specialSkills[SPECIALSKILL_CRITICALHITAMOUNT]));
	}
	if (it.abilities->specialSkills[SPECIALSKILL_CRITICALHITCHANCE] != 0) {
		tooltipData.push_back(TooltipData(TOOLTIP_ATTRIBUTE_CRITICALHIT_CHANCE, it.abilities->specialSkills[SPECIALSKILL_CRITICALHITCHANCE]));
	}
	if (it.abilities->elementType == COMBAT_FIREDAMAGE && it.abilities->elementDamage != 0) {
	tooltipData.push_back(TooltipData(TOOLTIP_ATTRIBUTE_FIRE_ATTACK, it.abilities->elementDamage));
	}
	if (it.abilities->elementType == COMBAT_ENERGYDAMAGE && it.abilities->elementDamage != 0) {
	tooltipData.push_back(TooltipData(TOOLTIP_ATTRIBUTE_ENERGY_ATTACK, it.abilities->elementDamage));
	}
	if (it.abilities->elementType == COMBAT_ICEDAMAGE && it.abilities->elementDamage != 0) {
	tooltipData.push_back(TooltipData(TOOLTIP_ATTRIBUTE_ICE_ATTACK, it.abilities->elementDamage));
	}
	if (it.abilities->elementType == COMBAT_DEATHDAMAGE && it.abilities->elementDamage != 0) {
	tooltipData.push_back(TooltipData(TOOLTIP_ATTRIBUTE_DEATH_ATTACK, it.abilities->elementDamage));
	}
	if (it.abilities->elementType == COMBAT_EARTHDAMAGE && it.abilities->elementDamage != 0) {
	tooltipData.push_back(TooltipData(TOOLTIP_ATTRIBUTE_EARTH_ATTACK, it.abilities->elementDamage));
	}
	if (it.abilities->elementType == COMBAT_HOLYDAMAGE && it.abilities->elementDamage != 0) {
	tooltipData.push_back(TooltipData(TOOLTIP_ATTRIBUTE_HOLY_ATTACK, it.abilities->elementDamage));
	}
	if (it.abilities->elementType == COMBAT_WATERDAMAGE && it.abilities->elementDamage != 0) {
	tooltipData.push_back(TooltipData(TOOLTIP_ATTRIBUTE_WATER_ATTACK, it.abilities->elementDamage));
	}
	if (it.abilities->elementType == COMBAT_ARCANEDAMAGE && it.abilities->elementDamage != 0) {
	tooltipData.push_back(TooltipData(TOOLTIP_ATTRIBUTE_ARCANE_ATTACK, it.abilities->elementDamage));
	}
}

void Item::getTooltipData(Item* item, uint16_t spriteId, uint16_t count, TooltipDataContainer& tooltipData)
{
	const ItemType& it = item ? items[item->getID()] : Item::items.getItemIdByClientId(spriteId);
	tooltipData.push_back(TooltipData(TOOLTIP_ATTRIBUTE_NAME, (item ? item->getName() : it.name)));
	tooltipData.push_back(TooltipData(TOOLTIP_ATTRIBUTE_COUNT, (item ? item->getSubType() : count)));
	if (it.isRune()) {
		tooltipData.push_back(TooltipData(TOOLTIP_ATTRIBUTE_CHARGES, std::max<uint32_t>(1, (item ? item->getSubType() : it.charges))));

		if (!it.runeSpellName.empty()) {
			tooltipData.push_back(TooltipData(TOOLTIP_ATTRIBUTE_RUNE_NAME, it.runeSpellName));
		}
		if (it.runeLevel > 0) {
			tooltipData.push_back(TooltipData(TOOLTIP_ATTRIBUTE_RUNE_LEVEL, it.runeLevel));
		}
		if (it.runeMagLevel > 0) {
			tooltipData.push_back(TooltipData(TOOLTIP_ATTRIBUTE_RUNE_LEVEL, it.runeMagLevel));
		}
	}
	else if (it.weaponType != WEAPON_NONE) {
    if (it.weaponType == WEAPON_DISTANCE && it.ammoType != AMMO_NONE) {
       if (item && item->hasAttribute(ITEM_ATTRIBUTE_ATTACK)) {
        int32_t attack = item->getAttack();
        if (attack > 0) {
            tooltipData.push_back(TooltipData(TOOLTIP_ATTRIBUTE_ATTACK, attack));
        }
    	} else if (it.attack > 0) {
        	tooltipData.push_back(TooltipData(TOOLTIP_ATTRIBUTE_ATTACK, it.attack));
    	}

        if (it.shootRange != 0) {
            tooltipData.push_back(TooltipData(TOOLTIP_ATTRIBUTE_SHOOTRANGE, it.shootRange));
        }
        if (item && item->hasAttribute(ITEM_ATTRIBUTE_HITCHANCE)) {
        int32_t hitChance = item->getHitChance();
        if (hitChance > 0) {
            tooltipData.push_back(TooltipData(TOOLTIP_ATTRIBUTE_HITCHANCE, hitChance));
        }
    } else if (it.hitChance > 0) {
        tooltipData.push_back(TooltipData(TOOLTIP_ATTRIBUTE_HITCHANCE, it.hitChance));
    }
    } else if (it.weaponType != WEAPON_AMMO && it.weaponType != WEAPON_WAND && 
               (it.attack != 0 || it.defense != 0 || it.extraDefense != 0)) {
        if (item && item->hasAttribute(ITEM_ATTRIBUTE_ATTACK)) {
        int32_t attack = item->getAttack();
        if (attack > 0) {
            tooltipData.push_back(TooltipData(TOOLTIP_ATTRIBUTE_ATTACK, attack));
        }
    } else if (it.attack > 0) {
        tooltipData.push_back(TooltipData(TOOLTIP_ATTRIBUTE_ATTACK, it.attack));
    }

        if (item && item->hasAttribute(ITEM_ATTRIBUTE_DEFENSE)) {
        int32_t defense = item->getDefense();
        if (defense > 0) {
            tooltipData.push_back(TooltipData(TOOLTIP_ATTRIBUTE_DEFENSE, defense));
        }
    } else if (it.defense > 0) {
        tooltipData.push_back(TooltipData(TOOLTIP_ATTRIBUTE_DEFENSE, it.defense));
    }

        if (item && item->hasAttribute(ITEM_ATTRIBUTE_EXTRADEFENSE)) {
        int32_t extraDefense = item->getExtraDefense();
        if (extraDefense > 0) {
            tooltipData.push_back(TooltipData(TOOLTIP_ATTRIBUTE_EXTRADEFENSE, extraDefense));
        }
    } else if (it.extraDefense > 0) {
        tooltipData.push_back(TooltipData(TOOLTIP_ATTRIBUTE_EXTRADEFENSE, it.extraDefense));
    }
	}
	} 
	else if (item && item->hasAttribute(ITEM_ATTRIBUTE_ARMOR)) {
    int32_t armor = item->getArmor();
    if (armor > 0) {
        tooltipData.push_back(TooltipData(TOOLTIP_ATTRIBUTE_ARMOR, armor));
    }
	} else if (it.armor > 0) {
    	tooltipData.push_back(TooltipData(TOOLTIP_ATTRIBUTE_ARMOR, it.armor));
	}

	else if (it.isFluidContainer()) {
		if (item && item->getFluidType() != 0) {
			tooltipData.push_back(TooltipData(TOOLTIP_ATTRIBUTE_FLUIDTYPE, items[item->getFluidType()].name));
		}
		else {
			tooltipData.push_back(TooltipData(TOOLTIP_ATTRIBUTE_FLUIDTYPE, "It is empty."));
		}
	}
	else if (it.isContainer()) {
		tooltipData.push_back(TooltipData(TOOLTIP_ATTRIBUTE_CONTAINER_SIZE, it.maxItems));
	}
	else if (it.isKey() && item) {
		tooltipData.push_back(TooltipData(TOOLTIP_ATTRIBUTE_KEY, item->getActionId()));
	}
	else if (it.charges > 0) {
		tooltipData.push_back(TooltipData(TOOLTIP_ATTRIBUTE_CHARGES, std::max<uint32_t>(1, (item ? item->getSubType() : it.charges))));
	}
	else if (it.showDuration) {
		std::ostringstream s;
		if (item && item->hasAttribute(ITEM_ATTRIBUTE_DURATION)) {
			int32_t duration = item->getDuration() / 1000;
			s << "That has energy for ";

			if (duration >= 120) {
				s << duration / 60 << " minutes left";
			}
			else if (duration > 60) {
				s << "1 minute left";
			}
			else {
				s << "less than a minute left";
			}
		}
		else {
			s << "That is brand-new.";
		}

		tooltipData.push_back(TooltipData(TOOLTIP_ATTRIBUTE_DURATION, s.str()));
	}

	if (it.wieldInfo != 0) {
		std::ostringstream s;
		s << std::endl << "It can only be wielded properly by ";

		if (it.wieldInfo & WIELDINFO_PREMIUM) {
			s << "premium ";
		}

		if (it.wieldInfo & WIELDINFO_VOCREQ) {
			s << it.vocationString;
		}
		else {
			s << "players";
		}

		if (it.wieldInfo & WIELDINFO_LEVEL) {
			s << " of level " << static_cast<int>(it.minReqLevel) << " or higher";
		}

		if (it.wieldInfo & WIELDINFO_MAGLV) {
			if (it.wieldInfo & WIELDINFO_LEVEL) {
				s << " and";
			}
			else {
				s << " of";
			}

			s << " magic level " << static_cast<int>(it.minReqMagicLevel) << " or higher";
		}

		tooltipData.push_back(TooltipData(TOOLTIP_ATTRIBUTE_WIELDINFO, s.str()));
	}

	if (item && item->getSpecialDescription() != "") {
		tooltipData.push_back(TooltipData(TOOLTIP_ATTRIBUTE_TEXT, item->getSpecialDescription()));
	}
	else if (it.description.length() > 0) {
		tooltipData.push_back(TooltipData(TOOLTIP_ATTRIBUTE_TEXT, it.description));
	}

	if (it.pickupable) {
		double weight = (item ? item->getWeight() : it.weight);
		if (weight > 0) {
			tooltipData.push_back(TooltipData(TOOLTIP_ATTRIBUTE_WEIGHT, getWeightDescription(it, weight)));
		}
	}

	if (item) {
		item->getRarityLevel(tooltipData);
	}

	getTooltipCombats(it, COMBAT_PHYSICALDAMAGE, tooltipData);
	getTooltipCombats(it, COMBAT_ENERGYDAMAGE, tooltipData);
	getTooltipCombats(it, COMBAT_EARTHDAMAGE, tooltipData);
	getTooltipCombats(it, COMBAT_FIREDAMAGE, tooltipData);
	getTooltipCombats(it, COMBAT_LIFEDRAIN, tooltipData);
	getTooltipCombats(it, COMBAT_MANADRAIN, tooltipData);
	getTooltipCombats(it, COMBAT_HEALING, tooltipData);
	getTooltipCombats(it, COMBAT_ICEDAMAGE, tooltipData);
	getTooltipCombats(it, COMBAT_HOLYDAMAGE, tooltipData);
	getTooltipCombats(it, COMBAT_DEATHDAMAGE, tooltipData);
	getTooltipCombats(it, COMBAT_WATERDAMAGE, tooltipData);
	getTooltipCombats(it, COMBAT_ARCANEDAMAGE, tooltipData);
	getTooltipStats(it, tooltipData);
	getTooltipSkills(it, tooltipData);
	getTooltipOther(it, tooltipData);
}

void Item::getRarityLevel(TooltipDataContainer& tooltipData)
{
    int rarity = 0;
    const auto rarityId = getCustomAttribute("rarity");
    if (rarityId) {
        const auto& value = rarityId->value;
        if (value.type() == typeid(int64_t)) {
            rarity = boost::get<int64_t>(value);
        }
    }
    if (rarity == 0) {
        rarity = items[getID()].rarity;
    }
    if (rarity > 0) {
        tooltipData.push_back(TooltipData(TOOLTIP_ATTRIBUTE_RARITY, rarity));
    }
}

template<>
const std::string& ItemAttributes::CustomAttribute::get<std::string>() {
	if (value.type() == typeid(std::string)) {
		return boost::get<std::string>(value);
	}

	return emptyString;
}

template<>
const int64_t& ItemAttributes::CustomAttribute::get<int64_t>() {
	if (value.type() == typeid(int64_t)) {
		return boost::get<int64_t>(value);
	}

	return emptyInt;
}

template<>
const double& ItemAttributes::CustomAttribute::get<double>() {
	if (value.type() == typeid(double)) {
		return boost::get<double>(value);
	}

	return emptyDouble;
}

template<>
const bool& ItemAttributes::CustomAttribute::get<bool>() {
	if (value.type() == typeid(bool)) {
		return boost::get<bool>(value);
	}

	return emptyBool;
}
