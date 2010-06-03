//
// File: clan.c                     -- Part of TempusMUD
//
// Copyright 1998 by John Watson, all rights reserved.
//

/* ************************************************************************
*   File: clan.c                                        Part of CircleMUD *
************************************************************************ */

#ifdef HAS_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "structs.h"
#include "utils.h"
#include "comm.h"
#include "security.h"
#include "handler.h"
#include "db.h"
#include "screen.h"
#include "house.h"
#include "clan.h"
#include "char_class.h"
#include "players.h"
#include "tmpstr.h"
#include "accstr.h"

/* extern variables */
extern struct room_data *world;
extern struct descriptor_data *descriptor_list;
extern struct room_data *world;
extern struct spell_info_type spell_info[];
int check_mob_reaction(struct creature *ch, struct creature *vict);
char *save_wld(struct creature *ch, struct zone_data *zone);

struct clan_data *clan_list;
extern FILE *player_fl;
int player_i = 0;

void
REMOVE_ROOM_FROM_CLAN(struct room_list_elem *rm_list, struct clan_data *clan)
{
	struct room_list_elem *temp;
	REMOVE_FROM_LIST(rm_list, clan->room_list, next);
}

void
REMOVE_MEMBER_FROM_CLAN(struct clanmember_data *member, struct clan_data *clan)
{
	struct clanmember_data *temp;
	REMOVE_FROM_LIST(member, clan->member_list, next);
}

static int
clan_member_count(struct clan_data *clan)
{
    struct clanmember_data *member = NULL;
    int result = 0;

    for (member = clan->member_list;member;member = member->next)
        result++;
    return result;
}

static const char *
clan_rankname(struct clan_data *clan, int rank)
{
    if (clan->ranknames[rank])
        return clan->ranknames[rank];
    else if (!rank)
        return "the recruit";
    return "the member";
}

static bool
char_can_enroll(struct creature *ch, struct creature *vict, struct clan_data *clan)
{
    // Ensure data integrity between clan structures
    if (GET_CLAN(vict) && real_clan(GET_CLAN(vict))) {
        if (!real_clanmember(GET_IDNUM(vict), real_clan(GET_CLAN(vict))))
            GET_CLAN(vict) = 0;

		if (GET_CLAN(vict) == GET_CLAN(ch)) {
            send_to_char(ch, "That person is already in your clan.\r\n");
            return false;
		} else {
			send_to_char(ch, "You cannot while they are a member of another clan.\r\n");
            return false;
        }
    } else {
        struct clanmember_data *member;

        member = real_clanmember(GET_IDNUM(vict), clan);
        if (member) {
            send_to_char(ch, "Something weird just happened... try again.\r\n");
            REMOVE_MEMBER_FROM_CLAN(member, clan);
            free(member);
        }
    }

    // Enrollment conditions that apply to everyone
    if (IS_NPC(vict)) {
		send_to_char(ch, "You can only enroll player characters.\r\n");
        return false;
    }
    if (AFF_FLAGGED(ch, AFF_CHARM)) {
		send_to_char(ch, "You obviously aren't quite in your right mind.\r\n");
        return false;
    }

    if (Security_isMember(ch, "Clan"))
        return true;
    // Enrollment conditions that don't apply to clan administrators
    else if (PLR_FLAGGED(vict, PLR_FROZEN))
		send_to_char(ch, "They are frozen right now.  Wait until a god has mercy.\r\n");
	else if (GET_LEVEL(vict) < LVL_CAN_CLAN)
		send_to_char(ch, "Players must be level 10 before being inducted into the clan.\r\n");
    else if (clan_member_count(clan) > MAX_CLAN_MEMBERS)
        send_to_char(ch, "The max number of members has been reached for this clan.\r\n");
    else if (clan->owner == GET_IDNUM(ch))
        return true;
    // Enrollment conditions that don't apply to clan owners
    else if (GET_CLAN(ch) != clan->number || !PLR_FLAGGED(ch, PLR_CLAN_LEADER))
		send_to_char(ch, "You are not a leader of the clan!\r\n");
    else
        return true;

    return false;
}

static bool
char_can_dismiss(struct creature *ch, struct creature *vict, struct clan_data *clan)
{
    struct clanmember_data *ch_member = real_clanmember(GET_IDNUM(ch), clan);
    struct clanmember_data *vict_member = real_clanmember(GET_IDNUM(vict), clan);

    if (vict == ch)
        send_to_char(ch, "Try resigning if you want to leave the clan.\r\n");
    else if (AFF_FLAGGED(ch, AFF_CHARM))
		send_to_char(ch, "You obviously aren't quite in your right mind.\r\n");
    else if (Security_isMember(ch, "Clan"))
        return true;
    // Dismissal conditions that don't apply to clan administrators
    else if (!clan)
        send_to_char(ch, "Try joining a clan first.\r\n");
    else if (!vict_member || GET_CLAN(vict) != GET_CLAN(ch))
        send_to_char(ch, "Umm, why don't you check the clan list, okay?\r\n");
    else if (clan->owner == GET_CLAN(ch))
        return true;
    // Dismissal conditions that don't apply to clan owners
    else if (GET_CLAN(ch) != clan->number || !PLR_FLAGGED(ch, PLR_CLAN_LEADER))
		send_to_char(ch, "You are not a leader of the clan!\r\n");
    else if (ch_member->rank <= vict_member->rank)
		send_to_char(ch, "You don't have the rank for that.\r\n");
    else if (PLR_FLAGGED(vict, PLR_CLAN_LEADER))
		send_to_char(ch, "You cannot dismiss co-leaders.\r\n");
    else
        return true;

    return false;
}

static bool
char_can_promote(struct creature *ch, struct creature *vict, struct clan_data *clan)
{
    struct clanmember_data *ch_member = real_clanmember(GET_IDNUM(ch), clan);
    struct clanmember_data *vict_member = real_clanmember(GET_IDNUM(vict), clan);

    if (AFF_FLAGGED(ch, AFF_CHARM))
		send_to_char(ch, "You obviously aren't quite in your right mind.\r\n");
    else if (!vict_member)
        send_to_char(ch, "You are not a member of that person's clan!\r\n");
    else if (vict_member->rank >= clan->top_rank
             && PLR_FLAGGED(vict, PLR_CLAN_LEADER))
		send_to_char(ch, "That person is already at the top rank.\r\n");
    else if (Security_isMember(ch, "Clan"))
        return true;
    // Promotion conditions that don't apply to clan administrators
    else if (!clan)
        send_to_char(ch, "Try joining a clan first.\r\n");
    else if (real_clan(GET_CLAN(vict)) != clan)
		send_to_char(ch, "You are not a member of that person's clan!\r\n");
    else if (clan->owner == GET_CLAN(ch))
        return true;
    // Promotion conditions that don't apply to clan owners
    else if (vict == ch)
        send_to_char(ch, "Promote yourself?  Haha.\r\n");
    else if (PLR_FLAGGED(ch, PLR_CLAN_LEADER))
        return true;
    // Promotion conditions that don't apply to clan leaders
    else if (ch_member->rank <= vict_member->rank)
		send_to_char(ch, "You don't have the rank for that.\r\n");
    else
        return true;

    return false;
}

