#include "room_data.h"
#include "creature.h"
#include "utils.h"
#include "quest.h"
#include "bomb.h"
#include "handler.h"
#include "player_table.h"
#include "spells.h"
#include "char_class.h"

bool
is_arena_combat(struct Creature *ch, struct Creature *vict)
{
	if (!vict->in_room)
		return false;

	if (ROOM_FLAGGED(vict->in_room, ROOM_ARENA) ||
			GET_ZONE(vict->in_room) == 400)
		return true;
    
    //mobs don't quest
    if (!IS_NPC(vict)) {
        if (GET_QUEST(vict)) {
            Quest *quest;
            
            quest = quest_by_vnum(GET_QUEST(vict));
            if (QUEST_FLAGGED(quest, QUEST_ARENA))
                return true;
        }        
    }
	
	if (!ch || !ch->in_room)
		return false;

    if (ROOM_FLAGGED(ch->in_room, ROOM_ARENA) ||
			GET_ZONE(ch->in_room) == 400)
		return true;

    //mobs don't quest
    if (!IS_NPC(ch)) {
        if (GET_QUEST(ch)) {
            Quest *quest;
            
            quest = quest_by_vnum(GET_QUEST(ch));
            if (QUEST_FLAGGED(quest, QUEST_ARENA))
                return true;
        }
    }
    
    return false;
}

bool 
is_npk_combat(struct Creature *ch, struct Creature *vict) {
    if (!ch || !vict) {
        return false;
    }

    if (IS_NPC(ch) || IS_NPC(vict))
        return false;

    if (vict->in_room->zone->getPKStyle() == ZONE_NEUTRAL_PK) {
        return true;
    }

    return false;
}

// Called to select a safe target when the character doesn't get to select
// the target
bool
ok_to_damage(struct Creature *ch, struct Creature *vict)
{
	// Boom.  no killers.
	return true;
}

void
count_pkill(Creature *killer, Creature *victim)
{
	bool award_bounty(Creature *, Creature *);
	Creature *perp;
	int gain;

	perp = killer;
	while (IS_AFFECTED(perp, AFF_CHARM) && perp->master &&
			perp->in_room == perp->master->in_room)
		perp = perp->master;

	GET_PKILLS(perp)++;

	if (PRF2_FLAGGED(perp, PRF2_PKILLER) &&
			!award_bounty(perp, victim) &&
            (killer->initiatedCombat(victim) || !killer->findCombat(victim))) {

		// Basic level/gen adjustment
        if (perp != victim) {
			// Start with 10 for causing hassle
			gain = 10;

			// adjust for level/gen difference
			gain += ((GET_LEVEL(perp) + GET_REMORT_GEN(perp) * 50)
				- (GET_LEVEL(victim) + GET_REMORT_GEN(victim) * 50)) / 5;

			// Additional adjustment for killing an innocent
			if (GET_REPUTATION(victim) == 0)
				gain *= 2;

            // Additional adjustment for killing a lower gen
            if (GET_REMORT_GEN(perp) > GET_REMORT_GEN(victim))
				gain += (GET_REMORT_GEN(perp) - GET_REMORT_GEN(victim)) * 9;

			if (IS_CRIMINAL(victim))
				gain /= 4;

			gain = MAX(1, gain);

			mudlog(LVL_IMMORT, CMP, true,
				"%s gained %d reputation for pkilling %s", GET_NAME(perp),
				gain, GET_NAME(victim));
			perp->gain_reputation(gain);
        }
	}
}

void
check_killer(struct Creature *ch, struct Creature *vict,
	const char *debug_msg)
{
	return;
}

void
check_thief(struct Creature *ch, struct Creature *vict,
	const char *debug_msg)
{
	Creature *perp;
	int gain = 0;

	// First we need to find the perp
	perp = ch;
	while (IS_AFFECTED(perp, AFF_CHARM) && perp->master &&
			perp->in_room == perp->master->in_room)
		perp = perp->master;
	
	if (perp == vict)
		return;

	// We don't care about NPCs
	if (IS_NPC(perp) || IS_NPC(vict))
		return;

	// Criminals have NO protection vs psteal
	if (IS_CRIMINAL(vict))
		return;
	
	// adjust for level/gen difference
	gain = ((GET_LEVEL(perp) + GET_REMORT_GEN(perp) * 50)
		- (GET_LEVEL(vict) + GET_REMORT_GEN(vict) * 50)) / 5;

	// Additional adjustment for killing an innocent
	if (GET_REPUTATION(vict) == 0)
		gain *= 2;

	// Additional adjustment for killing a lower gen
	if (GET_REMORT_GEN(perp) > GET_REMORT_GEN(vict))
		gain += (GET_REMORT_GEN(perp) - GET_REMORT_GEN(vict)) * 9;
	
	gain /= 10;

	gain = MAX(1, gain);


    if (!is_arena_combat(ch, vict)) {
    	mudlog(LVL_IMMORT, CMP, true,
               "%s gained %d reputation for pstealing from %s",
		       GET_NAME(perp), gain, GET_NAME(vict));
	    perp->gain_reputation(gain);
    }
    else {
    	mudlog(LVL_POWER, CMP, true,
               "%s pstealing from %s in [ARENA]",
		       GET_NAME(perp), GET_NAME(vict));
    }
}

