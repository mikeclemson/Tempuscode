//
// File: quest.c                     -- Part of TempusMUD
//
// Copyright 1998 by John Watson, all rights reserved.
//

//
//  File: quest.c -- Quest system for Tempus MUD
//  by Fireball, December 1997
//
//  Copyright 1998 John Watson
//

#ifdef HAS_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <fstream>
#include "xml_utils.h"

#include "structs.h"
#include "comm.h"
#include "utils.h"
#include "interpreter.h"
#include "db.h"
#include "editor.h"
#include "spells.h"
#include "quest.h"
#include "player_table.h"
#include "handler.h"
#include "screen.h"
#include "tmpstr.h"
#include "utils.h"
#include "security.h"
#include "player_table.h"
#include "accstr.h"

// external funcs here
ACMD(do_switch);
void do_qcontrol_help(struct creature *, char *);

// external vars here
extern struct descriptor_data *descriptor_list;

// internal vars here

const struct qcontrol_option {
	const char *keyword;
	const char *usage;
	int level;
} qc_options[] = {
	{"show", "[quest vnum]", LVL_AMBASSADOR},
	{"create", "<type> <name>", LVL_AMBASSADOR},
	{"end", "<quest vnum>", LVL_AMBASSADOR},
	{"add", "<name> <vnum>", LVL_AMBASSADOR},
	{"kick", "<player> <quest vnum>", LVL_AMBASSADOR},	// 5
	{"flags", "<quest vnum> <+/-> <flags>", LVL_AMBASSADOR},
	{"comment", "<quest vnum> <comments>", LVL_AMBASSADOR},
	{"describe", "<quest vnum>", LVL_AMBASSADOR},
	{"update", "<quest vnum>", LVL_AMBASSADOR},
	{"ban", "<player> <quest vnum>|\'all\'", LVL_AMBASSADOR},	// 10
	{"unban", "<player> <quest vnum>|\'all\'", LVL_AMBASSADOR},
	{"mute", "<player> <quest vnum>", LVL_AMBASSADOR},
	{"unmute", "<player> <quest vnum>", LVL_AMBASSADOR},
	{"level", "<quest vnum> <access level>", LVL_AMBASSADOR},
	{"minlev", "<quest vnum> <minlev>", LVL_AMBASSADOR},	// 15
	{"maxlev", "<quest vnum> <maxlev>", LVL_AMBASSADOR},
	{"mingen", "<quest vnum> <min generation>", LVL_AMBASSADOR},
	{"maxgen", "<quest vnum> <max generation>", LVL_AMBASSADOR},
	{"mload", "<mobile vnum> <vnum>", LVL_IMMORT},
	{"purge", "<quest vnum> <mobile name>", LVL_IMMORT},	// 20
	{"save", "", LVL_IMMORT },
	{"help", "<topic>", LVL_AMBASSADOR},
	{"switch", "<mobile name>", LVL_IMMORT},
	{"title", "<quest vnum> <title>", LVL_AMBASSADOR},
	{"oload", "<item num> <vnum>", LVL_AMBASSADOR},
	{"trans", "<quest vnum> [room number]", LVL_AMBASSADOR},
	{"award", "<quest vnum> <player> <pts> [comments]", LVL_AMBASSADOR},
	{"penalize", "<quest vnum> <player> <pts> [reason]", LVL_AMBASSADOR},
	{"restore", "<quest vnum>", LVL_AMBASSADOR},
    {"loadroom", "<quest vnum> <room number>", LVL_AMBASSADOR},
	{NULL, NULL, 0}				// list terminator
};

const char *qtypes[] = {
	"trivia",
	"scavenger",
	"hide-and-seek",
	"roleplay",
	"pkill",
	"award/payment",
	"misc",
	"\n"
};

const char *qtype_abbrevs[] = {
	"trivia",
	"scav",
	"h&s",
	"RP",
	"pkill",
	"A/P",
	"misc",
	"\n"
};

const char *quest_bits[] = {
	"REVIEWED",
	"NOWHO",
	"NO_OUTWHO",
	"NOJOIN",
	"NOLEAVE",
	"HIDE",
	"WHOWHERE",
	"ARENA",
	"\n"
};

const char *quest_bit_descs[] = {
	"This quest has been reviewed.",
	"The \'quest who\' command does not work in this quest.",
	"Players in this quest cannot use the \'who\' command.",
	"Players may not join this quest.",
	"Players may not leave this quest.",
	"Players cannot see this quest until this flag is removed.",
	"\'quest who\' will show the locations of other questers.",
	"Players will only die arena deaths.",
	"\n"
};

const char *qlog_types[] = {
	"off",
	"brief",
	"normal",
	"complete",
	"\n"
};

const char *qp_bits[] = {
	"DEAF",
	"MUTE",
	"\n"
};

struct QuestControl : protected vector<Quest> {
    QuestControl() : vector<Quest>() {
        filename = "etc/quest.xml";
        top_vnum = 0;
    }
    ~QuestControl() {
    }
    void loadQuests();
    void add( Quest &q ) { push_back(q); }
    void save();
    int getNextVnum() {
        return ++top_vnum;
    }
    void setNextVnum( int vnum ) {
        if( vnum >= top_vnum ) {
            top_vnum = vnum;
        }
    }
    using vector<Quest>_size;
    using vector<Quest>_operator[];
private:
    int top_vnum;
    const char *filename;
} quests;

void
QuestControl_loadQuests()
{
	erase(begin(),end());
	xmlDocPtr doc = xmlParseFile(filename);
	if (doc == NULL) {
		errlog("Quesst load FAILED.");
		return;
	}
	// discard root node
	xmlNodePtr cur = xmlDocGetRootElement(doc);
	if (cur == NULL) {
		xmlFreeDoc(doc);
		return;
	}

	cur = cur->xmlChildrenNode;
	// Load all the nodes in the file
	while (cur != NULL) {
		// But only question nodes
		if ((xmlMatches(cur->name, "Quest"))) {
			push_back(Quest(cur, doc));
		}
		cur = cur->next;
	}
	sort( begin(), end() );
	xmlFreeDoc(doc);
}

void
QuestControl_save()
{
	std_ofstream out(filename);

	if(!out) {
		errlog("Cannot open quest file: %s",filename);
		return;
	}
	out << "<Quests>" << endl;
	for( unsigned int i = 0; i < quests.size(); i++ ) {
		quests[i].save(out);
	}
	out << "</Quests>" << endl;
	out.flush();
	out.close();
}

char arg1[MAX_INPUT_LENGTH], arg2[MAX_INPUT_LENGTH];
FILE *qlogfile = NULL;

static void
do_qcontrol_options(struct creature *ch)
{
	int i = 0;

	strcpy(buf, "qcontrol options:\r\n");
	while (1) {
		if (!qc_options[i].keyword)
			break;
		sprintf(buf, "%s  %-15s %s\r\n", buf, qc_options[i].keyword,
                qc_options[i].usage);
		i++;
	}
	page_string(ch->desc, buf);
}

static void
do_qcontrol_usage(struct creature *ch, int com)
{
	if (com < 0)
		do_qcontrol_options(ch);
	else {
		send_to_char(ch, "Usage: qcontrol %s %s\r\n",
                     qc_options[com].keyword, qc_options[com].usage);
	}
}

void							//Load mobile.
do_qcontrol_mload(struct creature *ch, char *argument, int com)
{
	struct creature *mob;
	class Quest *quest = NULL;
	char arg1[MAX_INPUT_LENGTH];
	int number;

	argument = two_arguments(argument, buf, arg1);

	if (!*buf || !isdigit(*buf) || !*arg1 || !isdigit(*arg1)) {
		do_qcontrol_usage(ch, com);
		return;
	}

	if (!(quest = find_quest(ch, arg1))) {
		return;
	}
	if (!quest->canEdit(ch)) {
		return;
	}
	if (quest->getEnded() ) {
		send_to_char(ch, "Pay attention dummy! That quest is over!\r\n");
		return;
	}

	if ((number = atoi(buf)) < 0) {
		send_to_char(ch, "A NEGATIVE number??\r\n");
		return;
	}
	if (!real_mobile_proto(number)) {
		send_to_char(ch, "There is no mobile thang with that number.\r\n");
		return;
	}
	mob = read_mobile(number);
	char_to_room(mob, ch->in_room,false);
	act("$n makes a quaint, magical gesture with one hand.", true, ch,
		0, 0, TO_ROOM);
	act("$n has created $N!", false, ch, 0, mob, TO_ROOM);
	act("You create $N.", false, ch, 0, mob, TO_CHAR);

	sprintf(buf, "mloaded %s at %d.", GET_NAME(mob), ch->in_room->number);
	qlog(ch, buf, QLOG_BRIEF, MAX(GET_INVIS_LVL(ch), LVL_IMMORT), true);

}

void // Set loadroom for quest participants
do_qcontrol_loadroom(struct creature *ch, char *argument, int com)
{
	class Quest *quest = NULL;
	char arg1[MAX_INPUT_LENGTH];
	int number;

	argument = two_arguments(argument, buf, arg1);

	if (!*buf || !isdigit(*buf)) {
		do_qcontrol_usage(ch, com);
		return;
	}

	if (!(quest = find_quest(ch, buf))) {
		return;
	}
    if (!*arg1) {
        send_to_char(ch, "Current quest loadroom is (%d)\r\n", quest->loadroom);
        return;
    }
    if (!isdigit(*arg1)) {
        do_qcontrol_usage(ch, com);
        return;
    }
	if (!quest->canEdit(ch)) {
		return;
	}
	if (quest->getEnded() ) {
		send_to_char(ch, "Pay attention you dummy! That quest is over!\r\n");
		return;
	}

	if ((number = atoi(arg1)) < 0) {
		send_to_char(ch, "A NEGATIVE number??\r\n");
		return;
	}
    if (!real_room(number)) {
        send_to_char(ch, "That room doesn't exist!\r\n");
        return;
    }

    quest->loadroom = number;
    send_to_char(ch, "Okay, quest loadroom is now %d\r\n", number);

	sprintf(buf, "%s set quest loadroom to: (%d)",
            GET_NAME(ch), number);
	qlog(ch, buf, QLOG_BRIEF, MAX(GET_INVIS_LVL(ch), LVL_IMMORT), true);
}

static void
do_qcontrol_oload_list(struct creature * ch)
{
	int i = 0;
	struct obj_data *obj;

	send_to_char(ch, "Valid Quest Objects:\r\n");
	for (i = MIN_QUEST_OBJ_VNUM; i <= MAX_QUEST_OBJ_VNUM; i++) {
		if (!(obj = real_object_proto(i)))
			continue;
		if (IS_OBJ_STAT2(obj, ITEM2_UNAPPROVED))
			continue;
		send_to_char(ch, "    %s%d. %s%s %s: %d qps\r\n", CCNRM(ch, C_NRM),
                     i - MIN_QUEST_OBJ_VNUM, CCGRN(ch, C_NRM), obj->name,
                     CCNRM(ch, C_NRM), obj->shared->cost);
	}
}