ACMD(do_enroll)
{
	struct creature *vict = 0;
	struct clan_data *clan = real_clan(GET_CLAN(ch));
	struct clanmember_data *member = NULL;
	char *msg, *member_str;

	member_str = tmp_getword(&argument);

	if (Security_isMember(ch, "Clan")) {
		char *clan_str = tmp_getword(&argument);

		if (!*clan_str) {
			send_to_char(ch, "Enroll them into which clan?\r\n");
			return;
		} else {
			clan = clan_by_name(clan_str);
			if (!clan) {
				send_to_char(ch, "That clan doesn't exist.\r\n");
				return;
			}
		}
	}

	if (!clan)
		send_to_char(ch, "Hmm... You need to be in a clan yourself, first.\r\n");
	else if (!*member_str)
		send_to_char(ch, "You must specify the player to enroll.\r\n");
	else if (!(vict = get_char_room_vis(ch, member_str)))
		send_to_char(ch, "You don't see that person.\r\n");
    else if (char_can_enroll(ch, vict, clan)) {
		REMOVE_BIT(PLR_FLAGS(vict), PLR_CLAN_LEADER);
		send_to_char(vict, "You have been inducted into clan %s by %s!\r\n",
			clan->name, GET_NAME(ch));
		msg = tmp_sprintf("%s has been inducted into clan %s by %s!",
			GET_NAME(vict), clan->name, GET_NAME(ch));
		mudlog(GET_INVIS_LVL(ch), NRM, true, "%s", msg);
		msg = tmp_strcat(msg, "\r\n",NULL);
		send_to_clan(msg, GET_CLAN(ch));
		GET_CLAN(vict) = clan->number;
		CREATE(member, struct clanmember_data, 1);
		member->idnum = GET_IDNUM(vict);
		member->rank = 0;
		member->next = clan->member_list;
		clan->member_list = member;
		sort_clanmembers(clan);
		sql_exec("insert into clan_members (clan, player, rank, no_mail) values (%d, %ld, %d, 'f')",
			clan->number, GET_IDNUM(vict), 0);
	}
}

ACMD(do_dismiss)
{
	struct creature *vict;
	struct clan_data *clan = real_clan(GET_CLAN(ch));
	struct clanmember_data *member = NULL;
	bool in_file = false;
	long idnum = -1;
	char *arg, *msg;

	arg = tmp_getword(&argument);
	skip_spaces(&argument);

	if (!clan && !Security_isMember(ch, "Clan")) {
		send_to_char(ch, "Try joining a clan first.\r\n");
		return;
	} else if (!*arg) {
		send_to_char(ch, "You must specify the clan member to dismiss.\r\n");
		return;
	}
	/* Find the player. */
	if ((idnum = player_idnum_by_name(arg)) == 0) {
		send_to_char(ch, "There is no character named '%s'\r\n", arg);
		return;
	}

	if (!(vict = get_char_in_world_by_idnum(idnum))) {
		// load the char from file
		in_file = true;

		vict = load_player_from_xml(idnum);
		if (!vict) {
			send_to_char(ch, "Error loading char from file.\r\n");
			return;
		}
	}

	/* Was the player found? */
	if (!vict) {
		send_to_char(ch, "That person isn't in your clan.\r\n");
		if (in_file)
			extract_creature(vict);
		return;
	}

	clan = real_clan(GET_CLAN(vict));
	if (!clan)
		send_to_char(ch, "They aren't in the clan.\r\n");
	else if (char_can_dismiss(ch, vict, clan)) {
		send_to_char(vict, "You have been dismissed from clan %s by %s!\r\n",
			clan->name, GET_NAME(ch));
		GET_CLAN(vict) = 0;
		msg = tmp_sprintf("%s has been dismissed from clan %s by %s!",
			GET_NAME(vict), clan->name, GET_NAME(ch));
		mudlog(GET_INVIS_LVL(ch), NRM, true, "%s", msg);
		msg = tmp_strcat(msg, "\r\n",NULL);
		send_to_clan(msg, GET_CLAN(ch));
		if ((member = real_clanmember(GET_IDNUM(vict), clan))) {
			REMOVE_MEMBER_FROM_CLAN(member, clan);
			free(member);
		}
		REMOVE_BIT(PLR_FLAGS(vict), PLR_CLAN_LEADER);
		sql_exec("delete from clan_members where player=%ld", GET_IDNUM(vict));

        save_player_to_xml(vict);
        send_to_char(ch, "Player dismissed.\r\n");

		if (in_file)
			extract_creature(vict);
	}
}

ACMD(do_resign)
{

	struct clan_data *clan = real_clan(GET_CLAN(ch));
	struct clanmember_data *member = NULL;
	char *msg;

	skip_spaces(&argument);

	if (IS_NPC(ch))
		send_to_char(ch, "NPC's cannot resign...\r\n");
	else if (!clan)
		send_to_char(ch, "You need to be in a clan before you resign from it.\r\n");
	else if (AFF_FLAGGED(ch, AFF_CHARM))
		send_to_char(ch, "You obviously aren't quite in your right mind.\r\n");
	else if (strcmp(argument, "yes"))
		send_to_char(ch,
			"You must type 'resign yes' to leave your clan.\r\n");
	else {
		send_to_char(ch, "You have resigned from clan %s.\r\n", clan->name);
		msg = tmp_sprintf("%s has resigned from clan %s.", GET_NAME(ch),
			clan->name);
		mudlog(GET_INVIS_LVL(ch), NRM, true, "%s", msg);
		msg = tmp_strcat(msg, "\r\n",NULL);
		send_to_clan(msg, GET_CLAN(ch));
		GET_CLAN(ch) = 0;
		REMOVE_BIT(PLR_FLAGS(ch), PLR_CLAN_LEADER);
		if (clan->owner == GET_IDNUM(ch))
			clan->owner = 0;
		if ((member = real_clanmember(GET_IDNUM(ch), clan))) {
			REMOVE_MEMBER_FROM_CLAN(member, clan);
			free(member);
		}
		sql_exec("delete from clan_members where player=%ld", GET_IDNUM(ch));
	}
}

ACMD(do_promote)
{
	struct clan_data *clan = real_clan(GET_CLAN(ch));
	struct creature *vict = NULL;
	char *msg;

	skip_spaces(&argument);

	if (!clan)
		send_to_char(ch, "You are not even in a clan.\r\n");
    else if (!*argument)
		send_to_char(ch, "You must specify the person to promote.\r\n");
	else if (!(vict = get_char_room_vis(ch, argument)))
		send_to_char(ch, "No-one around by that name.\r\n");
    else if (char_can_promote(ch, vict, clan)) {
        struct clanmember_data *vict_member = real_clanmember(GET_IDNUM(vict), clan);

        if (vict_member->rank >= clan->top_rank) {
            // Promotion to clan leader
            SET_BIT(PLR_FLAGS(vict), PLR_CLAN_LEADER);
            msg = tmp_sprintf("%s has promoted %s to clan leader status.",
                              GET_NAME(ch),
                              (ch == vict) ? "self":GET_NAME(vict));
            slog("%s", msg);
            msg = tmp_strcat(msg, "\r\n",NULL);
            send_to_clan(msg, clan->number);
            save_player_to_xml(vict);
        } else {
            // Normal rank promotion
			vict_member->rank++;
			sql_exec("update clan_members set rank=%d where player=%ld",
				vict_member->rank, GET_IDNUM(vict));
			msg = tmp_sprintf("%s has promoted %s to clan rank %s (%d)",
                              GET_NAME(ch),
                              (ch == vict) ? "self":GET_NAME(vict),
                              clan_rankname(clan, vict_member->rank),
                              vict_member->rank);
			slog("%s", msg);
			msg = tmp_strcat(msg, "\r\n",NULL);
			send_to_clan(msg, clan->number);
			sort_clanmembers(clan);
        }
    }
}

