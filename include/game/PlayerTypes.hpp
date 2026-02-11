enum class PlayerClass {
    WARRIOR = 0,
    MAGE = 1,
    RANGER = 2,
    ROGUE = 3,
    CLERIC = 4,
    PALADIN = 5,
    DRUID = 6,
    MONK = 7,
    BARD = 8,
    NECROMANCER = 9,
    ANY = 10
};

enum class PlayerRace {
    HUMAN = 0,
    ELF = 1,
    DWARF = 2,
    ORC = 3,
    GNOME = 4,
    HALFLING = 5,
    DRAGONBORN = 6,
    TIEFLING = 7,
    HALF_ELF = 8,
    HALF_ORC = 9
};

enum class PlayerStatus {
    IDLE = 0,
    MOVING = 1,
    COMBAT = 2,
    CASTING = 3,
    DEAD = 4,
    STUNNED = 5,
    SLEEPING = 6,
    IN_MENU = 7,
    TRADING = 8,
    CRAFTING = 9,
    MOUNTED = 10
};