// Load Quest Object
static void
do_qcontrol_oload(struct creature *ch, char *argument, int com)
{
	struct obj_data *obj;
	class Quest *quest = NULL;
	int number;
	char arg2[MAX_INPUT_LENGTH];

	argument = two_arguments(argument, buf, arg2);

	if (!*buf || !isdigit(*buf)) {
		do_qcontrol_oload_list(ch);
		do_qcontrol_usage(ch, com);
		return;
	}

	if (!(quest = find_quest(ch, arg2))) {
		return;
	}
	if (!quest->canEdit(ch)) {
		return;
	}
	if (quest->getEnded() ) {
		send_to_char(ch, "Pay attention you dummy! That quest is over!\r\n");
		return;
	}

	if ((number = atoi(buf)) < 0) {
		send_to_char(ch, "A NEGATIVE number??\r\n");
		return;
	}
	if (number > MAX_QUEST_OBJ_VNUM - MIN_QUEST_OBJ_VNUM) {
		send_to_char(ch, "Invalid item number.\r\n");
		do_qcontrol_oload_list(ch);
		return;
	}
	obj = read_object(number + MIN_QUEST_OBJ_VNUM);

	if (!obj) {
		send_to_char(ch, "Error, no object loaded\r\n");
		return;
	}
	if (obj->shared->cost < 0) {
		send_to_char(ch, "This object is messed up.\r\n");
		extract_obj(obj);
		return;
	}
	if (GET_LEVEL(ch) == LVL_AMBASSADOR && obj->shared->cost > 0) {
		send_to_char(ch, "You can only load objects with a 0 cost.\r\n");
		extract_obj(obj);
		return;
	}

	if (obj->shared->cost > GET_IMMORT_QP(ch)) {
		send_to_char(ch, "You do not have the required quest points.\r\n");
		extract_obj(obj);
		return;
	}

	GET_IMMORT_QP(ch) -= obj->shared->cost;
	obj_to_char(obj, ch);
	ch->crashSave();
	act("$n makes a quaint, magical gesture with one hand.", true, ch,
		0, 0, TO_ROOM);
	act("$n has created $p!", false, ch, obj, 0, TO_ROOM);
	act("You create $p.", false, ch, obj, 0, TO_CHAR);

	sprintf(buf, "loaded %s at %d.", obj->name,
            ch->in_room->number);
	qlog(ch, buf, QLOG_BRIEF, MAX(GET_INVIS_LVL(ch), LVL_IMMORT), true);

}

void							//Purge mobile.
do_qcontrol_trans(struct creature *ch, char *argument)
{

	struct creature *vict;
	Quest *quest = NULL;
	struct room_data *room = NULL;

	argument = two_arguments(argument, arg1, arg2);
	if (!*arg1) {
		send_to_char(ch, "Usage: qcontrol trans <quest number> [room number]\r\n");
		return;
	}
	if (!(quest = find_quest(ch, arg1))) {
		return;
	}
	if (!quest->canEdit(ch)) {
		return;
	}
	if (quest->getEnded() ) {
		send_to_char(ch, "Pay attention dummy! That quest is over!\r\n");
		return;
	}
	if( *arg2 ) {
		if(! is_number(arg2) ) {
			send_to_char(ch, "No such room: '%s'\r\n",arg2);
			return;
		}
		room = real_room(atoi(arg2));
		if( room == NULL ) {
			send_to_char(ch, "No such room: '%s'\r\n",arg2);
			return;
		}
	}

	if( room == NULL )
		room = ch->in_room;

	int transCount = 0;
	for (int i = 0; i < quest->getNumPlayers(); i++) {
		long id = quest->getPlayer(i).idnum;
		vict = get_char_in_world_by_idnum(id);
		if ( vict == NULL || vict == ch )
			continue;
		if ((GET_LEVEL(ch) < GET_LEVEL(vict)) ) {
			send_to_char(ch, "%s ignores your summons.\r\n", GET_NAME(vict));
			continue;
		}
		++transCount;
		act("$n disappears in a mushroom cloud.", false, vict, 0, 0, TO_ROOM);
		char_from_room(vict,false);
		char_to_room(vict, room,false);
		act("$n arrives from a puff of smoke.", false, vict, 0, 0, TO_ROOM);
		act("$n has transferred you!", false, ch, 0, vict, TO_VICT);
		look_at_room(vict, room, 0);
	}
	char *buf = tmp_sprintf("has transferred %d questers to %s[%d] for quest %d.",
		 					transCount, room->name,room->number, quest->getVnum());
	qlog(ch, buf, QLOG_BRIEF, MAX(GET_INVIS_LVL(ch), LVL_IMMORT), true);

	send_to_char(ch,"%d players transferred.\r\n",transCount);
}

void							//Purge mobile.
do_qcontrol_purge(struct creature *ch, char *argument)
{

	struct creature *vict;
	class Quest *quest = NULL;
	char arg1[MAX_INPUT_LENGTH];

	argument = two_arguments(argument, arg1, buf);
	if (!*buf) {
		send_to_char(ch, "Purge what?\r\n");
		return;
	}
	if (!(quest = find_quest(ch, arg1))) {
		return;
	}
	if (!quest->canEdit(ch)) {
		return;
	}
	if (quest->getEnded() ) {
		send_to_char(ch, "Pay attention dummy! That quest is over!\r\n");
		return;
	}

	if ((vict = get_char_room_vis(ch, buf))) {
		if (!IS_NPC(vict)) {
			send_to_char(ch, "You don't need a quest to purge them!\r\n");
			return;
		}
		act("$n disintegrates $N.", false, ch, 0, vict, TO_NOTVICT);
		sprintf(buf, "has purged %s at %d.",
                GET_NAME(vict), vict->in_room->number);
		qlog(ch, buf, QLOG_BRIEF, MAX(GET_INVIS_LVL(ch), LVL_IMMORT), true);
		if (vict->desc) {
            set_desc_state(CXN_DISCONNECT, vict->desc);
			vict->desc = NULL;
		}
		vict->purge(false);
		send_to_char(ch, "%s", OK);
	} else {
		send_to_char(ch, "Purge what?\r\n");
		return;
	}

}

const char *
list_inactive_quests(void)
{
	int timediff;
	char timestr_a[128];
	int questCount = 0;
	const char *msg =
        "Finished Quests:\r\n"
        "-Vnum--Owner-------Type------Name----------------------Age------Players\r\n";

	if (!quests.size())
		return "There are no finished quests.\r\n";

	for (int i = quests.size() - 1; i >= 0; --i) {
		Quest *quest = &(quests[i]);
		if (!quest->getEnded())
			continue;
		questCount++;
		timediff = quest->getEnded() - quest->getStarted();
		snprintf(timestr_a, 127, "%02d:%02d", timediff / 3600, (timediff / 60) % 60);

		msg = tmp_sprintf( "%s %3d  %-10s  %-8s  %-24s %6s    %d\r\n", msg,
                           quest->getVnum(), playerIndex.getName(quest->getOwner()),
                           qtype_abbrevs[(int)quest->type], quest->name, timestr_a,
                           quest->getNumPlayers());
	}

	if (!questCount)
		return "There are no finished quests.\r\n";

	return tmp_sprintf("%s%d visible quest%s finished.\r\n", msg,
                       questCount, questCount == 1 ? "" : "s");
}

void
list_quest_bans(struct creature *ch, Quest * quest)
{
    const char *name;
	int i, num;

    acc_string_clear();
    acc_strcat("  -Banned Players------------------------------------\r\n", NULL);

	for (i = num = 0; i < quest->getNumBans(); i++) {
		name = playerIndex.getName(quest->getBan(i).idnum);
		if (!name) {
			acc_sprintf("BOGUS player idnum %ld!\r\n", quest->getBan(i).idnum);
			errlog("bogus player idnum %ld in list_quest_bans.",
                   quest->getBan(i).idnum);
			break;
		}

		acc_sprintf("  %2d. %-20s\r\n", ++num, name);
	}

    page_string(ch->desc, acc_get_string());
}

static void
do_qcontrol_show(struct creature *ch, char *argument)
{

	int timediff;
	Quest *quest = NULL;
	char *timestr_e, *timestr_s;
	char timestr_a[16];

	if (!quests.size()) {
		send_to_char(ch, "There are no quests to show.\r\n");
		return;
	}

	// show all quests
	if (!*argument) {
		const char *msg;

		msg = list_active_quests(ch);
		msg = tmp_strcat(msg, list_inactive_quests());
		page_string(ch->desc, msg);
		return;
	}

	// list q specific quest
	if (!(quest = find_quest(ch, argument)))
		return;

	time_t started = quest->getStarted();
	timestr_s = asctime(localtime(&started));
	*(timestr_s + strlen(timestr_s) - 1) = '\0';

	// quest is over, show summary information
	if (quest->getEnded() ) {

		timediff = quest->getEnded() - quest->getStarted();
		sprintf(timestr_a, "%02d:%02d", timediff / 3600, (timediff / 60) % 60);

		time_t started = quest->getStarted();
		timestr_e = asctime(localtime(&started));
		*(timestr_e + strlen(timestr_e) - 1) = '\0';

        acc_string_clear();

        acc_sprintf("Owner:  %-30s [%2d]\r\n"
                    "Name:   %s\r\n"
                    "Status: COMPLETED\r\n"
                    "Description:\r\n%s"
                    "  Type:           %s\r\n"
                    "  Started:        %s\r\n"
                    "  Ended:          %s\r\n"
                    "  Age:            %s\r\n"
                    "  Min Level:   Gen %-2d, Level %2d\r\n"
                    "  Max Level:   Gen %-2d, Level %2d\r\n"
                    "  Max Players:    %d\r\n"
                    "  Pts. Awarded:   %d\r\n",
                    playerIndex.getName(quest->getOwner()), quest->owner_level,
                    quest->name,
                    quest->description ? quest->description : "None.\r\n",
                    qtypes[(int)quest->type], timestr_s,
                    timestr_e, timestr_a,
                    quest->mingen , quest->minlevel,
                    quest->maxgen , quest->maxlevel,
                    quest->getMaxPlayers(), quest->getAwarded());

		page_string(ch->desc, acc_get_string());

		return;
	}
	// quest is still active

	timediff = time(0) - quest->getStarted();
	sprintf(timestr_a, "%02d:%02d", timediff / 3600, (timediff / 60) % 60);

	acc_sprintf("Owner:  %-30s [%2d]\r\n"
                "Name:   %s\r\n"
                "Status: ACTIVE\r\n"
                "Description:\r\n%s"
                "Updates:\r\n%s"
                "  Type:            %s\r\n"
                "  Flags:           %s\r\n"
                "  Started:         %s\r\n"
                "  Age:             %s\r\n"
                "  Min Level:   Gen %-2d, Level %2d\r\n"
                "  Max Level:   Gen %-2d, Level %2d\r\n"
                "  Num Players:     %d\r\n"
                "  Max Players:     %d\r\n"
                "  Pts. Awarded:    %d\r\n",
                playerIndex.getName(quest->getOwner()), quest->owner_level,
                quest->name,
                quest->description ? quest->description : "None.\r\n",
                quest->updates ? quest->updates : "None.\r\n",
                qtypes[(int)quest->type],
                tmp_printbits(quest->flags, quest_bits),
                timestr_s,
                timestr_a,
                quest->mingen , quest->minlevel,
                quest->maxgen , quest->maxlevel,
                quest->getNumPlayers(),
                quest->getMaxPlayers(),
                quest->getAwarded());

	if (quest->getNumPlayers()) {
		list_quest_players(ch, quest, buf2);
		acc_strcat(buf2, NULL);
	}

	if (quest->getNumBans()) {
		list_quest_bans(ch, quest);
		acc_strcat(buf2, NULL);
	}

	page_string(ch->desc, acc_get_string());

}

int
find_quest_type(char *argument)
{
	int i = 0;

	while (1) {
		if (*qtypes[i] == '\n')
			break;
		if (is_abbrev(argument, qtypes[i]))
			return i;
		i++;
	}
	return (-1);
}

void
qcontrol_show_valid_types( struct creature *ch ) {
	char *msg = tmp_sprintf("  Valid Types:\r\n");
	int i = 0;
	while (1) {
		if (*qtypes[i] == '\n')
			break;
		char *line = tmp_sprintf("    %2d. %s\r\n", i, qtypes[i]);
		msg = tmp_strcat(msg,line);
		i++;
	}
	page_string(ch->desc, msg);
	return;
}