ACMD(do_demote)
{
	struct clan_data *clan = real_clan(GET_CLAN(ch));
	struct clanmember_data *member1, *member2;
	struct creature *vict = NULL;
	char *msg;

	skip_spaces(&argument);

	if (!clan)
		send_to_char(ch, "You are not even in a clan.\r\n");
	else if (!*argument)
		send_to_char(ch, "You must specify the person to demote.\r\n");
	else if (!(vict = get_char_room_vis(ch, argument)))
		send_to_char(ch, "No-one around by that name.\r\n");
	else if (!(member1 = real_clanmember(GET_IDNUM(ch), clan)))
		send_to_char(ch, "You are not properly installed in the clan.\r\n");
	else if (AFF_FLAGGED(ch, AFF_CHARM))
		send_to_char(ch, "You obviously aren't quite in your right mind.\r\n");
	else if (ch == vict) {
		if (!member1->rank)
			send_to_char(ch, "You are already at the bottom of the totem pole.\r\n");
		else {
			member1->rank--;
			sql_exec("update clan_members set rank=%d where player=%ld",
				member1->rank, GET_IDNUM(vict));
			msg = tmp_sprintf("%s has demoted self to clan rank %s (%d)",
                              GET_NAME(ch),
                              clan_rankname(clan, member1->rank),
                              member1->rank);
			slog("%s", msg);
			msg = tmp_strcat(msg, "\r\n",NULL);
			send_to_clan(msg, clan->number);
			sort_clanmembers(clan);
		}
	} else if (real_clan(GET_CLAN(vict)) != clan) {
		act("$N is not a member of your clan.", false, ch, 0, vict, TO_CHAR);
	} else if (!(member2 = real_clanmember(GET_IDNUM(vict), clan))) {
		act("$N is not properly installed in the clan.\r\n",
			false, ch, 0, vict, TO_CHAR);
	} else if (!PLR_FLAGGED(ch, PLR_CLAN_LEADER)
		&& !Security_isMember(ch, "Clan")) {
		send_to_char(ch, "You are unable to demote.\r\n");
	} else if (member2->rank >= member1->rank
		|| PLR_FLAGGED(vict, PLR_CLAN_LEADER)) {
		act("You are not in a position to demote $M.", false, ch, 0, vict,
			TO_CHAR);
	} else if (member2->rank <= 0) {
		send_to_char(ch, "They are already as low as they can go.\r\n");
		if (member2->rank < 0) {
			errlog("clan member with rank < 0");
			member2->rank = 0;
		}
	} else {
		member2->rank--;
		sql_exec("update clan_members set rank=%d where player=%ld",
			member2->rank, GET_IDNUM(vict));
		msg = tmp_sprintf("%s has demoted %s to clan rank %s (%d)",
                          GET_NAME(ch),
                          GET_NAME(vict),
                          clan_rankname(clan, member2->rank),
                          member2->rank);
		slog("%s", msg);
		msg = tmp_strcat(msg, "\r\n",NULL);
		send_to_clan(msg, clan->number);
		sort_clanmembers(clan);
	}
}

ACMD(do_clanmail)
{
	struct clan_data *clan = real_clan(GET_CLAN(ch));
    struct clanmember_data *member;

	skip_spaces(&argument);

	if (!clan)
		send_to_char(ch, "You are not even in a clan.\r\n");
	else if (!(member = real_clanmember(GET_IDNUM(ch), clan)))
		send_to_char(ch, "You are not properly installed in the clan.\r\n");
    else {
        member->no_mail = !member->no_mail;
        sql_exec("update clan_members set no_mail='%c' where player=%ld",
                 (member->no_mail) ? 'T':'F', GET_IDNUM(ch));
        if (member->no_mail)

            send_to_char(ch, "You will no longer receive mail addressed to your clan.\r\n");
        else
			send_to_char(ch, "You will now receive mail addressed to your clan.\r\n");
    }
}