void
check_object_killer(struct obj_data *obj, struct Creature *vict)
{
	Creature cbuf(true);
	struct Creature *killer = NULL;
	int obj_id;

    if (ROOM_FLAGGED(vict->in_room, ROOM_PEACEFUL)) {
        return;
    }
	if (IS_NPC(vict))
		return;

	slog("Checking object killer %s -> %s. ", obj->name,
		GET_NAME(vict));

	if (IS_BOMB(obj))
		obj_id = BOMB_IDNUM(obj);
	else if (GET_OBJ_SIGIL_IDNUM(obj))
		obj_id = GET_OBJ_SIGIL_IDNUM(obj);
	else {
		errlog("unknown damager in check_object_killer.");
		return;
	}

	if (!obj_id)
		return;

	killer = get_char_in_world_by_idnum(obj_id);

	// load the bastich from file.
	if (!killer) {
		cbuf.clear();
		if (cbuf.loadFromXML(obj_id)) {
			killer = &cbuf;
			cbuf.account = Account::retrieve(playerIndex.getAccountID(obj_id));
		}
	}

	// the piece o shit has a bogus killer idnum on it!
	if (!killer) {
		errlog("bogus idnum %d on object %s damaging %s.",
			obj_id, obj->name, GET_NAME(vict));
		return;
	}

	count_pkill(killer, vict);

	// save the sonuvabitch to file
	killer->saveToXML();
}

void
punish_killer_death(Creature *ch)
{
    int loss = MAX(1, GET_SEVERITY(ch) / 2); // lose 1 level per 2, min 1
    int old_hit, old_mana, old_move;
    int lvl;

    //
    // Unaffect the character before all the stuff is subtracted. Bug was being abused
    //
    struct affected_type *af = ch->affected;
    while (af) {
        if (af->clearAtDeath()) {
            affect_remove(ch, af);
            af = ch->affected;
        } else {
            af = af->next;
        }
    }
        
    GET_REAL_GEN(ch) -= MIN(GET_REAL_GEN(ch), loss / 50);
    if (GET_LEVEL(ch) <= (loss % 50)) {
        GET_REAL_GEN(ch) -= 1;
        lvl = 49 - (loss % 50) + GET_LEVEL(ch);
    } else
        lvl = GET_LEVEL(ch) - (loss % 50);
		
    lvl = MIN(49, MAX(1, lvl));
    // Initialize their character to a level 1 of that gen, without
    // messing with skills/spells and such
    GET_LEVEL(ch) = 1;
    GET_EXP(ch) = 1;
    GET_MAX_HIT(ch) = 20;
    GET_MAX_MANA(ch) = 100;
    GET_MAX_MOVE(ch) = 82;
    old_hit = GET_HIT(ch);
    old_mana = GET_MANA(ch);
    old_move = GET_MOVE(ch);

    advance_level(ch, true);
    GET_MAX_MOVE(ch) += GET_CON(ch);

    // 
    while (--lvl) {
        GET_LEVEL(ch)++;
        advance_level(ch, true);
    }

    GET_HIT(ch) = MIN(old_hit, GET_MAX_HIT(ch));
    GET_MANA(ch) = MIN(old_mana, GET_MAX_MANA(ch));
    GET_MOVE(ch) = MIN(old_move, GET_MAX_MOVE(ch));

    // Remove all the skills that they shouldn't have
    for (int i = 1;i < MAX_SPELLS;i++)
        if (!ABLE_TO_LEARN(ch, i))
            SET_SKILL(ch, i, 0);

    // They're now that level, but without experience, and with extra
    // life points, pracs, and such.  Make it all sane.
    GET_EXP(ch) = exp_scale[(int)GET_LEVEL(ch)];
    GET_LIFE_POINTS(ch) = 0;
    GET_SEVERITY(ch) = 0;
    GET_INVIS_LVL(ch) = MIN(GET_LEVEL(ch), GET_INVIS_LVL(ch));

    // And they get uglier, too!
    GET_CHA(ch) = MAX(3, GET_CHA(ch) - 2);

    mudlog(LVL_AMBASSADOR, NRM, true,
           "%s losing %d levels for outlaw flag: now gen %d, lvl %d",
           GET_NAME(ch), loss, GET_REMORT_GEN(ch), GET_LEVEL(ch));
}