static void
do_qcontrol_create(struct creature *ch, char *argument, int com)
{
	int type;
	argument = one_argument(argument, arg1);
	skip_spaces(&argument);

	if (!*arg1 || !*argument) {
		do_qcontrol_usage(ch, com);
		qcontrol_show_valid_types(ch);
		return;
	}

	if ((type = find_quest_type(arg1)) < 0) {
		send_to_char(ch, "Invalid quest type '%s'.\r\n",arg1);
		qcontrol_show_valid_types(ch);
		return;
	}

	if (strlen(argument) >= MAX_QUEST_NAME) {
		send_to_char(ch, "Quest name too long.  Max length %d characters.\r\n",
                     MAX_QUEST_NAME - 1);
		return;
	}

	Quest quest(ch,type,argument);
	quests.add(quest);

	char *msg = tmp_sprintf( "created quest [%d] type %s, '%s'",
							 quest.getVnum(), qtypes[type], argument );
	qlog(ch, msg, QLOG_BRIEF, LVL_AMBASSADOR, true);
	send_to_char(ch, "Quest %d created.\r\n", quest.getVnum());
	if (!quest_by_vnum(quest.getVnum())->addPlayer(GET_IDNUM(ch)) ) {
		send_to_char(ch, "Error adding you to quest.\r\n");
		return;
	}
	GET_QUEST(ch) = quest.getVnum();
	save_player_to_xml(ch);
	save_quests();
}

char *
compose_qcomm_string(struct creature *ch, struct creature *vict, Quest * quest, int mode, const char *str)
{
	if (mode == QCOMM_SAY && ch) {
		if (ch == vict) {
			return tmp_sprintf("%s %2d] You quest-say,%s '%s'\r\n",
                               CCYEL_BLD(vict, C_NRM), quest->getVnum(),
                               CCNRM(vict, C_NRM), str);
		} else {
			return tmp_sprintf("%s %2d] %s quest-says,%s '%s'\r\n",
                               CCYEL_BLD(vict, C_NRM), quest->getVnum(),
                               PERS(ch, vict), CCNRM(vict, C_NRM), str);
		}
	} else {// quest echo
		return tmp_sprintf("%s %2d] %s%s\r\n",
                           CCYEL_BLD(vict, C_NRM), quest->getVnum(),
                           str, CCNRM(vict, C_NRM));
	}
}

void
send_to_quest(struct creature *ch, const char *str, Quest * quest, int level, int mode)
{
	struct creature *vict = NULL;
	int i;

	for (i = 0; i < quest->getNumPlayers(); i++) {
		if (quest->getPlayer(i).isFlagged(QP_IGNORE) && (level < LVL_AMBASSADOR))
			continue;

		if ((vict = get_char_in_world_by_idnum(quest->getPlayer(i).idnum))) {
			if (!PLR_FLAGGED(vict, PLR_MAILING | PLR_WRITING | PLR_OLC) &&
                vict->desc && GET_LEVEL(vict) >= level)
			{
				send_to_char(vict, "%s", compose_qcomm_string(ch, vict, quest, mode, str) );
			}
		}
	}
}

static void
do_qcontrol_end(struct creature *ch, char *argument, int com)
{
	Quest *quest = NULL;

	if (!*argument) {
		do_qcontrol_usage(ch, com);
		return;
	}

	if (!(quest = find_quest(ch, argument)))
		return;

	if (quest->getEnded() ) {
		send_to_char(ch, "That quest has already ended... duh.\r\n");
		return;
	}

	send_to_quest(ch, "Quest ending...", quest, 0, QCOMM_ECHO);

	qlog(ch, "Purging players from quest...", QLOG_COMP, 0, true);

	while (quest->getNumPlayers()) {
		if (!quest->removePlayer(quest->getPlayer((int)0).idnum)) {
			send_to_char(ch, "Error removing char from quest.\r\n");
			break;
		}

	}

	quest->setEnded(time(0));
	sprintf(buf, "ended quest %d '%s'", quest->getVnum(),quest->name);
	qlog(ch, buf, QLOG_BRIEF, 0, true);
	send_to_char(ch, "Quest ended.\r\n");
	save_quests();
}

static void
do_qcontrol_add(struct creature *ch, char *argument, int com)
{
	Quest *quest = NULL;
	struct creature *vict = NULL;

	argument = two_arguments(argument, arg1, arg2);

	if (!*arg1 || !*arg2) {
		do_qcontrol_usage(ch, com);
		return;
	}

	if (!(quest = find_quest(ch, arg2)))
		return;

	if (!(vict = check_char_vis(ch, arg1)))
		return;

	if (IS_NPC(vict)) {
		send_to_char(ch, "You cannot add mobiles to quests.\r\n");
		return;
	}

	if (quest->getEnded() ) {
		send_to_char(ch, "That quest has already ended, you wacko.\r\n");
		return;
	}

	if( quest->isPlaying(GET_IDNUM(vict) ) ) {
		send_to_char(ch, "That person is already part of this quest.\r\n");
		return;
	}

	if (GET_LEVEL(vict) >= GET_LEVEL(ch) && vict != ch) {
		send_to_char(ch, "You are not powerful enough to do this.\r\n");
		return;
	}

	if (!quest->canEdit(ch))
		return;

	if ( !quest->addPlayer(GET_IDNUM(vict)) ) {
		send_to_char(ch, "Error adding char to quest.\r\n");
		return;
	}
	GET_QUEST(vict) = quest->getVnum();

	sprintf(buf, "added %s to quest %d '%s'.",
			GET_NAME(vict), quest->getVnum(),quest->name);
	qlog(ch, buf, QLOG_COMP, GET_INVIS_LVL(vict), true);

	send_to_char(ch, "%s added to quest %d.\r\n", GET_NAME(vict), quest->getVnum());

	send_to_char(vict, "%s has added you to quest %d.\r\n", GET_NAME(ch),
                 quest->getVnum());

	sprintf(buf, "%s is now part of the quest.", GET_NAME(vict));
	send_to_quest(NULL, buf, quest, MAX(GET_INVIS_LVL(vict), LVL_AMBASSADOR),
                  QCOMM_ECHO);
}

static void
do_qcontrol_kick(struct creature *ch, char *argument, int com)
{

	Quest *quest = NULL;
	struct creature *vict = NULL;
    int idnum;
    const char *vict_name;
	int pid;
	int level = 0;
    char *arg1, *arg2;

    arg1 = tmp_getword(&argument);
    arg2 = tmp_getword(&argument);

	if (!*arg1 || !*arg2) {
		do_qcontrol_usage(ch, com);
		return;
	}

	if (!(quest = find_quest(ch, arg2)))
		return;

	if ((idnum = playerIndex.getID(arg1)) < 0) {
		send_to_char(ch, "There is no character named '%s'\r\n", arg1);
		return;
	}

	if (quest->getEnded() ) {
		send_to_char(ch,
                     "That quest has already ended.. there are no players in it.\r\n");
		return;
	}

	if (!quest->canEdit(ch))
		return;

	if(! quest->isPlaying(idnum) ) {
		send_to_char(ch, "That person not participating in this quest.\r\n");
		return;
	}

	if (!(vict = get_char_in_world_by_idnum(idnum))) {
		// load the char from file
		vict = new struct creature(true);
		pid = playerIndex.getID(arg1);
		if (pid > 0) {
			vict->loadFromXML(pid);
			level = GET_LEVEL(vict);
            vict_name = tmp_strdup(GET_NAME(vict));
			delete vict;
			vict = NULL;
		} else {
            delete vict;
			send_to_char(ch, "Error loading char from file.\r\n");
			return;
		}

	} else {
		level = GET_LEVEL(vict);
        vict_name = tmp_strdup(GET_NAME(vict));
	}

	if (level >= GET_LEVEL(ch) && vict && ch != vict) {
		send_to_char(ch, "You are not powerful enough to do this.\r\n");
		return;
	}

	if (!quest->removePlayer(idnum)) {
		send_to_char(ch, "Error removing char from quest.\r\n");
		return;
	}

	send_to_char(ch, "%s kicked from quest %d.\r\n",
                 vict_name, quest->getVnum());
	if (vict) {
		sprintf(buf, "kicked %s from quest %d '%s'.",
				vict_name, quest->getVnum(),quest->name);
		qlog(ch, buf, QLOG_BRIEF, MAX(GET_INVIS_LVL(vict), LVL_AMBASSADOR),
             true);

		send_to_char(vict, "%s kicked you from quest %d.\r\n",
                     GET_NAME(ch), quest->getVnum());

		sprintf(buf, "%s has been kicked from the quest.", vict_name);
		send_to_quest(NULL, buf, quest, MAX(GET_INVIS_LVL(vict),
                                            LVL_AMBASSADOR), QCOMM_ECHO);
	} else {
		sprintf(buf, "kicked %s from quest %d '%s'.",
				vict_name, quest->getVnum(),quest->name);
		qlog(ch, buf, QLOG_BRIEF, LVL_AMBASSADOR, true);

		sprintf(buf, "%s has been kicked from the quest.",
                vict_name);
		send_to_quest(NULL, buf, quest, LVL_AMBASSADOR, QCOMM_ECHO);
	}

	save_quests();
}

void
qcontrol_show_valid_flags( struct creature *ch ) {

	char *msg = tmp_sprintf("  Valid Quest Flags:\r\n");
	int i = 0;
	while (1) {
		if (*quest_bits[i] == '\n')
			break;
		char *line = tmp_sprintf("    %2d. %s - %s\r\n",
								 i,
								 quest_bits[i],
								 quest_bit_descs[i]);
		msg = tmp_strcat(msg,line);
		i++;
	}
	page_string(ch->desc, msg);
	return;
}
static void
do_qcontrol_flags(struct creature *ch, char *argument, int com)
{
	Quest *quest = NULL;
	int state, cur_flags = 0, tmp_flags = 0, flag = 0, old_flags = 0;

	argument = two_arguments(argument, arg1, arg2);

	if (!*arg1 || !*arg2 || !*argument) {
		do_qcontrol_usage(ch, com);
		qcontrol_show_valid_flags(ch);
		return;
	}

	if (!(quest = find_quest(ch, arg1)))
		return;

	if (!quest->canEdit(ch))
		return;

	if (*arg2 == '+')
		state = 1;
	else if (*arg2 == '-')
		state = 2;
	else {
		do_qcontrol_usage(ch, com);
		qcontrol_show_valid_flags(ch);
		return;
	}

	argument = one_argument(argument, arg1);

	old_flags = cur_flags = quest->flags;

	while (*arg1) {
		if ((flag = search_block(arg1, quest_bits, false)) == -1) {
			send_to_char(ch, "Invalid flag %s, skipping...\r\n", arg1);
		} else
			tmp_flags = tmp_flags | (1 << flag);

		argument = one_argument(argument, arg1);
	}

	if (state == 1)
		cur_flags = cur_flags | tmp_flags;
	else {
		tmp_flags = cur_flags & tmp_flags;
		cur_flags = cur_flags ^ tmp_flags;
	}

	quest->flags = cur_flags;

	tmp_flags = old_flags ^ cur_flags;
	sprintbit(tmp_flags, quest_bits, buf2);

	if (tmp_flags == 0) {
		send_to_char(ch, "Flags for quest %d not altered.\r\n", quest->getVnum());
	} else {
		send_to_char(ch, "[%s] flags %s for quest %d.\r\n", buf2,
                     state == 1 ? "added" : "removed", quest->getVnum());

		sprintf(buf, "%s [%s] flags for quest %d '%s'.",
                state == 1 ? "added" : "removed",
                buf2, quest->getVnum(), quest->name);
		qlog(ch, buf, QLOG_COMP, LVL_AMBASSADOR, true);
	}
	save_quests();
}

static void
do_qcontrol_comment(struct creature *ch, char *argument, int com)
{
	Quest *quest = NULL;

	argument = one_argument(argument, arg1);
	skip_spaces(&argument);

	if (!*argument || !*arg1) {
		do_qcontrol_usage(ch, com);
		return;
	}

	if (!(quest = find_quest(ch, arg1)))
		return;

	if (!quest->canEdit(ch))
		return;

	sprintf(buf, "comments on quest %d '%s': %s",
			quest->getVnum(), quest->name, argument);
	qlog(ch, buf, QLOG_NORM, LVL_AMBASSADOR, true);
	send_to_char(ch, "Comment logged as '%s'", argument);

	save_quests();
}