static void
acc_print_clan_members(struct creature *ch, struct clan_data *clan, bool complete, int min_lev)
{
	struct creature *i;
	struct clanmember_data *member = NULL;
	struct descriptor_data *d;
	char *name;

	for (member = clan->member_list; member; member = member->next) {
        bool found = false;
		for (d = descriptor_list; d && !found; d = d->next) {
			if (IS_PLAYING(d)) {
				i = ((d->original && GET_LEVEL(ch) > GET_LEVEL(d->original)) ?
					d->original : d->creature);
				if (i
                    && GET_CLAN(i) == GET_CLAN(ch)
                    && GET_IDNUM(i) == member->idnum
                    && can_see_creature(ch, i)
					&& GET_LEVEL(i) >= min_lev
                    && i->in_room) {
					name = tmp_strcat(GET_NAME(i), " ",
                                      clan_rankname(clan, member->rank),
                                      " (online)", NULL);
					if (d->original)
						acc_sprintf(
							"%s[%s%2d %s%s]%s %s%-40s%s - %s%s%s %s(in %s)%s\r\n",
							CCGRN(ch, C_NRM),
							CCNRM(ch, C_NRM),
							GET_LEVEL(i),
							char_class_abbrevs[GET_CLASS(i)],
							CCGRN(ch, C_NRM),
							CCNRM(ch, C_NRM),
							(GET_LEVEL(i) >= LVL_AMBASSADOR ? CCGRN(ch,
									C_NRM) : (PLR_FLAGGED(i,
										PLR_CLAN_LEADER) ? CCCYN(ch,
										C_NRM) : "")),
							name,
							((GET_LEVEL(i) >= LVL_AMBASSADOR
									|| PLR_FLAGGED(i,
										PLR_CLAN_LEADER)) ? CCNRM(ch, C_NRM) : ""),
							CCCYN(ch, C_NRM),
							(d->creature->in_room->zone == ch->in_room->zone) ?
								(check_sight_room(ch, d->creature->in_room)) ?
									d->creature->in_room->name
									: "You cannot tell..."
								: d->creature->in_room->zone->name,
							CCNRM(ch, C_NRM),
							CCRED(ch, C_CMP),
							GET_NAME(d->creature),
							CCNRM(ch, C_CMP));
					else if (GET_LEVEL(i) >= LVL_AMBASSADOR)
						acc_sprintf("%s[%s%s%s]%s %-40s%s - %s%s%s\r\n",
							CCYEL_BLD(ch, C_NRM),
							CCNRM_GRN(ch, C_SPR),
							level_abbrevs[GET_LEVEL(i) - LVL_AMBASSADOR],
							CCYEL_BLD(ch, C_NRM),
							CCNRM_GRN(ch, C_SPR),
							name,
							CCNRM(ch, C_SPR),
							CCCYN(ch, C_NRM),
							(i->in_room->zone == ch->in_room->zone) ?
								((check_sight_room(ch, i->in_room)) ?
									i->in_room->name
									: "You cannot tell...")
								: i->in_room->zone->name,
							CCNRM(ch, C_NRM));
					else
						acc_sprintf("%s[%s%2d %s%s]%s %s%-40s%s - %s%s%s\r\n",
							CCGRN(ch, C_NRM),
							CCNRM(ch, C_NRM),
							GET_LEVEL(i),
							char_class_abbrevs[GET_CLASS(i)],
							CCGRN(ch, C_NRM),
							CCNRM(ch, C_NRM),
							(PLR_FLAGGED(i, PLR_CLAN_LEADER) ?
								CCCYN(ch, C_NRM) : ""),
							name,
							(PLR_FLAGGED(i, PLR_CLAN_LEADER) ?
								CCNRM(ch, C_NRM) : ""),
							CCCYN(ch, C_NRM),
							(i->in_room->zone == ch->in_room->zone) ?
								((check_sight_room(ch, i->in_room)) ?
									i->in_room->name
									: "You cannot tell...")
								: i->in_room->zone->name,
							CCNRM(ch, C_NRM));

					found = true;
				}
			}
		}
		if (complete && !found) {
			i = load_player_from_xml(member->idnum);
			if (i) {
				name = tmp_strcat(GET_NAME(i), " ",
                                  clan_rankname(clan, member->rank), NULL);

				if (GET_LEVEL(i) >= LVL_AMBASSADOR)
					acc_sprintf("%s[%s%s%s]%s %-40s%s\r\n",
						CCYEL_BLD(ch, C_NRM), CCNRM_GRN(ch, C_SPR),
						level_abbrevs[GET_LEVEL(i) - LVL_AMBASSADOR],
						CCYEL_BLD(ch, C_NRM), CCNRM_GRN(ch, C_SPR),
						name, CCNRM(ch, C_SPR));
				else
					acc_sprintf("%s[%s%2d %s%s]%s %s%-40s%s\r\n",
						CCGRN(ch, C_NRM), CCNRM(ch, C_NRM), GET_LEVEL(i),
						char_class_abbrevs[GET_CLASS(i)], CCGRN(ch,
							C_NRM), CCNRM(ch, C_NRM), (PLR_FLAGGED(i,
								PLR_CLAN_LEADER) ? CCCYN(ch, C_NRM) : ""),
						name, CCNRM(ch, C_NRM));
			}
			extract_creature(i);
		}
	}
}

ACMD(do_clanlist)
{
	struct clan_data *clan = real_clan(GET_CLAN(ch));
	struct clanmember_data *ch_member = NULL;
	int min_lev = 0;
	bool complete = false;
	char *arg;

	if (!clan) {
		send_to_char(ch, "You are not a member of any clan.\r\n");
		return;
	}

	ch_member = real_clanmember(GET_IDNUM(ch), clan);

    arg = tmp_getword(&argument);
	while (*arg) {
		if (is_number(arg)) {
			min_lev = atoi(arg);
			if (min_lev < 0 || min_lev > LVL_GRIMP) {
				send_to_char(ch, "Try a number between 0 and 60, please.\r\n");
				return;
			}
		} else if (is_abbrev(arg, "all") || is_abbrev(arg, "complete")) {
			complete = true;
		} else {
			send_to_char(ch, "Clanlist usage: clanlist [minlevel] ['all']\r\n");
			return;
		}
        arg = tmp_getword(&argument);
	}

	acc_string_clear();
	acc_strcat("Members of clan ", clan->name, " :\r\n", NULL);
    acc_print_clan_members(ch, clan, complete, min_lev);
	page_string(ch->desc, acc_get_string());
}

ACMD(do_cinfo)
{

	struct clan_data *clan = real_clan(GET_CLAN(ch));
	struct clanmember_data *member;
	struct room_list_elem *rm_list = NULL;
	int found = 0;
	int i;

	if (!clan)
		send_to_char(ch, "You are not a member of any clan.\r\n");
	else {
		acc_string_clear();
		for (i = 0, member = clan->member_list; member; member = member->next)
			i++;
		acc_sprintf("Information on clan %s%s%s:\r\n\r\n"
			"Clan badge: '%s%s%s', Clan headcount: %d, "
			"Clan bank account: %lld\r\nClan ranks:\r\n",
			CCCYN(ch, C_NRM), clan->name, CCNRM(ch, C_NRM),
			CCCYN(ch, C_NRM), clan->badge, CCNRM(ch, C_NRM),
			i, clan->bank_account);
		for (i = clan->top_rank; i >= 0; i--) {
			acc_sprintf(" (%2d)  %s%s%s\r\n", i, CCYEL(ch, C_NRM),
                        clan_rankname(clan, i),
                        CCNRM(ch, C_NRM));
		}

		acc_strcat("Clan rooms:\r\n",NULL);
		for (rm_list = clan->room_list; rm_list; rm_list = rm_list->next) {
			if (rm_list->room
				&& ROOM_FLAGGED(rm_list->room, ROOM_CLAN_HOUSE)) {
				acc_strcat(CCCYN(ch, C_NRM),
					rm_list->room->name,
					CCNRM(ch, C_NRM), "\r\n", NULL);
				++found;
			}
		}
		if (!found)
			acc_strcat("None.\r\n",NULL);

		page_string(ch->desc, acc_get_string());
	}
}

ACMD(do_clanpasswd)
{
	struct special_search_data *srch = NULL;
	struct clan_data *clan = NULL;

	if (!GET_CLAN(ch) || !PLR_FLAGGED(ch, PLR_CLAN_LEADER)) {
		send_to_char(ch, "Only clan leaders can do this.\r\n");
		return;
	}
	if (!(clan = real_clan(GET_CLAN(ch)))) {
		send_to_char(ch, "Something about your clan is screwed up.\r\n");
		return;
	}
	if (!ROOM_FLAGGED(ch->in_room, ROOM_CLAN_HOUSE)) {
		send_to_char(ch, "You can't set any clan passwds in this room.\r\n");
		return;
	}
	if (!clan_house_can_enter(ch, ch->in_room)) {
		send_to_char(ch, "You can't set passwds in other people's clan house.\r\n");
		return;
	}

	skip_spaces(&argument);
	if (!*argument) {
		send_to_char(ch, "Set the clanpasswd to what?\r\n");
		return;
	}

	for (srch = ch->in_room->search; srch; srch = srch->next) {
		if (srch->command == SEARCH_COM_TRANSPORT &&
			SRCH_FLAGGED(srch, SRCH_CLANPASSWD))
			break;
	}

	if (!srch) {
		send_to_char(ch, "There is no clanpasswd search in this room.\r\n");
		return;
	}

	if (AFF_FLAGGED(ch, AFF_CHARM)) {
		send_to_char(ch, "You obviously aren't quite in your right mind.\r\n");
		return;
	}
	free(srch->keywords);
	srch->keywords = strdup(argument);
	if (!save_wld(ch, ch->in_room->zone))
		send_to_char(ch, "New clan password set here to '%s'.\r\n", argument);
	else
		send_to_char(ch, "There was a problem saving your clan password.\r\n");
}

