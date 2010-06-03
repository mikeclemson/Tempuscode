#include <functional>
#include <ext/functional>

#include "room_data.h"
#include "creature.h"
#include "utils.h"
#include "quest.h"
#include "bomb.h"
#include "handler.h"
#include "player_table.h"
#include "spells.h"
#include "char_class.h"
#include "comm.h"
#include "security.h"

bool
is_arena_combat(struct creature *ch, struct creature *vict)
{
	if (!vict->in_room)
		return false;

	if (ROOM_FLAGGED(vict->in_room, ROOM_ARENA))
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

    if (ROOM_FLAGGED(ch->in_room, ROOM_ARENA))
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
is_npk_combat(struct creature *ch, struct creature *vict) {
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

// Calculates the amount of reputation gained by the killer if they
// were to pkill the victim
int
pk_reputation_gain(struct creature *perp, struct creature *victim)
{
	if (perp == victim
        || IS_NPC(perp)
        || IS_NPC(victim)
        || !PRF2_FLAGGED(perp, PRF2_PKILLER)
        || perp->findCombat(victim))
        return 0;

    // Start with 10 for causing hassle
    int gain = 10;

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

    return gain;
}

struct creature *
find_responsible_party(struct creature *attacker, struct creature *victim)
{
    struct creature *perp = NULL;
    struct creature *ch;

    // if the victim is charmed, their master is at fault for letting
    // them die.  Charming can potentially be chained, so we find the
    // "top" master who is a PC in the same room here.
    for (ch = victim;
         AFF_FLAGGED(ch, AFF_CHARM)
             && ch->master
             && ch->in_room == ch->master->in_room;
         ch = ch->master)
        if (IS_PC(ch))
            perp = ch;
    if (perp)
        return perp;

    // if the attacker is charmed, their master is responsible for
    // their behavior.  We also handle chained charms here.
    for (ch = attacker;
         AFF_FLAGGED(ch, AFF_CHARM)
             && ch->master
             && ch->in_room == ch->master->in_room;
         ch = ch->master)
        if (IS_PC(ch))
            perp = ch;
    if (perp)
        return perp;

    // if neither was charmed, the attacker acted alone and is solely
    // responsible

    return attacker;
}

void
check_attack(struct creature *attacker, struct creature *victim)
{
    bool is_bountied(struct creature *hunter, struct creature *vict);
	struct creature *perp;

    // No reputation for attacking in arena
    if (is_arena_combat(attacker, victim))
        return;

	perp = find_responsible_party(attacker, victim);

    // no reputation for attacking a bountied person
    if (is_bountied(perp, victim))
        return;

    int gain = pk_reputation_gain(perp, victim);

    if (!gain)
        return;

    gain = MAX(1, gain / 5);
    perp->gain_reputation(gain);

    mudlog(LVL_IMMORT, CMP, true,
           "%s gained %d reputation for attacking %s", GET_NAME(perp),
           gain, GET_NAME(victim));
    GET_GRIEVANCES(victim).push_back(Grievance(time(NULL), GET_IDNUM(perp), gain, Grievance_ATTACK));
}

void
count_pkill(struct creature *killer, struct creature *victim)
{
	bool award_bounty(struct creature *, struct creature *);
	struct creature *perp;

    if (is_arena_combat(killer, victim))
        return;

	perp = find_responsible_party(killer, victim);

	GET_PKILLS(perp)++;

    if (award_bounty(perp, victim))
        return;

    int gain = pk_reputation_gain(perp, victim);

    if (!gain)
        return;

    perp->gain_reputation(gain);

    mudlog(LVL_IMMORT, CMP, true,
           "%s gained %d reputation for murdering %s", GET_NAME(perp),
           gain, GET_NAME(victim));
    GET_GRIEVANCES(victim).push_back(Grievance(time(NULL), GET_IDNUM(perp), gain, Grievance_MURDER));
}


void
check_thief(struct creature *ch, struct creature *victim)
{
	struct creature *perp;

	// First we need to find the perp
	perp = find_responsible_party(ch, victim);

    int gain = pk_reputation_gain(perp, victim);

    if (!gain)
        return;

    gain = MAX(1, gain / 10);
    perp->gain_reputation(gain);

    mudlog(LVL_IMMORT, CMP, true,
           "%s gained %d reputation for stealing from %s", GET_NAME(perp),
           gain, GET_NAME(victim));
    GET_GRIEVANCES(victim).push_back(Grievance(time(NULL), GET_IDNUM(perp), gain, Grievance_THEFT));

    if (is_arena_combat(ch, victim))
    	mudlog(LVL_POWER, CMP, true,
               "%s pstealing from %s in arena",
		       GET_NAME(perp), GET_NAME(victim));
}

struct grievance_player_id : public unary_function<const Grievance, int> {
    int operator()(const Grievance &grievance) const
        {
            return grievance._player_id;
        }
};

struct grievance_time : public unary_function<const Grievance, int> {
    int operator()(const Grievance &grievance) const
        {
            return grievance._time;
        }
};

void
perform_pardon(struct creature *ch, struct creature *pardoned)
{
    std_list<Grievance>::iterator grievance_it;

    // If there's a grievance, enact the reputation increase for each one
    for (grievance_it = GET_GRIEVANCES(ch).begin();
         grievance_it != GET_GRIEVANCES(ch).end();
         ++grievance_it) {
        if (grievance_it->_player_id == GET_IDNUM(pardoned)) {

            if (grievance_it->_grievance == Grievance_MURDER) {
                mudlog(LVL_IMMORT, CMP, true,
                       "%s recovered %d reputation for murdering %s", GET_NAME(pardoned),
                       grievance_it->_rep, GET_NAME(ch));
            } else if (grievance_it->_grievance == Grievance_ATTACK) {
                mudlog(LVL_IMMORT, CMP, true,
                       "%s recovered %d reputation for attacking %s", GET_NAME(pardoned),
                       grievance_it->_rep, GET_NAME(ch));
            } else {
                mudlog(LVL_IMMORT, CMP, true,
                       "%s recovered %d reputation for stealing from %s", GET_NAME(pardoned),
                       grievance_it->_rep, GET_NAME(ch));
            }

            pardoned->gain_reputation(-(grievance_it->_rep));
        }
    }

    std_list<Grievance>::iterator last_it =
        std_remove_if(GET_GRIEVANCES(ch).begin(),
                       GET_GRIEVANCES(ch).end(),
                       __gnu_cxx_compose1(std::bind2nd(std::equal_to<int>(),
                                                        GET_IDNUM(pardoned)),
                                           grievance_player_id()));
    GET_GRIEVANCES(ch).erase(last_it, GET_GRIEVANCES(ch).end());
}

// Expire old grievances after 24 hours.
void
expire_old_grievances(struct creature *ch)
{
    time_t min_time = time(NULL) - 86400;
    std_list<Grievance>::iterator last_it =
        std_remove_if(GET_GRIEVANCES(ch).begin(),
                       GET_GRIEVANCES(ch).end(),
                       __gnu_cxx_compose1(
                           std_bind2nd(std::less<time_t>(), min_time),
                           grievance_time()));
    GET_GRIEVANCES(ch).erase(last_it, GET_GRIEVANCES(ch).end());

}

ACMD(do_pardon)
{
    if (!*argument) {
        send_to_char(ch, "You must specify someone to pardon!\r\n");
        return;
    }

    if (AFF_FLAGGED(ch, AFF_CHARM)) {
        send_to_char(ch, "You don't seem quite in your right mind...\r\n");
        return;
    }

    // Find who they're accusing
    char *pardoned_name = tmp_getword(&argument);
    if (!playerIndex.exists(pardoned_name)) {
        send_to_char(ch, "There's no one of that name to pardon.\r\n");
        return;
    }

    // Get the pardoned character
    struct creature *pardoned = get_char_in_world_by_idnum(playerIndex[pardoned_name]);
    bool loaded_pardoned = false;
    if (!pardoned) {
        loaded_pardoned = true;
        pardoned = new struct creature(true);
        playerIndex.loadPlayer(pardoned_name, pardoned);
    }

    // Do the imm pardon
    if (IS_IMMORT(ch) && Security_isMember(ch, "AdminFull")) {
        if (!PLR_FLAGGED(pardoned, PLR_THIEF | PLR_KILLER)) {
            send_to_char(ch, "Your victim is not flagged.\r\n");
            return;
        }
        REMOVE_BIT(PLR_FLAGS(pardoned), PLR_THIEF | PLR_KILLER);
        perform_pardon(ch, pardoned);
        send_to_char(ch, "Pardoned.\r\n");
        send_to_char(pardoned, "You have been pardoned by the Gods!\r\n");
        mudlog(MAX(LVL_GOD, GET_INVIS_LVL(ch)), NRM, true,
               "(GC) %s pardoned by %s", GET_NAME(pardoned),
               GET_NAME(ch));
    } else {

        // Find out if the player has a valid grievance against the pardoned
        std_list<Grievance>::iterator grievance_it;

        expire_old_grievances(ch);
        grievance_it = std_find(GET_GRIEVANCES(ch).begin(),
                                 GET_GRIEVANCES(ch).end(),
                                 GET_IDNUM(pardoned));

        if (grievance_it == GET_GRIEVANCES(ch).end()) {
            // If no grievance, increase the reputation of the pardoner
            send_to_char(ch, "%s has done nothing for you to pardon.\r\n",
                         GET_NAME(pardoned));
            return;
        }
        send_to_char(ch, "You pardon %s of %s crimes against you.\r\n",
                     GET_NAME(pardoned), HSHR(pardoned));
        send_to_char(pardoned, "%s pardons your crimes against %s.\r\n",
                     GET_NAME(ch), HMHR(ch));
        perform_pardon(ch, pardoned);
    }

    save_player_to_xml(ch);
    pardoned->saveToXML();
    if (loaded_pardoned)
        delete pardoned;
}

void
check_object_killer(struct obj_data *obj, struct creature *vict)
{
	struct creature cbuf(true);
	struct creature *killer = NULL;
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
			cbuf.account = struct account_retrieve(playerIndex.getstruct accountID(obj_id));
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
punish_killer_death(struct creature *ch)
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

    GET_REMORT_GEN(ch) -= MIN(GET_REMORT_GEN(ch), loss / 50);
    if (GET_LEVEL(ch) <= (loss % 50)) {
        GET_REMORT_GEN(ch) -= 1;
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
        if (!is_able_to_learn(ch, i))
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