static void
do_qcontrol_desc(struct creature *ch, char *argument, int com)
{
	Quest *quest = NULL;

	skip_spaces(&argument);

	if (!*argument) {
		do_qcontrol_usage(ch, com);
		return;
	}

	if (!(quest = find_quest(ch, argument)))
		return;

	if (!quest->canEdit(ch))
		return;

	if (check_editors(ch, &(quest->description)))
		return;

	act("$n begins to edit a quest description.\r\n", true, ch, 0, 0, TO_ROOM);

	if (quest->description) {
		sprintf(buf, "began editing description of quest '%s'", quest->name);
	} else {
		sprintf(buf, "began writing description of quest '%s'", quest->name);
	}

	start_editing_text(ch->desc, &quest->description, MAX_QUEST_DESC);
	SET_BIT(PLR_FLAGS(ch), PLR_WRITING);
}

static void
do_qcontrol_update(struct creature *ch, char *argument, int com)
{
	Quest *quest = NULL;

	skip_spaces(&argument);

	if (!*argument) {
		do_qcontrol_usage(ch, com);
		return;
	}

	if (!(quest = find_quest(ch, argument)))
		return;

	if (!quest->canEdit(ch))
		return;

	if (check_editors(ch, &(quest->updates)))
		return;

	if (quest->description) {
		sprintf(buf, "began editing update of quest '%s'", quest->name);
	} else {
		sprintf(buf, "began writing the update of quest '%s'", quest->name);
	}

	start_editing_text(ch->desc, &quest->updates, MAX_QUEST_UPDATE);
	SET_BIT(PLR_FLAGS(ch), PLR_WRITING);

	act("$n begins to edit a quest update.\r\n", true, ch, 0, 0, TO_ROOM);
	qlog(ch, buf, QLOG_COMP, LVL_AMBASSADOR, true);
}

static void
do_qcontrol_ban(struct creature *ch, char *argument, int com)
{
	Quest *quest = NULL;
	struct creature *vict = NULL;
	unsigned int idnum, accountID;
    struct account* account = NULL;
	int pid;
	int level = 0;
    bool del_vict=false;

	argument = two_arguments(argument, arg1, arg2);

	if (!*arg1 || !*arg2) {
		do_qcontrol_usage(ch, com);
		return;
	}

	if (playerIndex.getID(arg1) < 0) {
		send_to_char(ch, "There is no character named '%s'\r\n", arg1);
		return;
	} else {
        idnum = playerIndex.getID(arg1);
        accountID = playerIndex.getstruct accountID(arg1);
    }

    if (!(vict = get_char_in_world_by_idnum(idnum))) {
		// load the char from file
		pid = playerIndex.getID(arg1);
		if (pid > 0) {
            vict = new struct creature(true);
			vict->loadFromXML(pid);
            level = GET_LEVEL(vict);
            del_vict = true;
            account = struct account_retrieve(accountID);
		} else {
			send_to_char(ch, "Error loading char from file.\r\n");
			return;
		}
	} else {
		level = GET_LEVEL(vict);
        account = vict->account;
	}

	if (level >= GET_LEVEL(ch) && vict && ch != vict) {
		send_to_char(ch, "You are not powerful enough to do this.\r\n");
		return;
	}

	if (!strcmp("all", arg2)) { //ban from all quests
        if (!Security_isMember(ch, "QuestorAdmin")) {
            send_to_char(ch, "You do not have this ability.\r\n");
        } else if (account->is_quest_banned()) {
            send_to_char(ch,
                         "That player is already banned from all quests.\r\n");
        } else {
            account->set_quest_banned(true); //ban

            sprintf(buf, "banned %s from all quests.", GET_NAME(vict));
            qlog(ch, buf, QLOG_COMP, 0, true);

            send_to_char(ch, "%s is now banned from all quests.\r\n", GET_NAME(vict));
            send_to_char(vict, "You have been banned from all quests.\r\n");
        }
    } else {
        if (!(quest = find_quest(ch, arg2)))
            return;

        if (!quest->canEdit(ch))
            return;

        if (quest->getEnded() ) {
            send_to_char(ch, "That quest has already , you psychopath!\r\n");
            return;
        }

        if (quest->isBanned(idnum)) {
            send_to_char(ch, "That character is already banned from this quest.\r\n");
            return;
        }

        if (quest->isPlaying(idnum)) {
            if (!quest->removePlayer(idnum)) {
                send_to_char(ch, "Unable to auto-kick victim from quest!\r\n");
            } else {
                send_to_char(ch, "%s auto-kicked from quest.\r\n", arg1);

                sprintf(buf, "auto-kicked %s from quest %d '%s'.",
                        vict ? GET_NAME(vict) : arg1, quest->getVnum(), quest->name);
                qlog(ch, buf, QLOG_COMP, 0, true);
            }
        }

        if (!quest->addBan(idnum)) {
            send_to_char(ch, "Error banning char from quest.\r\n");
            return;
        }

        if (vict) {
            send_to_char(ch, "You have been banned from quest '%s'.\r\n", quest->name);
        }

        sprintf(buf, "banned %s from quest %d '%s'.",
                vict ? GET_NAME(vict) : arg1, quest->getVnum(), quest->name);
        qlog(ch, buf, QLOG_COMP, 0, true);

        send_to_char(ch, "%s banned from quest %d.\r\n",
                     vict ? GET_NAME(vict) : arg1, quest->getVnum());
    }
    if (del_vict) {
        delete vict;
        vict=NULL;
    }

}

static void
do_qcontrol_unban(struct creature *ch, char *argument, int com)
{

	Quest *quest = NULL;
	struct creature *vict = NULL;
	unsigned int idnum, accountID;
	int level = 0;
    bool del_vict = false;
    struct account* account;

	argument = two_arguments(argument, arg1, arg2);

	if (!*arg1 || !*arg2) {
		do_qcontrol_usage(ch, com);
		return;
	}

	if (playerIndex.getID(arg1) < 0) {
		send_to_char(ch, "There is no character named '%s'\r\n", arg1);
		return;
	} else {
        idnum = playerIndex.getID(arg1);
        accountID = playerIndex.getstruct accountID(arg1);
    }

    if (!(vict = get_char_in_world_by_idnum(idnum))) {
		// load the char from file
		if (idnum > 0) {
            vict = new struct creature(true);
            vict->loadFromXML(idnum);
            level = GET_LEVEL(vict);
            del_vict=true;
            account = struct account_retrieve(accountID);
		} else {
			send_to_char(ch, "Error loading char from file.\r\n");
			return;
		}
	} else {
		level = GET_LEVEL(vict);
        account = vict->account;
    }

    if (level >= GET_LEVEL(ch) && vict && ch != vict) {
		send_to_char(ch, "You are not powerful enough to do this.\r\n");
		return;
	}

	if (!strcmp("all", arg2)) { //unban from all quests
        if (!Security_isMember(ch, "QuestorAdmin")) {
            send_to_char(ch, "You do not have this ability.\r\n");
        } else if (!account->is_quest_banned()) {
            send_to_char(ch,
                         "That player is not banned from all quests... maybe you should ban him!\r\n");
        } else {

            account->set_quest_banned(false); //unban

            sprintf(buf, "unbanned %s from all quests.", GET_NAME(vict));
            qlog(ch, buf, QLOG_COMP, 0, true);

            send_to_char(ch, "%s unbanned from all quests.\r\n", GET_NAME(vict));
            send_to_char(vict, "You are no longer banned from all quests.\r\n");
        }
    } else {
        if (!(quest = find_quest(ch, arg2)))
            return;

        if (!quest->canEdit(ch))
            return;

        if (quest->getEnded() ) {
            send_to_char(ch, "That quest has already ended, you psychopath!\r\n");
            return;
        }

        if (!quest->isBanned(idnum)) {
            send_to_char(ch,
                         "That player is not banned... maybe you should ban him!\r\n");
            return;
        }

        if (!quest->removeBan(idnum)) {
            send_to_char(ch, "Error unbanning char from quest.\r\n");
            return;
        }

        sprintf(buf, "unbanned %s from %d quest '%s'.",
                vict ? GET_NAME(vict) : arg1,quest->getVnum(), quest->name);
        qlog(ch, buf, QLOG_COMP, 0, true);

        send_to_char(ch, "%s unbanned from quest %d.\r\n",
                     vict ? GET_NAME(vict) : arg1, quest->getVnum());
    }

    if (del_vict) {
        delete vict;
        vict=NULL;
    }
}

static void
do_qcontrol_level(struct creature *ch, char *argument, int com)
{
	Quest *quest = NULL;

	argument = one_argument(argument, arg1);
	skip_spaces(&argument);

	if (!*argument || !*arg1) {
		do_qcontrol_usage(ch, com);
		return;
	}

	if (!(quest = find_quest(ch, arg1)))
		return;

	if (!quest->canEdit(ch))
		return;

	quest->owner_level = atoi(arg2);

	sprintf(buf, "set quest %d '%s' access level to %d",
            quest->getVnum(),quest->name, quest->owner_level);
	qlog(ch, buf, QLOG_NORM, LVL_AMBASSADOR, true);

}

static void
do_qcontrol_minlev(struct creature *ch, char *argument, int com)
{
	Quest *quest = NULL;

	argument = two_arguments(argument, arg1, arg2);

	if (!*arg2 || !*arg1) {
		do_qcontrol_usage(ch, com);
		return;
	}

	if (!(quest = find_quest(ch, arg1)))
		return;

	if (!quest->canEdit(ch))
		return;

	quest->minlevel = MIN(LVL_GRIMP, MAX(0, atoi(arg2)));

	sprintf(buf, "set quest %d '%s' minimum level to %d",
            quest->getVnum(),quest->name, quest->minlevel);

	qlog(ch, buf, QLOG_NORM, LVL_AMBASSADOR, true);

}

static void
do_qcontrol_maxlev(struct creature *ch, char *argument, int com)
{
	Quest *quest = NULL;

	argument = two_arguments(argument, arg1, arg2);

	if (!*arg2 || !*arg1) {
		do_qcontrol_usage(ch, com);
		return;
	}

	if (!(quest = find_quest(ch, arg1)))
		return;

	if (!quest->canEdit(ch))
		return;

	quest->maxlevel = MIN(LVL_GRIMP, MAX(0, atoi(arg2)));

	sprintf(buf, "set quest %d '%s' maximum level to %d",
            quest->getVnum(),quest->name, quest->maxlevel);
	qlog(ch, buf, QLOG_NORM, LVL_AMBASSADOR, true);
}

static void
do_qcontrol_mingen(struct creature *ch, char *argument, int com)
{
	Quest *quest = NULL;

	argument = two_arguments(argument, arg1, arg2);

	if (!*arg2 || !*arg1) {
		do_qcontrol_usage(ch, com);
		return;
	}

	if (!(quest = find_quest(ch, arg1)))
		return;

	if (!quest->canEdit(ch))
		return;

	quest->mingen = MIN(10, MAX(0, atoi(arg2)));

	sprintf(buf, "set quest %d '%s' minimum gen to %d",
            quest->getVnum(),quest->name, quest->mingen );
	qlog(ch, buf, QLOG_NORM, LVL_AMBASSADOR, true);
}

static void
do_qcontrol_maxgen(struct creature *ch, char *argument, int com)
{
	Quest *quest = NULL;

	argument = two_arguments(argument, arg1, arg2);

	if (!*arg2 || !*arg1) {
		do_qcontrol_usage(ch, com);
		return;
	}

	if (!(quest = find_quest(ch, arg1)))
		return;

	if (!quest->canEdit(ch))
		return;

	quest->maxgen = MIN(10, MAX(0, atoi(arg2)));

	sprintf(buf, "set quest %d '%s' maximum gen to %d",
            quest->getVnum(),quest->name, quest->maxgen);
	qlog(ch, buf, QLOG_NORM, LVL_AMBASSADOR, true);
}