struct clan_data *
real_clan(int vnum)
{
	struct clan_data *clan = NULL;

	if (!(vnum))
		return NULL;

	for (clan = clan_list; clan; clan = clan->next)
		if (clan->number == vnum)
			return (clan);

	return (NULL);
}

struct clan_data*
clan_by_owner( int idnum )
{
    struct clan_data *clan;

	for (clan = clan_list; clan; clan = clan->next)
		if( clan->owner == idnum )
			return clan;
	return NULL;
}

struct clan_data *
clan_by_name(char *arg)
{
	struct clan_data *clan = NULL;
	int clan_num = -1;

	if (is_number(arg))
		clan_num = atoi(arg);

	skip_spaces(&arg);
	if (!*arg)
		return NULL;

	for (clan = clan_list; clan; clan = clan->next)
		if (is_abbrev(arg, clan->name) || clan->number == clan_num)
			return (clan);

	return (NULL);
}

struct clanmember_data *
real_clanmember(long idnum, struct clan_data *clan)
{

	struct clanmember_data *member;
	for (member = clan->member_list; member; member = member->next)
		if (member->idnum == idnum)
			return (member);

	return (NULL);
};

typedef struct cedit_command_data {
	const char *keyword;
	int level;
} cedit_command_data;

cedit_command_data cedit_keys[] = {
	{"create", 1},
	{"delete", 1},
	{"set", 0},
	{"show", 0},
	{"add", 0},
	{"remove", 0},
	{"sort", 1},
	{NULL, 0}
};