static void
do_qcontrol_mute(struct creature *ch, char *argument, int com)
{

	Quest *quest = NULL;
	long idnum;

	argument = two_arguments(argument, arg1, arg2);

	if (!*arg1 || !*arg2) {
		do_qcontrol_usage(ch, com);
		return;
	}

	if (!(quest = find_quest(ch, arg2)))
		return;

	if (!quest->canEdit(ch))
		return;

	if ((idnum = playerIndex.getID(arg1)) < 0) {
		send_to_char(ch, "There is no character named '%s'\r\n", arg1);
		return;
	}

	if (quest->getEnded()) {
		send_to_char(ch, "That quest has already ended, you psychopath!\r\n");
		return;
	}

	if (! quest->isPlaying(idnum)) {
		send_to_char(ch, "That player is not in the quest.\r\n");
		return;
	}

	if (quest->getPlayer(idnum).isFlagged(QP_MUTE)) {
		send_to_char(ch, "That player is already muted.\r\n");
		return;
	}

	quest->getPlayer(idnum).setFlag(QP_MUTE);

	sprintf(buf, "muted %s in %d quest '%s'.", arg1, quest->getVnum(),quest->name);
	qlog(ch, buf, QLOG_COMP, 0, true);

	send_to_char(ch, "%s muted for quest %d.\r\n", arg1, quest->getVnum());

}

static void
do_qcontrol_unmute(struct creature *ch, char *argument, int com)
{

	Quest *quest = NULL;
	long idnum;

	argument = two_arguments(argument, arg1, arg2);

	if (!*arg1 || !*arg2) {
		do_qcontrol_usage(ch, com);
		return;
	}

	if (!(quest = find_quest(ch, arg2)))
		return;

	if (!quest->canEdit(ch))
		return;

	if ((idnum = playerIndex.getID(arg1)) < 0) {
		send_to_char(ch, "There is no character named '%s'\r\n", arg1);
		return;
	}

	if (quest->getEnded()) {
		send_to_char(ch, "That quest has already ended, you psychopath!\r\n");
		return;
	}

	if(! quest->isPlaying(idnum) ) {
		send_to_char(ch, "That player is not in the quest.\r\n");
		return;
	}

	if (!quest->getPlayer(idnum).isFlagged(QP_MUTE)) {
		send_to_char(ch, "That player not muted.\r\n");
		return;
	}

	quest->getPlayer(idnum).removeFlag(QP_MUTE);

	sprintf(buf, "unmuted %s in quest %d '%s'.", arg1, quest->getVnum(), quest->name);
	qlog(ch, buf, QLOG_COMP, 0, true);

	send_to_char(ch, "%s unmuted for quest %d.\r\n", arg1, quest->getVnum());

}

static void
do_qcontrol_switch(struct creature *ch, char *argument)
{
	class Quest *quest = NULL;

	if ((!(quest = quest_by_vnum(GET_QUEST(ch))) ||
         !quest->isPlaying(GET_IDNUM(ch))  )
		&& (GET_LEVEL(ch) < LVL_ELEMENT)) {
		send_to_char(ch, "You are not currently active on any quest.\r\n");
		return;
	}
	do_switch(ch, argument, 0, SCMD_QSWITCH, 0);
}

static void
do_qcontrol_title(struct creature *ch, char *argument)
{
	Quest *quest;
	char *quest_str;

	quest_str = tmp_getword(&argument);
	if (isdigit(*quest_str)) {
		quest = quest_by_vnum(atoi(quest_str));
	} else {
		send_to_char(ch, "You must specify a quest to change the title of!\r\n");
		return;
	}

	if (!quest) {
		send_to_char(ch, "Quest #%s is not active!\r\n", quest_str);
		return;
	}

	if (!quest->canEdit(ch)) {
		send_to_char(ch, "Piss off, beanhead.  Permission DENIED!\r\n");
		return;
	}

	if (!*argument) {
		send_to_char(ch, "You might wanna put a new title in there, partner.\r\n");
		return;
	}

	free(quest->name);
	quest->name = strdup(argument);
	send_to_char(ch, "Quest #%d's title has been changed to %s\r\n",
                 quest->getVnum(), quest->name);
}

/*************************************************************************
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * UTILITY FUNCTIONS                                                     *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *************************************************************************/

/*************************************************************************
 * function to find a quest                                              *
 * argument is the vnum of the quest as a string                         *
 *************************************************************************/
Quest *
find_quest(struct creature *ch, char *argument)
{
	int vnum;
	Quest *quest = NULL;

	if (*argument)
		vnum = atoi(argument);
	else
		vnum = GET_QUEST(ch);

	if ((quest = quest_by_vnum(vnum)))
		return quest;

	send_to_char(ch, "There is no quest number %d.\r\n", vnum);

	return NULL;
}

/*************************************************************************
 * low level function to return a quest                                  *
 *************************************************************************/
Quest *
quest_by_vnum(int vnum)
{
	for( unsigned int i = 0; i < quests.size(); i++ ) {
		if( quests[i].getVnum() == vnum )
			return &quests[i];
	}
	return NULL;
}

/*************************************************************************
 * function to list active quests to both mortals and gods               *
 *************************************************************************/

const char *
list_active_quests(struct creature *ch)
{
	int timediff;
	int questCount = 0;
	char timestr_a[32];
	const char *msg =
        "Active quests:\r\n"
        "-Vnum--Owner-------Type------Name----------------------Age------Players\r\n";

	if (!quests.size())
		return "There are no active quests.\r\n";

	for (unsigned int i = 0; i < quests.size(); i++) {
		Quest *quest = &(quests[i]);
		if (quest->getEnded())
			continue;
		if (QUEST_FLAGGED(quest, QUEST_HIDE)
			&& !PRF_FLAGGED(ch, PRF_HOLYLIGHT))
			continue;
		questCount++;

		timediff = time(0) - quest->getStarted();
		snprintf(timestr_a, 16, "%02d:%02d", timediff / 3600, (timediff / 60) % 60);

		msg = tmp_sprintf( "%s %3d  %-10s  %-8s  %-24s %6s    %d\r\n", msg,
                           quest->getVnum(), playerIndex.getName(quest->getOwner()),
                           qtype_abbrevs[(int)quest->type], quest->name, timestr_a,
                           quest->getNumPlayers());
	}
	if (!questCount)
		return "There are no visible quests.\r\n";

	return tmp_sprintf("%s%d visible quest%s active.\r\n\r\n", msg,
                       questCount, questCount == 1 ? "" : "s");
}

void
list_quest_players(struct creature *ch, Quest * quest, char *outbuf)
{
	char buf[MAX_STRING_LENGTH], buf2[MAX_STRING_LENGTH];
	int i, num_online, num_offline;
	struct creature *vict = NULL;
	char bitbuf[1024];

	strcpy(buf, "  -Online Players------------------------------------\r\n");
	strcpy(buf2, "  -Offline Players-----------------------------------\r\n");

	for (i = num_online = num_offline = 0; i < quest->getNumPlayers(); i++) {
        const char *name = playerIndex.getName(quest->getPlayer(i).idnum);

		if (!name) {
			strcat(buf, "BOGUS player idnum!\r\n");
			strcat(buf2, "BOGUS player idnum!\r\n");
			errlog("bogus player idnum in list_quest_players.");
			break;
		}

		if (quest->getPlayer(i).getFlags()) {
			sprintbit(quest->getPlayer(i).getFlags(), qp_bits, bitbuf);
		} else {
			strcpy(bitbuf, "");
		}

		// player is in world and visible
		if ((vict = get_char_in_world_by_idnum(quest->getPlayer(i).idnum)) &&
			can_see_creature(ch, vict)) {

			// see if we can see the locations of the players
			if (PRF_FLAGGED(ch, PRF_HOLYLIGHT)) {
				sprintf(buf, "%s  %2d. %-15s - %-10s %s[%5d] D%d/MK%d/PK%d %s\r\n", buf,
                        ++num_online,
                        name,
                        bitbuf,
                        vict->in_room->name,
                        vict->in_room->number,
                        quest->getPlayer(i).deaths,
                        quest->getPlayer(i).mobkills,
                        quest->getPlayer(i).pkills,
                        vict->desc ? "" : "   (linkless)");
            } else if (QUEST_FLAGGED(quest, QUEST_WHOWHERE)) {
				sprintf(buf, "%s  %2d. %-15s - %s\r\n", buf,
                        ++num_online, name, vict->in_room->name);
			} else {
				sprintf(buf, "%s  %2d. %-15s - %-10s\r\n", buf, ++num_online,
                        name, bitbuf);
			}

		}
		// player is either offline or invisible
		else if (PRF_FLAGGED(ch, PRF_HOLYLIGHT)) {
			sprintf(buf2, "%s  %2d. %-15s - %-10s\r\n", buf2, ++num_offline,
                    name, bitbuf);
		}
	}

	// only gods may see the offline players
	if (PRF_FLAGGED(ch, PRF_HOLYLIGHT))
		strcat(buf, buf2);

	if (outbuf)
		strcpy(outbuf, buf);
	else
		page_string(ch->desc, buf);

}

void
qlog(struct creature *ch, const char *str, int type, int min_level, int file)
{
	// Mortals don't need to be seeing logs
	if (min_level < LVL_IMMORT)
		min_level = LVL_IMMORT;

	if (type) {
        struct descriptor_data *d = NULL;

        for (d = descriptor_list; d; d = d->next) {
            if (d->input_mode == CXN_PLAYING
				&& !PLR_FLAGGED(d->creature, PLR_WRITING)
				&& !PLR_FLAGGED(d->creature, PLR_OLC)) {
                int level = (d->original) ?
                    GET_LEVEL(d->original):GET_LEVEL(d->creature);
                int qlog_level = (d->original) ?
                    GET_QLOG_LEVEL(d->original):GET_QLOG_LEVEL(d->creature);

                if (level >= min_level && qlog_level >= type)
                    send_to_desc(d, "&Y[&g QLOG: %s %s &Y]&n\r\n",
                                 ch ? PERS(ch, d->creature) : "",
                                 str);
			}
		}
	}

	if (file) {
		fprintf(qlogfile, "%-19.19s _ %s %s\n",
                tmp_ctime(time(NULL)),
                ch ? GET_NAME(ch) : "",
                str);
		fflush(qlogfile);
	}
}

struct creature *
check_char_vis(struct creature *ch, char *name)
{
	struct creature *vict;

	if (!(vict = get_char_vis(ch, name))) {
		send_to_char(ch, "No-one by the name of '%s' around.\r\n", name);
	}
	return (vict);
}

int
boot_quests(void)
{
	if (!(qlogfile = fopen(QLOGFILENAME, "a"))) {
		errlog("unable to open qlogfile.");
		safe_exit(1);
	}
	quests.loadQuests();
	return 1;
}

int
check_editors(struct creature *ch, char **buffer)
{
	struct descriptor_data *d = NULL;

	for (d = descriptor_list; d; d = d->next) {
		if (d->text_editor && d->text_editor->IsEditing(*buffer)) {
			send_to_char(ch, "%s is already editing that buffer.\r\n",
                         d->creature ? PERS(d->creature, ch) : "BOGUSMAN");
			return 1;
		}
	}
	return 0;
}

ACMD(do_qlog)
{
	int i;

	skip_spaces(&argument);

	GET_QLOG_LEVEL(ch) = MIN(MAX(GET_QLOG_LEVEL(ch), 0), QLOG_COMP);

	if (!*argument) {
		send_to_char(ch, "You current qlog level is: %s.\r\n",
                     qlog_types[(int)GET_QLOG_LEVEL(ch)]);
		return;
	}

	if ((i = search_block(argument, qlog_types, false)) < 0) {
		buf[0] = '\0';
		for (i = 0;*qlog_types[i] != '\n';i++) {
			strcat(buf, qlog_types[i]);
			strcat(buf, " ");
		}
		send_to_char(ch, "Unknown qlog type '%s'.  Options are: %s\r\n",
                     argument, buf);
		return;
	}

	GET_QLOG_LEVEL(ch) = i;
	send_to_char(ch, "Qlog level set to: %s.\r\n", qlog_types[i]);
}

/***************************************************************************/
/***************************************************************************/
/***************************************************************************/

const char *quest_commands[][2] = {
	{"list", "shows currently active quests"},
	{"info", "get info about a specific quest"},
	{"join", "join an active quest"},
	{"leave", "leave a quest"},

	// Go back and set this in the player file when you get time!
	{"status", "list the quests you are participating in"},
	{"who", "list all players in a specific quest"},
	{"current", "specify which quest you are currently active in"},
	{"ignore", "ignore most qsays from specified quest."},
	{"\n", "\n"}
	// "\n" terminator must be here
};

ACMD(do_quest)
{
	int i = 0;

	if (IS_NPC(ch))
		return;

	skip_spaces(&argument);
	argument = one_argument(argument, arg1);

	if (!*arg1) {
		send_to_char(ch, "Quest options:\r\n");

		while (1) {
			if (*quest_commands[i][0] == '\n')
				break;
			send_to_char(ch, "  %-10s -- %s\r\n",
                         quest_commands[i][0],
                         quest_commands[i][1]);
			i++;
		}

		return;
	}

	for (i = 0;; i++) {
		if (*quest_commands[i][0] == '\n') {
			send_to_char(ch,
                         "No such quest option, '%s'.  Type 'quest' for usage.\r\n",
                         arg1);
			return;
		}
		if (is_abbrev(arg1, quest_commands[i][0]))
			break;
	}

	switch (i) {
	case 0:					// list
		do_quest_list(ch);
		break;
	case 1:					// info
		do_quest_info(ch, argument);
		break;
	case 2:					// join
		do_quest_join(ch, argument);
		break;
	case 3:					// leave
		do_quest_leave(ch, argument);
		break;
	case 4:					// status
		do_quest_status(ch);
		break;
	case 5:					// who
		do_quest_who(ch, argument);
		break;
	case 6:					// current
		do_quest_current(ch, argument);
		break;
	case 7:					// ignore
		do_quest_ignore(ch, argument);
		break;
	default:
		break;
	}
}

void
do_quest_list(struct creature *ch)
{
	send_to_char(ch, "%s", list_active_quests(ch));
}

void
do_quest_join(struct creature *ch, char *argument)
{
	Quest *quest = NULL;

	skip_spaces(&argument);

	if (!*argument) {
		send_to_char(ch, "Join which quest?\r\n");
		return;
	}

	if (!(quest = find_quest(ch, argument)))
		return;

	if (quest->getEnded() ||
		(QUEST_FLAGGED(quest, QUEST_HIDE)
         && !PRF_FLAGGED(ch, PRF_HOLYLIGHT))) {
		send_to_char(ch, "No such quest is running.\r\n");
		return;
	}

	if (quest->isPlaying(GET_IDNUM(ch))) {
		send_to_char(ch, "You are already in that quest, fool.\r\n");
		return;
	}

	if (!quest->canJoin(ch))
		return;

	if (! quest->addPlayer(GET_IDNUM(ch))) {
		send_to_char(ch, "Error adding char to quest.\r\n");
		return;
	}

	GET_QUEST(ch) = quest->getVnum();
	ch->crashSave();

	sprintf(buf, "joined quest %d '%s'.", quest->getVnum(),quest->name);
	qlog(ch, buf, QLOG_COMP, 0, true);

	send_to_char(ch, "You have joined quest '%s'.\r\n", quest->name);

	sprintf(buf, "%s has joined the quest.", GET_NAME(ch));
	send_to_quest(NULL, buf, quest, MAX(GET_INVIS_LVL(ch), 0), QCOMM_ECHO);
}

void
do_quest_leave(struct creature *ch, char *argument)
{
	Quest *quest = NULL;

	skip_spaces(&argument);

	if (!*argument) {
		if (!(quest = quest_by_vnum(GET_QUEST(ch)))) {
			send_to_char(ch, "Leave which quest?\r\n");
			return;
		}
	}

	else if (!(quest = find_quest(ch, argument)))
		return;

	if (quest->getEnded() ||
		(QUEST_FLAGGED(quest, QUEST_HIDE)
         && !PRF_FLAGGED(ch, PRF_HOLYLIGHT))) {
		send_to_char(ch, "No such quest is running.\r\n");
		return;
	}

	if (!quest->isPlaying(GET_IDNUM(ch))) {
		send_to_char(ch, "You are not in that quest, fool.\r\n");
		return;
	}

	if (!quest->canLeave(ch))
		return;

	if (!quest->removePlayer(GET_IDNUM(ch))) {
		send_to_char(ch, "Error removing char from quest.\r\n");
		return;
	}

	sprintf(buf, "left quest %d '%s'.", quest->getVnum(),quest->name);
	qlog(ch, buf, QLOG_COMP, 0, true);

	send_to_char(ch, "You have left quest '%s'.\r\n", quest->name);

	sprintf(buf, "%s has left the quest.", GET_NAME(ch));
	send_to_quest(NULL, buf, quest, MAX(GET_INVIS_LVL(ch), 0), QCOMM_ECHO);
}

void
do_quest_info(struct creature *ch, char *argument)
{
	Quest *quest = NULL;
	int timediff;
	char timestr_a[128];
	char *timestr_s;

	skip_spaces(&argument);

	if (!*argument) {
		if (!(quest = quest_by_vnum(GET_QUEST(ch)))) {
			send_to_char(ch, "Get info on which quest?\r\n");
			return;
		}
	} else if (!(quest = find_quest(ch, argument)))
		return;

	if (quest->getEnded() ||
		(QUEST_FLAGGED(quest, QUEST_HIDE)
         && !PRF_FLAGGED(ch, PRF_HOLYLIGHT))) {
		send_to_char(ch, "No such quest is running.\r\n");
		return;
	}

	timediff = time(0) - quest->getStarted();
	sprintf(timestr_a, "%02d:%02d", timediff / 3600, (timediff / 60) % 60);

	time_t started = quest->getStarted();
	timestr_s = asctime(localtime(&started));
	*(timestr_s + strlen(timestr_s) - 1) = '\0';

	sprintf(buf,
            "Quest [%d] info:\r\n"
            "Owner:  %s\r\n"
            "Name:   %s\r\n"
            "Description:\r\n%s"
            "Updates:\r\n%s"
            "  Type:            %s\r\n"
            "  Started:         %s\r\n"
            "  Age:             %s\r\n"
            "  Min Level:   Gen %-2d, Level %2d\r\n"
            "  Max Level:   Gen %-2d, Level %2d\r\n"
            "  Num Players:     %d\r\n"
            "  Max Players:     %d\r\n",
            quest->getVnum(),
            playerIndex.getName(quest->getOwner()), quest->name,
            quest->description ? quest->description : "None.\r\n",
            quest->updates ? quest->updates : "None.\r\n",
            qtypes[(int)quest->type], timestr_s, timestr_a,
            quest->mingen, quest->minlevel,
            quest->maxgen, quest->maxlevel,
            quest->getNumPlayers(), quest->getMaxPlayers());
	page_string(ch->desc, buf);

}

void
do_quest_status(struct creature *ch)
{
	char timestr_a[128];
	int timediff;
	bool found = false;

	const char *msg = "You are participating in the following quests:\r\n"
		"-Vnum--Owner-------Type------Name----------------------Age------Players\r\n";

	for( unsigned int i = 0; i < quests.size(); i++ ) {
		Quest *quest = &(quests[i]);
		if (quest->getEnded())
			continue;
		if( quest->isPlaying( GET_IDNUM(ch) ) ) {
			timediff = time(0) - quest->getStarted();
			snprintf(timestr_a,128, "%02d:%02d", timediff / 3600, (timediff / 60) % 60);

			char *line = tmp_sprintf(" %s%3d  %-10s  %-8s  %-24s %6s    %d\r\n",
                                     quest->getVnum()== GET_QUEST(ch) ? "*" : " ",
                                     quest->getVnum(), playerIndex.getName(quest->getOwner()),
                                     qtype_abbrevs[(int)quest->type],
                                     quest->name, timestr_a, quest->getNumPlayers());
			msg = tmp_strcat(msg,line,NULL);
			found = true;
		}
	}
	if (!found)
		msg = tmp_strcat(msg,"None.\r\n",NULL);
	page_string(ch->desc, msg);
}

void
do_quest_who(struct creature *ch, char *argument)
{
	Quest *quest = NULL;
	skip_spaces(&argument);

	if (!*argument) {
		if (!(quest = quest_by_vnum(GET_QUEST(ch)))) {
			send_to_char(ch, "List the players for which quest?\r\n");
			return;
		}
	} else if (!(quest = find_quest(ch, argument)))
		return;

	if (quest->getEnded()||
		(QUEST_FLAGGED(quest, QUEST_HIDE)
         && !PRF_FLAGGED(ch, PRF_HOLYLIGHT))) {
		send_to_char(ch, "No such quest is running.\r\n");
		return;
	}

	if (QUEST_FLAGGED(quest, QUEST_NOWHO)) {
		send_to_char(ch, "Sorry, you cannot get a who listing for this quest.\r\n");
		return;
	}

	if (QUEST_FLAGGED(quest, QUEST_NO_OUTWHO) &&
        !quest->isPlaying(GET_IDNUM(ch))) {
		send_to_char(ch,
                     "Sorry, you cannot get a who listing from outside this quest.\r\n");
		return;
	}

	list_quest_players(ch, quest, NULL);

}

void
do_quest_current(struct creature *ch, char *argument)
{
	Quest *quest = NULL;

	skip_spaces(&argument);

	if (!*argument) {
		if (!(quest = quest_by_vnum(GET_QUEST(ch)))) {
			send_to_char(ch, "You are not current on any quests.\r\n");
			return;
		}
		send_to_char(ch, "You are current on quest %d, '%s'\r\n", quest->getVnum(),
                     quest->name);
		return;
	}

	if (!(quest = find_quest(ch, argument)))
		return;

	if (quest->getEnded() ||
		(QUEST_FLAGGED(quest, QUEST_HIDE)
         && !PRF_FLAGGED(ch, PRF_HOLYLIGHT))) {
		send_to_char(ch, "No such quest is running.\r\n");
		return;
	}

	if ( !quest->isPlaying(GET_IDNUM(ch))) {
		send_to_char(ch, "You are not even in that quest.\r\n");
		return;
	}

	GET_QUEST(ch) = quest->getVnum();
	ch->crashSave();

	send_to_char(ch, "Ok, you are now currently active in '%s'.\r\n", quest->name);
}

void
do_quest_ignore(struct creature *ch, char *argument)
{
	Quest *quest = NULL;

	skip_spaces(&argument);

	if (!*argument) {
		if (!(quest = quest_by_vnum(GET_QUEST(ch)))) {
			send_to_char(ch, "Ignore which quest?\r\n");
			return;
		}
	}

	else if (!(quest = find_quest(ch, argument)))
		return;

	if (quest->getEnded() ||
		(QUEST_FLAGGED(quest, QUEST_HIDE)
         && !PRF_FLAGGED(ch, PRF_HOLYLIGHT))) {
		send_to_char(ch, "No such quest is running.\r\n");
		return;
	}

	if(! quest->isPlaying(GET_IDNUM(ch) ) ) {
		send_to_char(ch, "You are not even in that quest.\r\n");
		return;
	}

	quest->getPlayer(GET_IDNUM(ch)).toggleFlag(QP_IGNORE);

	send_to_char(ch, "Ok, you are %s ignoring '%s'.\r\n",
                 quest->getPlayer(GET_IDNUM(ch)).isFlagged(QP_IGNORE) ? "now" : "no longer",
                 quest->name);
}