ACMD(do_cedit)
{

	struct clan_data *clan = NULL;
	struct clanmember_data *member = NULL;
	struct room_list_elem *rm_list = NULL;
	struct room_data *room = NULL;
	int cedit_command, i, j;
	money_t money;
	char *arg1, *arg2, *arg3;

	skip_spaces(&argument);

	arg1 = tmp_getword(&argument);
	for (cedit_command = 0; cedit_keys[cedit_command].keyword; cedit_command++)
		if (!strcasecmp(arg1, cedit_keys[cedit_command].keyword))
			break;

	if (!cedit_keys[cedit_command].keyword) {
		send_to_char(ch,
			"Valid cedit options:  save, create, delete(*), set, show, add, remove.\r\n");
		return;
	}

	if (!Security_isMember(ch, "Clan")) {
		send_to_char(ch, "Sorry, you can't use the cedit command.\r\n");
	}

	if (cedit_keys[cedit_command].level == 1
		&& !Security_isMember(ch, "WizardFull")) {
		send_to_char(ch, "Sorry, you can't use that cedit command.\r\n");
		return;
	}

	arg1 = tmp_getword(&argument);
	if (*arg1) {
		if (is_number(arg1))
			clan = real_clan(atoi(arg1));
		else
			clan = clan_by_name(arg1);

		// protect quaker and null
		if (clan && GET_LEVEL(ch) < LVL_CREATOR && (clan->number == 0
				|| clan->number == 6)) {
			send_to_char(ch, "Sorry, you cannot edit this clan.\r\n");
			return;
		}

	}

	switch (cedit_command) {

	case 0:			/*** create ***/
		{
			int clan_number = 0;
			if (!*arg1) {
				send_to_char(ch, "Create clan with what vnum?\r\n");
				break;
			}
			if (!is_number(arg1)) {
				send_to_char(ch, "You must specify the clan numerically.");
				break;
			}
			clan_number = atoi(arg1);
			if (clan_number <= 0) {
				send_to_char(ch, "You must specify the clan numerically.");
				break;
			} else if (real_clan(atoi(arg1))) {
				send_to_char(ch, "A clan already exists with that vnum.\r\n");
				break;
			} else if (clan_number > 255) {
				send_to_char(ch, "The clan number must be less than 255.\r\n");
				break;
			} else if ((clan = create_clan(atoi(arg1)))) {
				send_to_char(ch, "Clan created.\r\n");
				slog("(cedit) %s created clan %d.", GET_NAME(ch),
					clan->number);
			} else {
				send_to_char(ch, "There was an error creating the clan.\r\n");
			}
			break;
		}
	case 1:			  /*** delete ***/
		if (!clan) {
			if (!*arg1)
				send_to_char(ch, "Delete what clan?\r\n");
			else {
				send_to_char(ch, "Clan '%s' does not exist.\r\n", arg1);
			}
			return;
		} else {
			i = clan->number;
			if (!delete_clan(clan)) {
				send_to_char(ch, "Clan deleted.  Sucked anyway.\r\n");
				slog("(cedit) %s deleted clan %d.", GET_NAME(ch), i);
			} else
				send_to_char(ch, "ERROR occurred while deleting clan.\r\n");
		}
		break;

	case 2:			  /*** set    ***/
		if (!*arg1) {
			send_to_char(ch,
				"Usage: cedit set <vnum> <name|badge|rank|bank|member|owner>['top']<value>\r\n");
			return;
		}

		arg2 = tmp_getword(&argument);
		skip_spaces(&argument);

		if (!*argument || !*arg2) {
			send_to_char(ch,
				"Usage: cedit set <clan> <name|badge|rank|bank|member|owner>['top']<value>\r\n");
			return;
		}
		if (!clan) {
			send_to_char(ch, "Clan '%s' does not exist.\r\n", arg1);
			return;
		}
		// cedit set name
		if (is_abbrev(arg2, "name")) {
			if (strlen(argument) > MAX_CLAN_NAME) {
				send_to_char(ch, "Name too long.  Maximum %d characters.\r\n",
					MAX_CLAN_NAME);
				return;
			}
			if (clan->name) {
				free(clan->name);
			}
			clan->name = strdup(argument);
			sql_exec("update clans set name='%s' where idnum=%d",
				clan->name, clan->number);
			slog("(cedit) %s set clan %d name to '%s'.", GET_NAME(ch),
				clan->number, clan->name);

		}
		// cedit set badge
		else if (is_abbrev(arg2, "badge")) {
			if (strlen(argument) > MAX_CLAN_BADGE - 1) {
				send_to_char(ch, "Badge too long.  Maximum %d characters.\r\n",
					MAX_CLAN_BADGE - 1);
				return;
			}
			if (clan->badge) {
				free(clan->badge);
			}
			clan->badge = strdup(argument);
			sql_exec("update clans set badge='%s' where idnum=%d",
                tmp_sqlescape(clan->badge), clan->number);
			slog("(cedit) %s set clan %d badge to '%s'.", GET_NAME(ch),
				clan->number, clan->badge);

		}
		// cedit set rank
		else if (is_abbrev(arg2, "rank")) {

			arg3 = tmp_getword(&argument);
			skip_spaces(&argument);

			if (is_abbrev(arg3, "top")) {
				if (!*argument) {
					send_to_char(ch, "Set top rank of clan to what?\r\n");
					return;
				}
				if (!is_number(argument) || (i = atoi(argument)) < 0 ||
					i >= NUM_CLAN_RANKS) {
					send_to_char(ch,
						"top rank must be a number between 0 and %d.\r\n",
						NUM_CLAN_RANKS - 1);
					return;
				}
				clan->top_rank = i;
				// Free memory from strings taken by newly invalid rank names
				i += 1;
				while (i < NUM_CLAN_RANKS) {
					if (clan->ranknames[i]) {
						free(clan->ranknames[i]);
						clan->ranknames[i] = NULL;
					}
					i++;
				}

				for (member = clan->member_list; member; member = member->next)
					if (member->rank > clan->top_rank)
						member->rank = clan->top_rank;
				sql_exec("update clan_members set rank=%d where clan=%d and rank > %d",
					clan->top_rank, clan->number, clan->top_rank);
				sql_exec("delete from clan_ranks where clan=%d and rank > %d",
					clan->number, clan->top_rank);

				slog("(cedit) %s set clan %d top to %d.", GET_NAME(ch),
					clan->number, clan->top_rank);
				send_to_char(ch, "Top rank of clan set.\r\n");

				return;
			}

			if (!is_number(arg3) || (i = atoi(arg3)) < 0 || i > clan->top_rank) {
				send_to_char(ch, "[rank] must be a number between 0 and %d.\r\n",
					clan->top_rank);
				return;
			}
			if (!*argument) {
				send_to_char(ch, "Set that rank to what name?\r\n");
				return;
			}
			if (strlen(argument) >= MAX_CLAN_RANKNAME) {
				send_to_char(ch, "Rank names may not exceed %d characters.\r\n",
					MAX_CLAN_RANKNAME - 1);
				return;
			}
			if (clan->ranknames[i]) {
				free(clan->ranknames[i]);
				sql_exec("update clan_ranks set title='%s' where clan=%d and rank=%d",
					tmp_sqlescape(argument), clan->number, i);
			} else {
				sql_exec("insert into clan_ranks (clan, rank, title) values (%d, %d, '%s')",
					clan->number, i, tmp_sqlescape(argument));
			}
			clan->ranknames[i] = strdup(argument);

			send_to_char(ch, "Rank title set.\r\n");
			slog("(cedit) %s set clan %d rank %d to '%s'.",
				GET_NAME(ch), clan->number, i, clan->ranknames[i]);

			return;

		}
		// cedit set bank
		else if (is_abbrev(arg2, "bank")) {
			if (!*argument) {
				send_to_char(ch, "Set the bank account of the clan to what?\r\n");
				return;
			}
			if( !is_number(argument) ) {
				send_to_char(ch,
					"Try setting the bank account to an appropriate number asswipe.\r\n");
				return;
			}
			money = atoll(argument);
			if( money < 0 ) {
				send_to_char(ch,
					"This clan has no overdraft protection. Negative value invalid.\r\n");
				return;
			}
			slog("(cedit) %s set clan %d bank from %lld to %lld.", GET_NAME(ch),
				clan->number, clan->bank_account, money );
			send_to_char(ch, "Clan bank account set from %lld to %lld\r\n",
					clan->bank_account, money );
			clan->bank_account = money;
			sql_exec("update clans set bank=%lld where idnum=%d",
				money, clan->number);

			return;

		}
		// cedit set owner
		else if (is_abbrev(arg2, "owner")) {
			if (!*argument) {
				send_to_char(ch, "Set the owner of the clan to what?\r\n");
				return;
			}
            i = atoi(argument);

			if (i == 0 && (i = player_idnum_from_name(argument)) == 0) {
				send_to_char(ch, "No such person.\r\n");
				return;
			}
			clan->owner = i;
			send_to_char(ch, "Clan owner set.\r\n");
			slog("(cedit) %s set clan %d owner to %s.", GET_NAME(ch),
				clan->number, argument);
			sql_exec("update clans set owner=%d where idnum=%d",
				i, clan->number);
			return;
		}
		// cedit set member
		else if (is_abbrev(arg2, "member")) {
			if (!*argument) {
				send_to_char(ch,
					"Usage: cedit set <clan> member <member> <rank>\r\n");
				return;
			}
			arg3 = tmp_getword(&argument);
			arg1 = tmp_getword(&argument);

			if (!is_number(arg3)) {
				if ((i = player_idnum_from_name(arg3)) == 0) {
					send_to_char(ch, "There is no such player in existence...\r\n");
					return;
				}
			} else if ((i = atoi(arg3)) < 0) {
				send_to_char(ch, "Such a comedian.\r\n");
				return;
			}

			if (!*arg1) {
				send_to_char(ch,
					"Usage: cedit set <clan> member <member> <rank>\r\n");
				return;
			}

			j = atoi(arg1);

			sql_exec("update clan_members set rank=%d where player=%d",
				j, i);
			for (member = clan->member_list; member; member = member->next)
				if (member->idnum == i) {
					member->rank = j;
					send_to_char(ch, "Member rank set.\r\n");
					slog("(cedit) %s set clan %d member %d rank to %d.",
						GET_NAME(ch), clan->number, i, member->rank);
					break;
				}

			if (!member)
				send_to_char(ch, "Unable to find that member.\r\n");
			return;

		} else {
			send_to_char(ch, "Unknown option '%s'.\r\n", arg2);
			return;
		}

		send_to_char(ch, "Successful.\r\n");
		break;

	case 3:			  /*** show   ***/

		if (!clan && *arg1) {
			send_to_char(ch, "Clan '%s' does not exist.\r\n", arg1);
			return;
		}
		do_show_clan(ch, clan);
		break;

	case 4:			  /*** add    ***/
		if (!*arg1) {
			send_to_char(ch, "Usage: cedit add <clan> <room|member> <number>\r\n");
			return;
		}
		arg2 = tmp_getword(&argument);
		arg3 = tmp_getword(&argument);

		if (!*arg3 || !*arg2) {
			send_to_char(ch, "Usage: cedit add <clan> <room|member> <number>\r\n");
			return;
		}
		if (!clan) {
			send_to_char(ch, "Clan '%s' does not exist.\r\n", arg1);
			return;
		}
		// cedit add room
		if (is_abbrev(arg2, "room")) {
			if (!(room = real_room(atoi(arg3)))) {
				send_to_char(ch, "No room with number %s exists.\r\n", arg3);
				return;
			}
			for (rm_list = clan->room_list; rm_list; rm_list = rm_list->next)
				if (rm_list->room == room) {
					send_to_char(ch,
						"This room is already a part of the clan, dumbass.\r\n");
					return;
				}

			CREATE(rm_list, struct room_list_elem, 1);
			rm_list->room = room;
			rm_list->next = clan->room_list;
			clan->room_list = rm_list;
			send_to_char(ch, "Room added.\r\n");

			slog("(cedit) %s added room %d to clan %d.", GET_NAME(ch),
				room->number, clan->number);
			sql_exec("insert into clan_rooms (clan, room) values (%d, %d)",
				clan->number, room->number);

			return;
		}
		// cedit add member
		else if (is_abbrev(arg2, "member")) {
			if (!is_number(arg3)) {
				if ((i = player_idnum_from_name(arg3)) == 0) {
					send_to_char(ch, "There exists no player with that name.\r\n");
					return;
				}
			} else if ((i = atoi(arg3)) < 0) {
				send_to_char(ch, "Real funny... reeeeeaaal funny.\r\n");
				return;
			}
			for (member = clan->member_list; member; member = member->next) {
				if (member->idnum == i) {
					send_to_char(ch,
						"That player is already on the member list.\r\n");
					return;
				}
			}
			CREATE(member, struct clanmember_data, 1);

			member->idnum = i;
			member->rank = 0;
			member->next = clan->member_list;
			clan->member_list = member;

			send_to_char(ch, "Clan member added to list.\r\n");;

			slog("(cedit) %s added member %ld to clan %d.",
				GET_NAME(ch), member->idnum, clan->number);
			sql_exec("insert into clan_members (clan, player, rank, no_mail) values (%d, %d, 0, 'f')",
				clan->number, i);

			return;
		} else {
			send_to_char(ch, "Invalid command, punk.\r\n");
		}
		break;
	case 5:			  /** remove ***/

		if (!*arg1) {
			send_to_char(ch,
				"Usage: cedit remove <clan> <room|member> <number>\r\n");
			return;
		}
		arg2 = tmp_getword(&argument);
		arg3 = tmp_getword(&argument);

		if (!*arg3 || !*arg2) {
			send_to_char(ch,
				"Usage: cedit remove <clan> <room|member> <number>\r\n");
			return;
		}
		if (!clan) {
			send_to_char(ch, "Clan '%s' does not exist.\r\n", arg1);
			return;
		}

		if (is_abbrev(arg2, "room")) {
			if (!(room = real_room(atoi(arg3)))) {
				send_to_char(ch, "No room with number %s exists.\r\n", arg3);
			}
			for (rm_list = clan->room_list; rm_list; rm_list = rm_list->next)
				if (rm_list->room == room)
					break;

			if (!rm_list) {
				send_to_char(ch, "That room is not part of the list, dorkus.\r\n");
				return;
			}

			REMOVE_ROOM_FROM_CLAN(rm_list, clan);
			free(rm_list);
			send_to_char(ch,
				"Room removed and memory freed.  Thank you.. Call again.\r\n");

			slog("(cedit) %s removed room %d from clan %d.",
				GET_NAME(ch), room->number, clan->number);
			sql_exec("delete from clan_rooms where clan=%d and room=%d",
				clan->number, room->number);

			return;

		}
		// cedit remove member
		else if (is_abbrev(arg2, "member")) {
			if (!is_number(arg3)) {
				if ((i = player_idnum_from_name(arg3)) == 0) {
					send_to_char(ch, "There exists no player with that name.\r\n");
					return;
				}
			} else if ((i = atoi(arg3)) < 0) {
				send_to_char(ch, "Real funny... reeeeeaaal funny.\r\n");
				return;
			}
			for (member = clan->member_list; member; member = member->next)
				if (member->idnum == i)
					break;

			if (!member) {
				send_to_char(ch, "That player is not a part of this clan.\r\n");
				return;
			}

			REMOVE_MEMBER_FROM_CLAN(member, clan);
			free(member);
			send_to_char(ch, "Member removed from the sacred list.\r\n");

			slog("(cedit) %s removed member %d from clan %d.",
				GET_NAME(ch), i, clan->number);
			sql_exec("delete from clan_members where clan=%d and player=%d",
				clan->number, i);

			return;
		} else
			send_to_char(ch, "YO!  Remove members, or rooms... geez!\r\n");

		break;

	case 6: /** sort **/
		if (!clan) {
			if (!*arg1)
				send_to_char(ch, "Sort what clan?\r\n");
			else {
				send_to_char(ch, "Clan '%s' does not exist.\r\n", arg1);
			}
			return;
		}
		sort_clanmembers(clan);
		send_to_char(ch, "Clanmembers sorted.\r\n");
		break;

	default:
		send_to_char(ch, "Unknown cedit option.\r\n");
		break;
	}
}