ACMD(do_qsay)
{
	Quest *quest = NULL;

	skip_spaces(&argument);

	if (!(quest = quest_by_vnum(GET_QUEST(ch))) ||
        !quest->isPlaying(GET_IDNUM(ch)) ) {
		send_to_char(ch, "You are not currently active on any quest.\r\n");
		return;
	}

	if( quest->getPlayer(GET_IDNUM(ch)).isFlagged(QP_MUTE) ) {
		send_to_char(ch, "You have been quest-muted.\r\n");
		return;
	}

	if( quest->getPlayer(GET_IDNUM(ch)).isFlagged(QP_IGNORE) ) {
		send_to_char(ch, "You can't quest-say while ignoring the quest.\r\n");
		return;
	}

	if (!*argument) {
		send_to_char(ch, "Qsay what?\r\n");
		return;
	}

	send_to_quest(ch, argument, quest, 0, QCOMM_SAY);
}

ACMD(do_qecho)
{
	Quest *quest = NULL;

	skip_spaces(&argument);

	if (!(quest = quest_by_vnum(GET_QUEST(ch))) ||
		!quest->isPlaying(GET_IDNUM(ch)) ){
		send_to_char(ch, "You are not currently active on any quest.\r\n");
		return;
	}

	if( quest->getPlayer(GET_IDNUM(ch)).isFlagged(QP_MUTE)) {
		send_to_char(ch, "You have been quest-muted.\r\n");
		return;
	}

	if( quest->getPlayer(GET_IDNUM(ch)).isFlagged(QP_IGNORE)) {
		send_to_char(ch, "You can't quest-echo while ignoring the quest.\r\n");
		return;
	}

	if (!*argument) {
		send_to_char(ch, "Qecho what?\r\n");
		return;
	}

	send_to_quest(ch, argument, quest, 0, QCOMM_ECHO);
}

void
qp_reload(int sig __attribute__ ((unused)))
{
	struct creature *immortal;
	int online = 0;

	//
	// Check if the imm is logged on
	//
	struct creatureList_iterator cit = characterList.begin();
	for (; cit != characterList.end(); ++cit) {
		immortal = *cit;
		if (GET_LEVEL(immortal) >= LVL_AMBASSADOR && (!IS_NPC(immortal)
                                                      && GET_QUEST_ALLOWANCE(immortal) > 0)) {
			slog("QP_RELOAD: Reset %s to %d QPs from %d. ( online )",
                 GET_NAME(immortal), GET_QUEST_ALLOWANCE(immortal),
                 GET_IMMORT_QP(immortal));

			GET_IMMORT_QP(immortal) = GET_QUEST_ALLOWANCE(immortal);
			send_to_char(immortal, "Your quest points have been restored!\r\n");
			immortal->crashSave();
			online++;
		}
	}
	mudlog(LVL_GRGOD, NRM, true,
           "QP's have been reloaded - %d reset online", online);
}

static void
do_qcontrol_award(struct creature *ch, char *argument, int com)
{
	Quest *quest = NULL;
	struct creature *vict = NULL;
	char arg3[MAX_INPUT_LENGTH];	// Awarded points
	int award;
	int idnum;

	argument = two_arguments(argument, arg1, arg2);
	argument = one_argument(argument, arg3);
	award = atoi(arg3);

	if (!*arg1 || !*arg2) {
		do_qcontrol_usage(ch, com);
		return;
	}

	if (!(quest = find_quest(ch, arg1))) {
		return;
	}

	if (!quest->canEdit(ch)) {
		return;
	}

	if ((idnum = playerIndex.getID(arg2)) < 0) {
		send_to_char(ch, "There is no character named '%s'.\r\n", arg2);
		return;
	}

	if (quest->getEnded() ) {
		send_to_char(ch, "That quest has already ended, you psychopath!\r\n");
		return;
	}

	if ((vict = get_char_in_world_by_idnum(idnum))) {
		if(! quest->isPlaying(idnum) ) {
			send_to_char(ch, "No such player in the quest.\r\n");
			return;
		}
		if (!vict->desc) {
			send_to_char(ch,
                         "You can't award quest points to a linkless player.\r\n");
			return;
		}
	}

	if ((award <= 0)) {
		send_to_char(ch, "The award must be greater than zero.\r\n");
		return;
	}

	if ((award > GET_IMMORT_QP(ch))) {
		send_to_char(ch, "You do not have the required quest points.\r\n");
		return;
	}

	if (!(vict)) {
		send_to_char(ch, "No such player in the quest.\r\n");
		return;
	}

	if ((ch) && (vict)) {
		GET_IMMORT_QP(ch) -= award;
		vict->account->set_quest_points(vict->account->get_quest_points() + award);
		quest->addAwarded(award);
		ch->crashSave();
		vict->crashSave();
		sprintf(buf, "awarded player %s %d qpoints.", GET_NAME(vict), award);
		qlog(ch, buf, QLOG_BRIEF, MAX(GET_INVIS_LVL(ch), LVL_AMBASSADOR),
             true);
		if (*argument) {
			sprintf(buf, "'s Award Comments: %s", argument);
			qlog(ch, buf, QLOG_COMP, MAX(GET_INVIS_LVL(ch), LVL_AMBASSADOR),
                 true);
		}
	}

}
static void
do_qcontrol_penalize(struct creature *ch, char *argument, int com)
{
	Quest *quest = NULL;
	struct creature *vict = NULL;
	char arg3[MAX_INPUT_LENGTH];	// Penalized points
	int penalty;
	int idnum;

	argument = two_arguments(argument, arg1, arg2);
	argument = one_argument(argument, arg3);
	penalty = atoi(arg3);

	if (!*arg1 || !*arg2) {
		do_qcontrol_usage(ch, com);
		return;
	}

	if (!(quest = find_quest(ch, arg1))) {
		return;
	}

	if (!quest->canEdit(ch)) {
		return;
	}

	if ((idnum = playerIndex.getID(arg2)) < 0) {
		send_to_char(ch, "There is no character named '%s'.\r\n", arg2);
		return;
	}

	if (quest->getEnded() ) {
		send_to_char(ch, "That quest has already ended, you psychopath!\r\n");
		return;
	}

	if ((vict = get_char_in_world_by_idnum(idnum))) {
		if(! quest->isPlaying(idnum) ) {
			send_to_char(ch, "No such player in the quest.\r\n");
			return;
		}
	}

	if (!(vict)) {
		send_to_char(ch, "No such player in the quest.\r\n");
		return;
	}

	if ((penalty <= 0)) {
		send_to_char(ch, "The penalty must be greater than zero.\r\n");
		return;
	}

	if ((penalty > vict->account->get_quest_points())) {
		send_to_char(ch, "They do not have the required quest points.\r\n");
		return;
	}

	if ((ch) && (vict)) {
		vict->account->set_quest_points(vict->account->get_quest_points() - penalty);
		GET_IMMORT_QP(ch) += penalty;
		quest->addPenalized(penalty);
		vict->crashSave();
		ch->crashSave();
		send_to_char(vict, "%d of your quest points have been taken by %s!\r\n",
                     penalty, GET_NAME(ch));
		send_to_char(ch, "%d quest points transferred from %s.\r\n", penalty,
                     GET_NAME(vict));
		sprintf(buf, "penalized player %s %d qpoints.", GET_NAME(vict),
                penalty);
		qlog(ch, buf, QLOG_BRIEF, MAX(GET_INVIS_LVL(ch), LVL_AMBASSADOR),
             true);
		if (*argument) {
			sprintf(buf, "'s Penalty Comments: %s", argument);
			qlog(ch, buf, QLOG_COMP, MAX(GET_INVIS_LVL(ch), LVL_AMBASSADOR),
                 true);
		}
	}
}

static void
do_qcontrol_restore(struct creature *ch, char *argument, int com)
{
	Quest *quest = NULL;
	struct creature *vict = NULL;
	char *str;
	int i;

	str = tmp_getword(&argument);

	if (!str) {
		do_qcontrol_usage(ch, com);
		return;
	}

	if (!(quest = find_quest(ch, str)))
		return;

	if (!quest->canEdit(ch))
		return;

	for (i = 0; i < quest->getNumPlayers(); i++) {
		if ((vict = get_char_in_world_by_idnum(quest->getPlayer(i).idnum))) {
			vict->restore();
			if (!PLR_FLAGGED(vict, PLR_MAILING | PLR_WRITING | PLR_OLC) &&
                vict->desc) {
				send_to_char(vict, "%s",
                             compose_qcomm_string(ch, vict, quest, QCOMM_ECHO,
                                                  "You have been restored!") );
			}
		}
	}
}

static void
do_qcontrol_save(struct creature *ch)
{
	quests.save();
	send_to_char(ch,"Quests saved.\r\n");
}

ACMD(do_qcontrol)
{
	char arg1[MAX_INPUT_LENGTH];
	int com;

	argument = one_argument(argument, arg1);
	skip_spaces(&argument);
	if (!*arg1) {
		do_qcontrol_options(ch);
		return;
	}

	for (com = 0;; com++) {
		if (!qc_options[com].keyword) {
			send_to_char(ch, "Unknown qcontrol option, '%s'.\r\n", arg1);
			return;
		}
		if (is_abbrev(arg1, qc_options[com].keyword))
			break;
	}
	if (qc_options[com].level > GET_LEVEL(ch)) {
		send_to_char(ch, "You are not godly enough to do this!\r\n");
		return;
	}

	switch (com) {
	case 0:					// show
		do_qcontrol_show(ch, argument);
		break;
	case 1:					// create
		do_qcontrol_create(ch, argument, com);
		break;
	case 2:					// end
		do_qcontrol_end(ch, argument, com);
		break;
	case 3:					// add
		do_qcontrol_add(ch, argument, com);
		break;
	case 4:					// kick
		do_qcontrol_kick(ch, argument, com);
		break;
	case 5:					// flags
		do_qcontrol_flags(ch, argument, com);
		break;
	case 6:					// comment
		do_qcontrol_comment(ch, argument, com);
		break;
	case 7:					// desc
		do_qcontrol_desc(ch, argument, com);
		break;
	case 8:					// update
		do_qcontrol_update(ch, argument, com);
		break;
	case 9:					// ban
		do_qcontrol_ban(ch, argument, com);
		break;
	case 10:					// unban
		do_qcontrol_unban(ch, argument, com);
		break;
	case 11:					// mute
		do_qcontrol_mute(ch, argument, com);
		break;
	case 12:					// unnute
		do_qcontrol_unmute(ch, argument, com);
		break;
	case 13:					// level
		do_qcontrol_level(ch, argument, com);
		break;
	case 14:					// minlev
		do_qcontrol_minlev(ch, argument, com);
		break;
	case 15:					// maxlev
		do_qcontrol_maxlev(ch, argument, com);
		break;
	case 16:					// maxgen
		do_qcontrol_mingen(ch, argument, com);
		break;
	case 17:					// mingen
		do_qcontrol_maxgen(ch, argument, com);
		break;
	case 18:					// Load Mobile
		do_qcontrol_mload(ch, argument, com);
		break;
	case 19:					// Purge Mobile
		do_qcontrol_purge(ch, argument);
		break;
	case 20:
		do_qcontrol_save(ch);
		break;
	case 21:					// help - in help_collection.cc
		do_qcontrol_help(ch, argument);
		break;
	case 22:
		do_qcontrol_switch(ch, argument);
		break;
	case 23:	// title
		do_qcontrol_title(ch, argument);
		break;
	case 24:					// oload
		do_qcontrol_oload(ch, argument, com);
		break;
	case 25:
		do_qcontrol_trans(ch, argument);
		break;
	case 26:					// award
		do_qcontrol_award(ch, argument, com);
		break;
	case 27:					// penalize
		do_qcontrol_penalize(ch, argument, com);
		break;
	case 28:					// restore
		do_qcontrol_restore(ch, argument, com);
		break;
    case 29:
        do_qcontrol_loadroom(ch, argument, com);
        break;
	default:
		send_to_char(ch, "Sorry, this qcontrol option is not implemented.\r\n");
		break;
	}
}