bool
boot_clans(void)
{
	struct clan_data *clan, *tmp_clan;
	struct clanmember_data *member;
	struct room_list_elem *rm_list;
	struct room_data *room;
	PGresult *res;
	int idx, count, num;

	slog("Reading clans");

	res = sql_query("select idnum, name, badge, bank, owner from clans");
	count = PQntuples(res);
	if (count == 0) {
		slog("WARNING: No clans loaded");
		return false;
	}

	for (idx = 0;idx < count;idx++) {
		CREATE(clan, struct clan_data, 1);

		clan->number = atoi(PQgetvalue(res, idx, 0));
		clan->name = strdup(PQgetvalue(res, idx, 1));
		clan->badge = strdup(PQgetvalue(res, idx, 2));
		clan->bank_account = atoll(PQgetvalue(res, idx, 3));
		clan->owner = atoi(PQgetvalue(res, idx, 4));
		clan->member_list = NULL;
		clan->room_list = NULL;
		clan->next = NULL;

		if (!clan_list)
			clan_list = clan;
		else {
			for (tmp_clan = clan_list; tmp_clan; tmp_clan = tmp_clan->next)
				if (!tmp_clan->next) {
					tmp_clan->next = clan;
					break;
				}
		}
	}

	// Add the ranks to the clan
	res = sql_query("select clan, rank, title from clan_ranks");
	count = PQntuples(res);
	for (idx = 0;idx < count;idx++) {
		clan = real_clan(atol(PQgetvalue(res, idx, 0)));
		num = atoi(PQgetvalue(res, idx, 1));
		clan->top_rank = MAX(clan->top_rank, num);
		clan->ranknames[num] = strdup(PQgetvalue(res, idx, 2));
	}

	// Now add all the members to the clans
	res = sql_query("select clan, player, rank, no_mail from clan_members order by rank");
	count = PQntuples(res);
	for (idx = 0;idx < count;idx++) {
		CREATE(member, struct clanmember_data, 1);
		member->idnum = atol(PQgetvalue(res, idx, 1));
		member->rank = atol(PQgetvalue(res, idx, 2));
		member->no_mail = !strcmp(PQgetvalue(res, idx, 3), "t");
		member->next = NULL;

		clan = real_clan(atol(PQgetvalue(res, idx, 0)));
        if (!clan->member_list)
			clan->member_list = member;
		else {
            member->next = clan->member_list;
            clan->member_list = member;
        }
	}

	// Add rooms to the clans
	res = sql_query("select clan, room from clan_rooms");
	count = PQntuples(res);
	for (idx = 0;idx < count;idx++) {
		clan = real_clan(atol(PQgetvalue(res, idx, 0)));
		room = real_room(atoi(PQgetvalue(res, idx, 1)));
		if (room) {
			CREATE(rm_list, struct room_list_elem, 1);
			rm_list->room = room;
			rm_list->next = clan->room_list;
			clan->room_list = rm_list;
		}
	}

	slog("Clan boot successful.");
	return true;
}