void
save_quests() {
	quests.save();
}

Quest_Quest( struct creature *ch, int type, const char* name )
	: players(), bans()
{
	this->vnum = quests.getNextVnum();
	this->type = type;
	this->name = strdup(name);
	this->owner_id = GET_IDNUM(ch);
	this->owner_level = GET_LEVEL(ch);

	flags = QUEST_HIDE;
	started = time(0);
	ended = 0;

	description = (char*) malloc(sizeof(char) * 2);
	description[0] = ' ';
	description[1] = '\0';

	updates = (char*) malloc(sizeof(char) * 2);
	updates[0] = ' ';
	updates[1] = '\0';

	max_players = 0;
	awarded = 0;
	penalized = 0;
	minlevel = 0;
	maxlevel = 49;
	mingen = 0;
	maxgen = 10;
    loadroom = -1;
}

Quest_~Quest()
{
	clearDescs();
}
void Quest_clearDescs() {
	if( name != NULL ) {
		free(name);
		name = NULL;
	}
	if( description != NULL ) {
		free(description);
		description = NULL;
	}
	if( updates != NULL ) {
		free(updates);
		updates = NULL;
	}
}

Quest_Quest( const Quest &q ) : players(), bans()
{
	name = description = updates = NULL;
	*this = q;
}

Quest_Quest( xmlNodePtr n, xmlDocPtr doc )
{
	xmlChar *s;
	vnum = xmlGetIntProp(n, "VNUM");
	quests.setNextVnum( vnum );
	owner_id = xmlGetLongProp(n, "OWNER");
	started = (time_t) xmlGetLongProp(n, "STARTED");
	ended = (time_t) xmlGetLongProp(n, "ENDED");
	max_players = xmlGetIntProp(n, "MAX_PLAYERS");
	maxlevel = xmlGetIntProp(n, "MAX_LEVEL");
	minlevel = xmlGetIntProp(n, "MIN_LEVEL");
	maxgen = xmlGetIntProp(n, "MAX_GEN");
	mingen = xmlGetIntProp(n, "MIN_GEN");
	awarded = xmlGetIntProp(n, "AWARDED");
	penalized = xmlGetIntProp(n, "PENALIZED");
	owner_level = xmlGetIntProp(n, "OWNER_LEVEL");
	flags = xmlGetIntProp(n, "FLAGS");
    loadroom = xmlGetIntProp(n, "LOADROOM");
	char *typest = xmlGetProp(n, "TYPE");
	type = search_block(typest, qtype_abbrevs, true);
	free(typest);

	name = xmlGetProp(n, "NAME");

	description = updates = NULL;

	xmlNodePtr cur = n->xmlChildrenNode;
	while (cur != NULL) {
		if ((xmlMatches(cur->name, "Description"))) {
			s = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
			if (s != NULL) {
				description = (char *)s;
			}
		} else if ((xmlMatches(cur->name, "Update"))) {
			s = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
			if (s != NULL) {
				updates = (char *)s;
			}
		} else if ((xmlMatches(cur->name, "Player"))) {
			long id = xmlGetLongProp(cur, "ID");
			int flags = xmlGetIntProp(cur, "FLAGS");
			qplayer_data player(id);
			player.setFlag(flags);
            player.deaths = xmlGetIntProp(cur, "DEATHS");
            player.mobkills = xmlGetIntProp(cur, "MKILLS");
            player.pkills = xmlGetIntProp(cur, "PKILLS");
			players.push_back(player);
		} else if ((xmlMatches(cur->name, "Ban"))) {
			long id = xmlGetLongProp(cur, "ID");
			int flags = xmlGetIntProp(cur, "FLAGS");
			qplayer_data player(id);
			player.setFlag(flags);
			bans.push_back(player);
		}
		cur = cur->next;
	}
}

Quest& Quest_operator=( const Quest &q )
{
	clearDescs();

	vnum = q.vnum;
	type = q.type;
	name = strdup(q.name);
	owner_id = q.owner_id;
	owner_level = q.owner_level;
	players = q.players;
	bans = q.bans;

	flags = q.flags;
	started = q.started;
	ended = q.ended;
	if( q.description != NULL )
		description = strdup(q.description);
	if( q.updates != NULL )
		updates = strdup(q.updates);
	max_players = q.max_players;
	awarded = q.awarded;
	penalized = q.penalized;
	minlevel = q.minlevel;
	maxlevel = q.maxlevel;
	maxgen = q.maxgen;
	mingen = q.mingen;
    loadroom = q.loadroom;
	return *this;
}

bool Quest_removePlayer( long id ) {
	struct creature *vict = NULL;
	vector<qplayer_data>_iterator it;

	it = find(players.begin(),players.end(),qplayer_data(id) );
	if( it == players.end() )
		return false;

	if (!(vict = get_char_in_world_by_idnum(id))) {
		// load the char from file
		vict = new struct creature(true);
		if (vict->loadFromXML(id)) {
			//HERE
			GET_QUEST(vict) = 0;
			save_player_to_xml(vict);
			delete vict;
		} else {
			errlog("Error loading player id %ld from file for removal from quest %d.\r\n",
                   id, vnum );
			delete vict;
			return false;
		}
	} else {
		GET_QUEST(vict) = 0;
		vict->crashSave();
	}

	players.erase(it);
	return true;
}

bool Quest_removeBan( long id ) {
	vector<qplayer_data>_iterator it;

	it = find(bans.begin(),bans.end(),qplayer_data(id) );
	if( it == bans.end() )
		return false;

	bans.erase(it);
	return true;
}

bool Quest_addBan( long id ) {
	if( find(bans.begin(),bans.end(),qplayer_data(id) ) == bans.end() ){
		bans.push_back(qplayer_data(id));
		return true;
	}
	return false;
}

bool Quest_isBanned( long id ) {
	return find(bans.begin(),bans.end(),qplayer_data(id) ) != bans.end();
}

bool Quest_isPlaying( long id ) {
	return find(players.begin(),players.end(),qplayer_data(id) ) != players.end();
}

qplayer_data &Quest_getPlayer( long id ) {
	vector<qplayer_data>_iterator it;
	it = find(players.begin(),players.end(),qplayer_data(id) );
	return *it;
}

qplayer_data &Quest_getBan( long id ) {
	vector<qplayer_data>_iterator it;
	it = find(bans.begin(),bans.end(),qplayer_data(id) );
	return *it;
}

bool Quest_addPlayer( long id ) {
	if( find(players.begin(),players.end(),qplayer_data(id) ) == players.end() ){
		players.push_back(qplayer_data(id));
		return true;
	}
	return false;
}

void qplayer_data_removeFlag( int flag ) {
	REMOVE_BIT(flags,flag);
}

void qplayer_data_toggleFlag( int flag ) {
	TOGGLE_BIT(flags,flag);
}

void qplayer_data_setFlag( int flag ) {
	SET_BIT(flags,flag);
}

bool qplayer_data_isFlagged( int flag ) {
	return IS_SET(flags, flag);
}

qplayer_data& qplayer_data_operator=( const qplayer_data &q )
{
	idnum = q.idnum;
	flags = q.flags;
    deaths = q.deaths;
    mobkills = q.mobkills;
    pkills = q.pkills;
	return *this;
}

bool Quest_canEdit(struct creature *ch)
{
	if( Security_isMember(ch, "QuestorAdmin") )
		return true;

	if (GET_LEVEL(ch) <= owner_level &&
		GET_IDNUM(ch) != owner_id && GET_IDNUM(ch) != 1) {
		send_to_char(ch, "You cannot do that to this quest.\r\n");
		return false;
	}
	return true;
}
bool
Quest_canJoin(struct creature *ch)
{
	if (PLR_FLAGGED(ch, PLR_KILLER | PLR_THIEF)) {
		send_to_char(ch, "Join when you're no longer a killer or thief.\r\n");
		return false;
	}

	if (QUEST_FLAGGED(this, QUEST_NOJOIN)) {
		send_to_char(ch, "This quest is open by invitation only.\r\n"
                     "Contact the wizard in charge of the quest for an invitation.\r\n");
		return false;
	}

	if (!levelOK(ch))
		return false;

	if (isBanned(GET_IDNUM(ch))) {
		send_to_char(ch, "Sorry, you have been banned from this quest.\r\n");
		return false;
	}

    if (ch->account->is_quest_banned()) {
        send_to_char(ch, "Sorry, you have been banned from all quests.\r\n");
        return false;
    }

	return true;
}

bool
Quest_canLeave(struct creature *ch)
{
	if (QUEST_FLAGGED(this, QUEST_NOLEAVE)) {
		send_to_char(ch, "Sorry, you cannot leave the quest right now.\r\n");
		return false;
	}
	return true;
}

bool
Quest_levelOK(struct creature *ch)
{
	if (GET_LEVEL(ch) >= LVL_AMBASSADOR)
		return true;

	if (GET_REMORT_GEN(ch) > maxgen) {
		send_to_char(ch, "Your generation is too high for this quest.\r\n");
		return false;
	}

	if ( GET_LEVEL(ch) > maxlevel ) {
		send_to_char(ch, "Your level is too high for this quest.\r\n");
		return false;
	}
	if (GET_REMORT_GEN(ch) < mingen ) {
		send_to_char(ch, "Your generation is too low for this quest.\r\n");
		return false;
	}

	if ( GET_LEVEL(ch) < minlevel ) {
		send_to_char(ch, "Your level is too low for this quest.\r\n");
		return false;
	}

	return true;
}

void
Quest_save(std::ostream &out)
{
	const char *indent = "    ";

	out << indent << "<Quest VNUM=\"" << vnum << "\" NAME=\"" << xmlEncodeTmp(name)
        << "\" OWNER=\"" << owner_id << "\" STARTED=\"" << started
        << "\" ENDED=\"" << ended << "\""
        << endl;
	out << indent << indent
        << "MAX_PLAYERS=\"" << max_players << "\" MAX_LEVEL=\"" << maxlevel
        << "\" MIN_LEVEL=\"" << minlevel << "\" MAX_GEN=\"" << maxgen
        << "\" MIN_GEN=\"" << mingen << "\"" << endl;

	out << indent << indent
		<< "AWARDED=\"" << awarded
		<< "\" PENALIZED=\"" << penalized
		<< "\" TYPE=\"" << xmlEncodeTmp(tmp_strdup(qtype_abbrevs[type]))
		<< "\" OWNER_LEVEL=\"" << owner_level
		<< "\" FLAGS=\"" << flags
		<< "\" LOADROOM=\"" << loadroom << "\">" << endl;

	if (description)
		out << indent << "  <Description>"
            << xmlEncodeTmp(description)
            << "</Description>" << endl;

	out << indent << "  <Update>" << xmlEncodeTmp(updates) << "</Update>" << endl;

	for( unsigned int i = 0; i < players.size(); i++ ) {
		out << indent
            << "  <Player ID=\"" << players[i].idnum << '"'
            << " FLAGS=\"" << players[i].flags << '"'
            << " DEATHS=\"" << players[i].deaths << '"'
            << " MKILLS=\"" << players[i].mobkills << '"'
            << " PKILLS=\"" << players[i].pkills << '"'
            << "/>" << endl;
	}
	for( unsigned int i = 0; i < bans.size(); i++ ) {
		out << indent << "  <Ban ID=\"" << bans[i].idnum
            << "\" FLAGS=\"" << bans[i].getFlags() << "\" />" << endl;
	}
	out << indent << "</Quest>" << endl;
}

void
Quest_tallyDeath(int player)
{
    getPlayer((long)player).deaths += 1;
    quests.save();
}
void
Quest_tallyMobKill(int player) {
    getPlayer((long)player).mobkills += 1;
    quests.save();
}
void
Quest_tallyPlayerKill(int player) {
    getPlayer((long)player).pkills += 1;
    quests.save();
}