struct clan_data *
create_clan(int vnum)
{

	struct clan_data *newclan = NULL, *clan = NULL;
	int i;

	CREATE(newclan, struct clan_data, 1);

	newclan->number = vnum;
	newclan->bank_account = 0;
	newclan->top_rank = 0;
	newclan->name = strdup("New");
	newclan->badge = strdup("(//NEW\\\\)");
	for (i = 0; i < NUM_CLAN_RANKS; i++)
		newclan->ranknames[i] = NULL;

	newclan->member_list = NULL;
	newclan->room_list = NULL;
	newclan->next = NULL;

	if (!clan_list || newclan->number < clan_list->number) {
		newclan->next = clan_list;
		clan_list = newclan;
	} else {
		for (clan = clan_list; clan; clan = clan->next)
			if (!clan->next ||
				(clan->number < vnum && clan->next->number > vnum)) {
				newclan->next = clan->next;
				clan->next = newclan;
				break;
			}
	}

	sql_exec("insert into clans (idnum, name, badge, bank) values (%d, 'New', '(//NEW\\\\)', 0)", newclan->number);
	return (newclan);
}

int
delete_clan(struct clan_data *clan)
{

	struct clan_data *tmp_clan = NULL;
	struct room_list_elem *rm_list = NULL;
	struct clanmember_data *member = NULL;
	int i;

	if (clan_list == clan) {
		clan_list = clan->next;
	} else {
		for (tmp_clan = clan_list; tmp_clan; tmp_clan = tmp_clan->next)
			if (tmp_clan->next && tmp_clan->next == clan) {
				tmp_clan->next = clan->next;
				break;
			}

		if (!tmp_clan)
			return 1;
	}

	if (clan->name) {
		free(clan->name);
	}
	if (clan->badge) {
		free(clan->badge);
	}
	for (i = 0; i < NUM_CLAN_RANKS; i++)
		if (clan->ranknames[i]) {
			free(clan->ranknames[i]);
		}

	for (member = clan->member_list; member; member = clan->member_list) {
		clan->member_list = member->next;
		free(member);
	}

	for (rm_list = clan->room_list; rm_list; rm_list = clan->room_list) {
		clan->room_list = rm_list->next;
		free(rm_list);
	}
	sql_exec("delete from clan_rooms where clan=%d", clan->number);
	sql_exec("delete from clan_members where clan=%d", clan->number);
	sql_exec("delete from clan_ranks where clan=%d", clan->number);
	sql_exec("delete from clans where idnum=%d", clan->number);

    struct creature *tch;

    for (tch = creatures;tch;tch = tch->next) {
		if (IS_PC(tch) && GET_CLAN(tch) == clan->number) {
			GET_CLAN(tch) = 0;
			send_to_char(tch, "Your clan has been disbanded!\r\n");
		}
	}

	free(clan);

	return 0;
}

void
do_show_clan(struct creature *ch, struct clan_data *clan)
{
	struct clanmember_data *member = NULL;
	struct room_list_elem *rm_list = NULL;
	int i, num_rooms = 0, num_members = 0;

	acc_string_clear();
	if (clan) {
		acc_sprintf(
			"CLAN %d - Name: %s%s%s, Badge: %s%s%s, Top Rank: %d\r\n",
			clan->number, CCCYN(ch, C_NRM), clan->name, CCNRM(ch, C_NRM),
			CCCYN(ch, C_NRM), clan->badge, CCNRM(ch, C_NRM), clan->top_rank);

        acc_sprintf("Bank: %-20lld Owner: %s[%ld]\r\n",
                    clan->bank_account,
                    player_name_from_idnum(clan->owner),
                    clan->owner);

		for (i = clan->top_rank; i >= 0; i--) {
			acc_sprintf("Rank %2d: %s%s%s\r\n", i,
                        CCYEL(ch, C_NRM),
                        clan_rankname(clan, i),
                        CCNRM(ch, C_NRM));
		}

		acc_strcat("ROOMS:\r\n",NULL);

		for (rm_list = clan->room_list, num_rooms = 0;
             rm_list; rm_list = rm_list->next) {
			num_rooms++;
			acc_sprintf("%3d) %5d.  %s%s%s\r\n", num_rooms,
                        rm_list->room->number,
                        CCCYN(ch, C_NRM), rm_list->room->name, CCNRM(ch, C_NRM));
		}
		if (!num_rooms)
			acc_strcat("None.\r\n",NULL);

        num_members = clan_member_count(clan);
        if (num_members) {
            acc_sprintf("%d MEMBERS:\r\n", clan_member_count(clan));

            for (member = clan->member_list;member;member = member->next) {
                int acct_id = player_account_by_idnum(member->idnum);
                acc_sprintf("%-50s %s[%d]\r\n",
                            tmp_sprintf("%5ld %s%s%s %s(%d)",
                                        member->idnum,
                                        CCYEL(ch, C_NRM),
                                        player_name_by_idnum(member->idnum),
                                        CCNRM(ch, C_NRM),
                                        clan_rankname(clan, member->rank),
                                        member->rank),
                            account_by_id(acct_id)->name,
                            acct_id);
            }
        } else {
            acc_strcat("No members.\r\n",NULL);
        }
	} else {
		acc_strcat("CLANS:\r\n", NULL);
		for (clan = clan_list; clan; clan = clan->next) {

            member = clan->member_list;
            num_members = 0;
			while (member) {
                num_members++;
                member = member->next;
            }

			acc_sprintf(" %3d - %s%20s%s  %s%20s%s  (%3d members)\r\n",
                        clan->number,
                        CCCYN(ch, C_NRM), clan->name, CCNRM(ch, C_NRM),
                        CCCYN(ch, C_NRM), clan->badge, CCNRM(ch, C_NRM), num_members);
		}
	}
	page_string(ch->desc, acc_get_string());
}

int
clan_house_can_enter(struct creature *ch, struct room_data *room)
{
	struct clan_data *clan = NULL, *ch_clan = NULL;
	struct room_list_elem *rm_list = NULL;

	if (!ROOM_FLAGGED(room, ROOM_CLAN_HOUSE))
		return 1;
	if (GET_LEVEL(ch) >= LVL_DEMI)
		return 1;
	if (Security_isMember(ch, "Clan"))
		return 1;
	if (!(ch_clan = real_clan(GET_CLAN(ch))))
		return 0;

	for (clan = clan_list; clan; clan = clan->next) {
		if (clan == ch_clan)
			continue;
		for (rm_list = clan->room_list; rm_list; rm_list = rm_list->next)
			if (rm_list->room == room)
				return 0;
	}

	return 1;
}

void
sort_clanmembers(struct clan_data *clan)
{

	struct clanmember_data *new_list = NULL, *i = NULL, *j = NULL, *k = NULL;

	if (!clan->member_list)
		return;

	for (i = clan->member_list; i; i = j) {
		j = i->next;
		if (!new_list || (new_list->rank < i->rank)) {
			i->next = new_list;
			new_list = i;
		} else {
			k = new_list;
			while (k->next)
				if (k->next->rank < i->rank)
					break;
				else
					k = k->next;
			i->next = k->next;
			k->next = i;
		}
	}
	clan->member_list = new_list;
}