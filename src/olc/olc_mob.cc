//
// File: olc_mob.c                     -- Part of TempusMUD
//
// Copyright 1998 by John Watson, all rights reserved.
//


#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include "structs.h"
#include "utils.h"
#include "comm.h"
#include "interpreter.h"
#include "char_class.h"
#include "handler.h"
#include "db.h"
#include "olc.h"
#include "screen.h"
#include "flow_room.h"
#include "specs.h"

extern struct zone_data *zone_table;
extern struct descriptor_data *descriptor_list;
extern struct char_data *character_list;
extern struct player_special_data dummy_mob;
extern int top_of_zone_table;
extern int olc_lock;
extern int top_of_mobt;
extern int *obj_index;
extern int *mob_index;
extern int *shp_index;

long  asciiflag_conv(char *buf);

char *one_argument_no_lower(char *argument, char *first_arg);
int   search_block_no_lower(char *arg, char **list, bool exact);
int   fill_word_no_lower(char *argument);
void  num2str(char *str, int num);
void  clear_char(struct char_data *ch);
void  set_physical_attribs(struct char_data *ch);
void  do_stat_character(struct char_data *ch, struct char_data *k);
struct extra_descr_data *locate_exdesc(char *word, struct extra_descr_data *list);
char *find_exdesc(char *word, struct extra_descr_data *list);
int   get_line_count(char *buffer);

SPECIAL(shop_keeper);

// locals
static char  arg1[MAX_INPUT_LENGTH], arg2[MAX_INPUT_LENGTH];

const char *olc_mset_keys[] = {
    "alias",
    "name",
    "ldesc",
    "desc",
    "flags",
    "flags2",          /** 5 **/
    "aff",
    "aff2",
    "aff3",
    "align",
    "str",            /** 10 **/
    "int",
    "wis",
    "dex",
    "con",
    "cha",            /** 15 **/
    "level",
    "hitp_mod",
    "hitd_num",
    "hitd_size",
    "mana",        /** 20 **/
    "move",
    "baredam",
    "baredsize",
    "gold",
    "exp",       /** 25 **/
    "attacktype",
    "position",
    "sex",
    "remort_class",
    "cash",        /** 30 **/
    "hitroll",
    "damroll",
    "ac",
    "class",
    "race",         /** 35 **/
    "dpos",
    "stradd",
    "height",
    "weight",
    "reply",         /** 40 **/
    "special",
    "morale",
    "str_app",
    "move_buf",
    "lair",         /** 45 **/
    "leader",
    "\n"

};

#define NUM_MSET_COMMANDS 45


struct char_data *
do_create_mob(struct char_data *ch, int vnum)
{
    struct char_data *mob = NULL, *new_mob = NULL;
    struct zone_data *zone = NULL;
    int j;

    if ((mob = real_mobile_proto(vnum))) {
	send_to_char("ERROR: Mobile already exists.\r\n", ch);
	return NULL;
    }

    for (zone = zone_table; zone; zone = zone->next)
	if (vnum >= zone->number * 100 && vnum <= zone->top)
	    break;
  
    if (!zone) {
	send_to_char("ERROR: A zone must be defined for the mobile first.\r\n",
		     ch);
	return NULL;
    }

    if (!CAN_EDIT_ZONE(ch, zone)) {
	send_to_char("Try creating mobiles in your own zone, luser.\r\n", ch);
	sprintf(buf, "OLC: %s failed attempt to CREATE mob %d.",
		GET_NAME(ch), vnum);
	mudlog(buf, BRF, GET_INVIS_LEV(ch), TRUE);
	return NULL;
    }

    if ( ! OLC_EDIT_OK( ch, zone, ZONE_MOBS_APPROVED ) ) {
	send_to_char("Mobile OLC is not approved for this zone.\r\n", ch);
	return NULL;
    }

    for (mob = mob_proto; mob; mob = mob->next)
	if (vnum > mob->mob_specials.shared->vnum && 
	    (!mob->next || vnum < mob->next->mob_specials.shared->vnum))
	    break;

  
    CREATE(new_mob, struct char_data, 1);
  
    clear_char(new_mob);
  
    CREATE(new_mob->mob_specials.shared, struct mob_shared_data, 1);
    new_mob->mob_specials.shared->vnum = vnum;
    new_mob->mob_specials.shared->number = 0;
    new_mob->mob_specials.shared->func = NULL;
    new_mob->mob_specials.shared->proto = new_mob;
  
    new_mob->player_specials = &dummy_mob;

    /***** String data *** */
    new_mob->player.name = str_dup("Fresh Blank Mobile");
    new_mob->player.short_descr = str_dup("A Fresh Blank Mobile");
    new_mob->player.long_descr = str_dup("A Fresh Blank Mobile is here waiting to be violated.\n");
    new_mob->player.description = NULL;
    new_mob->player.title = NULL;

    /* *** Numeric data *** */
    MOB_FLAGS(new_mob) = 0;
    MOB2_FLAGS(new_mob) = 0;
    SET_BIT(MOB_FLAGS(new_mob), MOB_ISNPC);
    AFF_FLAGS(new_mob) = 0;
    AFF2_FLAGS(new_mob) = 0;
    AFF3_FLAGS(new_mob) = 0;
    GET_ALIGNMENT(new_mob) = 0;


    new_mob->real_abils.str = 11;
    new_mob->real_abils.intel = 11;
    new_mob->real_abils.wis = 11;
    new_mob->real_abils.dex = 11;
    new_mob->real_abils.con = 11;
    new_mob->real_abils.cha = 11;

    GET_LEVEL(new_mob) = 1;
    new_mob->points.hitroll = 0;
    new_mob->points.armor = 0;

    /* max hit = 0 is a flag that H, M, V is xdy+z */
    new_mob->points.max_hit = 0;
    new_mob->points.hit = 10;
    new_mob->points.mana = 10;
    new_mob->points.move = 50;

    new_mob->points.max_mana = 10;
    new_mob->points.max_move = 50;

    new_mob->mob_specials.shared->damnodice = 5;
    new_mob->mob_specials.shared->damsizedice = 2;
    new_mob->points.damroll = 0;

    new_mob->player.char_class = CLASS_NORMAL;
    new_mob->player.race  = RACE_MOBILE;

    GET_GOLD(new_mob) = 0;
    GET_EXP(new_mob) = 100;
    GET_MORALE(new_mob) = 100;
    GET_MOB_LAIR(new_mob) = -1;
    GET_MOB_LEADER(new_mob) = -1;

    new_mob->mob_specials.shared->attack_type = 0;

    new_mob->char_specials.position = POS_STANDING;
    new_mob->mob_specials.shared->default_pos = POS_STANDING;
    new_mob->player.sex = SEX_NEUTRAL;

    new_mob->player.weight = 200;
    new_mob->player.height = 198;
    
    new_mob->player.remort_char_class = -1;
    set_physical_attribs(new_mob);

    for (j = 0; j < 3; j++)
	GET_COND(new_mob, j) = -1;

    for (j = 0; j < 5; j++)
	GET_SAVE(new_mob, j) = 0;

    new_mob->mob_specials.response = NULL;
    new_mob->aff_abils = new_mob->real_abils;

    for (j = 0; j < NUM_WEARS; j++)
	new_mob->equipment[j] = NULL;

    new_mob->desc = NULL;
    new_mob->next = NULL;
    new_mob->in_room = NULL;
   
    if (mob) {
	new_mob->next = mob->next;
	mob->next = new_mob;
    } else {
	new_mob->next = NULL;
	mob_proto = new_mob;

    }

    top_of_mobt++;
  
    return(new_mob);
}


void do_mob_medit(struct char_data *ch, char *argument)
{
    struct char_data *mobile = NULL, *tmp_mob = NULL;
    struct zone_data *zone = NULL;
    struct descriptor_data *d = NULL;
    int j;
   
    mobile = GET_OLC_MOB(ch);
   
    if (!*argument) {
	if (!mobile)
	    send_to_char("You are not currently editing a mobile.\r\n", ch);
	else {
	    sprintf(buf, "Current olc mobile: [%5d] %s\r\n",
		    mobile->mob_specials.shared->vnum, mobile->player.short_descr);
	    send_to_char(buf, ch);
	}
	return;
    }
    if (!is_number(argument)) {
	if (is_abbrev(argument, "exit")) {
	    send_to_char("Exiting mobile editor.\r\n", ch);
	    GET_OLC_MOB(ch) = NULL;
	    return;
	}
	send_to_char("The argument must be a number.\r\n", ch);
	return;
    } else {
	j = atoi(argument);
	if ((tmp_mob = real_mobile_proto(j)) == NULL)
	    send_to_char("There is no such mobile.\r\n", ch);
	else {
	    for (zone = zone_table; zone; zone = zone->next)
		if (j <= zone->top)
		    break;
	    if (!zone) {
		send_to_char("That mobile does not belong to any zone!!\r\n", ch);
		slog("SYSERR: mobile not in any zone.");
		return;
	    }
	
	    if (!CAN_EDIT_ZONE(ch, zone)) {
		send_to_char("You do not have permission to edit those mobiles.\r\n",ch);
		return;
	    }

	    if ( ! OLC_EDIT_OK( ch, zone, ZONE_MOBS_APPROVED ) ) {
		send_to_char("Mobile OLC is not approved for this zone.\r\n", ch);
		return;
	    }
      
	    for (d = descriptor_list; d; d = d->next) {
		if (d->character && GET_OLC_MOB(d->character) == tmp_mob) {
		    act("$N is already editing that mobile.",FALSE,ch,0,d->character,
			TO_CHAR);
		    return;
		}
	    }

	    GET_OLC_MOB(ch) = tmp_mob;
	    sprintf(buf, "Now editing mobile [%d] %s%s%s\r\n",
		    tmp_mob->mob_specials.shared->vnum,
		    CCGRN(ch, C_NRM), tmp_mob->player.short_descr,
		    CCNRM(ch, C_NRM));
	    send_to_char(buf, ch);
	}
    }
}

void do_mob_mstat(struct char_data *ch)
{
    struct char_data *mob = NULL;
   
    mob = GET_OLC_MOB(ch);
   
    if (!mob) 
	send_to_char("You are not currently editing a mobile.\r\n", ch);
    else
	do_stat_character(ch, mob);
}

#define mob_p GET_OLC_MOB(ch)
#define OLC_REPLY_USAGE "olc mset reply  <create | remove | edit | addkey>" \
"<keywords> [new keywords]\r\n"

void do_mob_mset(struct char_data *ch, char *argument)
{
    struct extra_descr_data *reply, *nreply, *temp;
    struct char_data *tmp_mob;
    struct zone_data *zone;
    int i, mset_command, tmp_flags, flag, cur_flags, state;
  
    if (!mob_p) {
	send_to_char("You are not currently editing a mobile.\r\n", ch);
	return;
    }
  
    if (!*argument) {
	strcpy(buf, "Valid mset commands:\r\n");
	strcat(buf, CCYEL(ch, C_NRM));
	i = 0;
	while (i < NUM_MSET_COMMANDS) {
	    strcat(buf, olc_mset_keys[i]);
	    strcat(buf, "\r\n");
	    i++;
	}
	strcat(buf, CCNRM(ch, C_NRM));
	page_string(ch->desc, buf, 1);
	return;
    }
  
    half_chop(argument, arg1, arg2);
    skip_spaces(&argument);
  
    if ((mset_command = search_block(arg1, olc_mset_keys, FALSE))<0) {
	sprintf(buf, "Invalid mset command '%s'.\r\n", arg1);
	send_to_char(buf, ch);
	return;
    } 
    if (mset_command != 3 && !*arg2) {
	sprintf(buf, "Set %s to what??\r\n", olc_mset_keys[mset_command]);
	send_to_char(buf, ch);
	return;
    }

#ifdef DMALLOC
    dmalloc_verify(0);
#endif
  
    switch (mset_command) {
    case 0:                 /** Alias **/
	if (mob_p->player.name)
	    free(mob_p->player.name);
	mob_p->player.name = strdup(arg2);
	UPDATE_MOBLIST(mob_p, tmp_mob, ->player.name);
	send_to_char("Mobile aliases set.\r\n", ch);
	break;
    case 1:                 /** Name **/
	if (mob_p->player.short_descr)
	    free(mob_p->player.short_descr);
	mob_p->player.short_descr = strdup(arg2);
	UPDATE_MOBLIST_NAMES(mob_p, tmp_mob, ->player.short_descr);
	send_to_char("Mobile name set.\r\n", ch);
	break;
    case 2:                 /** ldesc **/
	if (mob_p->player.long_descr)
	    free(mob_p->player.long_descr);
	if (arg2[0] == '~')
	    mob_p->player.long_descr = NULL;
	else {
	    strcpy(buf, arg2);
	    strcat(buf, "\n");
	    mob_p->player.long_descr = strdup(buf);
	}
	send_to_char("Mobile long description set.\r\n", ch);
	UPDATE_MOBLIST(mob_p, tmp_mob, ->player.long_descr);
	break;
    case 3:                 /** desc **/
	if (mob_p->player.description == NULL) {
	    CREATE(mob_p->player.description, char, MAX_STRING_LENGTH);
	    send_to_char(TED_MESSAGE, ch);
	    act("$n starts to write a mobile description.", TRUE, ch, 0, 0, TO_ROOM);
	    ch->desc->str = &mob_p->player.description;
	    ch->desc->max_str = MAX_STRING_LENGTH;
	    SET_BIT(PLR_FLAGS(ch), PLR_OLC);
	    for (tmp_mob = character_list; tmp_mob; tmp_mob = tmp_mob->next)
		if (GET_MOB_VNUM(tmp_mob) == GET_MOB_VNUM(mob_p))
		    tmp_mob->player.description = NULL;

	}
	else {
	    send_to_char("Use TED to modify the mobile descripition.\r\n", ch);
	    ch->desc->str = &mob_p->player.description;
	    ch->desc->max_str = MAX_STRING_LENGTH;
	    ch->desc->editor_mode = 1;
	    ch->desc->editor_cur_lnum = get_line_count(mob_p->player.description);
	    SET_BIT(PLR_FLAGS(ch), PLR_OLC);
	    act("$n begins to edit a mobile description.", TRUE, ch, 0, 0, TO_ROOM);
	    for (tmp_mob = character_list; tmp_mob; tmp_mob = tmp_mob->next)
		if (GET_MOB_VNUM(tmp_mob) == GET_MOB_VNUM(mob_p))
		    tmp_mob->player.description = NULL;
	}
	break;
    case 4:                /** flags **/
	tmp_flags = 0;
	argument = one_argument(arg2, arg1);

	for (zone = zone_table; zone; zone = zone->next)
	    if (mob_p->mob_specials.shared->vnum >= zone->number * 100 && 
		mob_p->mob_specials.shared->vnum <= zone->top)
		break;

	if (!zone) {
	    slog("SYSERR:  Error!  mobile not in zone.");
	    send_to_char("ERROR\r\n", ch);
	    return;
	}

	if (*arg1 == '+')
	    state = 1;
	else if (*arg1 == '-')
	    state = 2;
	else {
	    send_to_char("Usage: olc mset flags [+/-] [FLAG, FLAG, ...]\r\n", ch);
	    return;
	}
      
	argument = one_argument(argument, arg1);
    
	cur_flags = MOB_FLAGS(mob_p);
    
	while (*arg1) {
	    if ((flag = search_block(arg1, action_bits_desc,FALSE)) == -1) {
		sprintf(buf, "Invalid flag %s, skipping...\r\n", arg1);
		send_to_char(buf, ch);
	    }
	    else if ((1 << flag) == MOB_SPEC && !GET_MOB_SPEC(mob_p) &&
		     state == 1)
		send_to_char("Can't set SPEC bit until special is assigned.\r\n", ch);
	    else
		tmp_flags = tmp_flags|(1 << flag);
      
	    argument = one_argument(argument, arg1);
	}
      
	if (state == 1)
	    cur_flags = cur_flags | tmp_flags;
	else {
	    tmp_flags = cur_flags & tmp_flags;
	    cur_flags = cur_flags ^ tmp_flags;
	}
    
	MOB_FLAGS(mob_p) = cur_flags;
      
	if (tmp_flags == 0 && cur_flags == 0) {
	    SET_BIT(zone->flags, ZONE_MOBS_MODIFIED);
	    SET_BIT(MOB_FLAGS(mob_p), MOB_ISNPC);
	    send_to_char("Mobile flags set to: ISNPC\r\n", ch);
	}
	else if (tmp_flags == 0)
	    send_to_char("Mobile flags not altered.\r\n", ch);
	else {
	    SET_BIT(zone->flags, ZONE_MOBS_MODIFIED);
	    SET_BIT(MOB_FLAGS(mob_p), MOB_ISNPC);
	    send_to_char("Mobile flags set.\r\n", ch);
	}
	break;
    case 5:                 /** flags2 **/
	tmp_flags = 0;
	argument = one_argument(arg2, arg1);

	for (zone = zone_table; zone; zone = zone->next)
	    if (mob_p->mob_specials.shared->vnum >= zone->number * 100 && 
		mob_p->mob_specials.shared->vnum <= zone->top)
		break;

	if (*arg1 == '+')
	    state = 1;
	else if (*arg1 == '-')
	    state = 2;
	else {
	    send_to_char("Usage: olc mset flags2 [+/-] [FLAG, FLAG, ...]\r\n", ch);
	    return;
	}
      
	argument = one_argument(argument, arg1);
    
	cur_flags = MOB2_FLAGS(mob_p);
    
	while (*arg1) {
	    if ((flag = search_block(arg1,action2_bits_desc,FALSE)) == -1) {
		sprintf(buf, "Invalid flag %s, skipping...\r\n", arg1);
		send_to_char(buf, ch);
	    }
	    else 
		tmp_flags = tmp_flags|(1 << flag);
	
	    argument = one_argument(argument, arg1);
	}
      
	if (state == 1)
	    cur_flags = cur_flags | tmp_flags;
	else {
	    tmp_flags = cur_flags & tmp_flags;
	    cur_flags = cur_flags ^ tmp_flags;
	}
    
	MOB2_FLAGS(mob_p) = cur_flags;
      
	if (tmp_flags == 0 && cur_flags == 0) {
	    SET_BIT(zone->flags, ZONE_MOBS_MODIFIED);
	    send_to_char("Mobile flags2 set to: none\r\n", ch);
	}
	else if (tmp_flags == 0)
	    send_to_char("Mobile flags2 not altered.\r\n", ch);
	else {
	    SET_BIT(zone->flags, ZONE_MOBS_MODIFIED);
	    send_to_char("Mobile flags2 set.\r\n", ch);
	    REMOVE_BIT(MOB2_FLAGS(mob_p), MOB2_RENAMED);
	}
	break;
    case 6:                    /** aff **/
	tmp_flags = 0;
	argument = one_argument(arg2, arg1);

	for (zone = zone_table; zone; zone = zone->next)
	    if (mob_p->mob_specials.shared->vnum >= zone->number * 100 && 
		mob_p->mob_specials.shared->vnum <= zone->top)
		break;

	if (*arg1 == '+')
	    state = 1;
	else if (*arg1 == '-')
	    state = 2;
	else {
	    send_to_char("Usage: olc mset aff [+/-] [FLAG, FLAG, ...]\r\n", ch);
	    return;
	}
      
	argument = one_argument(argument, arg1);
    
	cur_flags = AFF_FLAGS(mob_p);
    
	while (*arg1) {
	    if ((flag = search_block(arg1, affected_bits_desc,FALSE)) == -1) {
		sprintf(buf, "Invalid flag %s, skipping...\r\n", arg1);
		send_to_char(buf, ch);
	    }
	    else 
		tmp_flags = tmp_flags|(1 << flag);
	
	    argument = one_argument(argument, arg1);
	}
      
	if (state == 1)
	    cur_flags = cur_flags | tmp_flags;
	else {
	    tmp_flags = cur_flags & tmp_flags;
	    cur_flags = cur_flags ^ tmp_flags;
	}
    
	AFF_FLAGS(mob_p) = cur_flags;
      
	if (tmp_flags == 0 && cur_flags == 0) {
	    SET_BIT(zone->flags, ZONE_MOBS_MODIFIED);
	    send_to_char("Mobile affected flags set to: none\r\n", ch);
	}
	else if (tmp_flags == 0)
	    send_to_char("Mobile affected flags not altered.\r\n", ch);
	else {
	    SET_BIT(zone->flags, ZONE_MOBS_MODIFIED);
	    send_to_char("Mobile affected flags set.\r\n", ch);
	}
	break;
    case 7:                    /** aff2 **/
	tmp_flags = 0;
	argument = one_argument(arg2, arg1);

	for (zone = zone_table; zone; zone = zone->next)
	    if (mob_p->mob_specials.shared->vnum >= zone->number * 100 && 
		mob_p->mob_specials.shared->vnum <= zone->top)
		break;

	if (*arg1 == '+')
	    state = 1;
	else if (*arg1 == '-')
	    state = 2;
	else {
	    send_to_char("Usage: olc mset aff2 [+/-] [FLAG, FLAG, ...]\r\n", ch);
	    return;
	}
      
	argument = one_argument(argument, arg1);
    
	cur_flags = AFF2_FLAGS(mob_p);
    
	while (*arg1) {
	    if ((flag = search_block(arg1, affected2_bits_desc,FALSE)) == -1) {
		sprintf(buf, "Invalid flag %s, skipping...\r\n", arg1);
		send_to_char(buf, ch);
	    }
	    else 
		tmp_flags = tmp_flags|(1 << flag);
	
	    argument = one_argument(argument, arg1);
	}
      
	if (state == 1)
	    cur_flags = cur_flags | tmp_flags;
	else {
	    tmp_flags = cur_flags & tmp_flags;
	    cur_flags = cur_flags ^ tmp_flags;
	}
    
	AFF2_FLAGS(mob_p) = cur_flags;
      
	if (tmp_flags == 0 && cur_flags == 0) {
	    SET_BIT(zone->flags, ZONE_MOBS_MODIFIED);
	    send_to_char("Mobile affected2 flags set to: none\r\n", ch);
	}
	else if (tmp_flags == 0)
	    send_to_char("Mobile affected2 flags not altered.\r\n", ch);
	else {
	    SET_BIT(zone->flags, ZONE_MOBS_MODIFIED);
	    send_to_char("Mobile affected2 flags set.\r\n", ch);
	}
	break;
    case 8:                    /** aff3 **/
	tmp_flags = 0;
	argument = one_argument(arg2, arg1);

	for (zone = zone_table; zone; zone = zone->next)
	    if (mob_p->mob_specials.shared->vnum >= zone->number * 100 && 
		mob_p->mob_specials.shared->vnum <= zone->top)
		break;

	if (*arg1 == '+')
	    state = 1;
	else if (*arg1 == '-')
	    state = 2;
	else {
	    send_to_char("Usage: olc mset aff3 [+/-] [FLAG, FLAG, ...]\r\n", ch);
	    return;
	}
      
	argument = one_argument(argument, arg1);
    
	cur_flags = AFF3_FLAGS(mob_p);
    
	while (*arg1) {
	    if ((flag = search_block(arg1, affected3_bits_desc,FALSE)) == -1) {
		sprintf(buf, "Invalid flag %s, skipping...\r\n", arg1);
		send_to_char(buf, ch);
	    }
	    else 
		tmp_flags = tmp_flags|(1 << flag);
	
	    argument = one_argument(argument, arg1);
	}
      
	if (state == 1)
	    cur_flags = cur_flags | tmp_flags;
	else {
	    tmp_flags = cur_flags & tmp_flags;
	    cur_flags = cur_flags ^ tmp_flags;
	}
    
	AFF3_FLAGS(mob_p) = cur_flags;
      
	if (tmp_flags == 0 && cur_flags == 0) {
	    SET_BIT(zone->flags, ZONE_MOBS_MODIFIED);
	    send_to_char("Mobile affected3 flags set to: none\r\n", ch);
	}
	else if (tmp_flags == 0)
	    send_to_char("Mobile affected3 flags not altered.\r\n", ch);
	else {
	    SET_BIT(zone->flags, ZONE_MOBS_MODIFIED);
	    send_to_char("Mobile affected3 flags set.\r\n", ch);
	}
	break;
    case 9:               /** align **/
	i = atoi(arg2);
	if (i < -1000 || i > 1000)
	    send_to_char("Alignment must be between -1000 and 1000.\r\n", ch);
	else {
	    GET_ALIGNMENT(mob_p) = i;
	    send_to_char("Mobile alignment set.\r\n", ch);
	}
	break;
    case 10:              /** str **/
	i = atoi(arg2);
	if (i < 3 || i > 25)
	    send_to_char("Strength must be between 3 and 25.\r\n", ch);
	else {
	    mob_p->aff_abils.str = mob_p->real_abils.str = i;
	    send_to_char("Mobile strength set.\r\n", ch);
	}
	break;
    case 11:              /** intel **/
	i = atoi(arg2);
	if (i < 3 || i > 25)
	    send_to_char("Intelligence must be between 3 and 25.\r\n", ch);
	else {
	    mob_p->aff_abils.intel = mob_p->real_abils.intel = i;
	    send_to_char("Mobile intelligence set.\r\n", ch);
	}
	break;
    case 12:             /** wis **/
	i = atoi(arg2);
	if (i < 3 || i > 25)
	    send_to_char("Wisdom must be between 3 and 25.\r\n", ch);
	else {
	    mob_p->aff_abils.wis = mob_p->real_abils.wis = i;
	    send_to_char("Mobile wisdom set.\r\n", ch);
	}
	break;
    case 13:             /** dex **/
	i = atoi(arg2);
	if (i < 3 || i > 25)
	    send_to_char("Dexterity must be between 3 and 25.\r\n", ch);
	else {
	    mob_p->aff_abils.dex = mob_p->real_abils.dex = i;
	    send_to_char("Mobile dexterity set.\r\n", ch);
	}
	break;
    case 14:             /** con **/
	i = atoi(arg2);
	if (i < 3 || i > 25)
	    send_to_char("Constitution must be between 3 and 25.\r\n", ch);
	else {
	    mob_p->aff_abils.con = mob_p->real_abils.con = i;
	    send_to_char("Mobile constitution set.\r\n", ch);
	}
	break;
    case 15:             /** cha **/
	i = atoi(arg2);
	if (i < 3 || i > 25)
	    send_to_char("Charisma must be between 3 and 25.\r\n", ch);
	else {
	    mob_p->aff_abils.cha = mob_p->real_abils.cha = i;
	    send_to_char("Mobile charisma set.\r\n", ch);
	}
	break;
    case 16:            /** level **/
	i = atoi(arg2);
	if (i < 1 || i > 50)
	    send_to_char("Level must be between 1 and 50.\r\n", ch);
	else {
	    GET_LEVEL(mob_p) = i;
	    send_to_char("Mobile level set.\r\n", ch);

	    set_physical_attribs(mob_p);
	    GET_HIT(mob_p) = MOB_D1(i);
	    GET_MANA(mob_p) = MOB_D2(i);
	    GET_MOVE(mob_p) = MOB_MOD(i);
	    GET_AC(mob_p) = (100 - (i * 3));
	    mob_p->mob_specials.shared->damnodice = ((i*i+3) >> 7) + 1;
	    mob_p->mob_specials.shared->damsizedice =  ((i+1) >> 3) + 3;
	    GET_DAMROLL(mob_p)  = i >> 1;
      
	}
	break;
    case 17:            /** hitp_mod **/ 
	i = atoi(arg2);
	if (i < 0 || i > 32767)
	    send_to_char("Hit point modifier must be between 1 and 32767.\r\n", ch);
	else {
	    GET_MOVE(mob_p) = i;
	    send_to_char("Mobile hit point mod set.\r\n", ch);
	}
	break;
    case 18:            /** hitd_num **/ 
	i = atoi(arg2);
	if (i < 0 || i > 200)
	    send_to_char("Hit point dice number must be between 1 and 200.\r\n", ch);
	else {
	    GET_HIT(mob_p) = i;
	    send_to_char("Mobile hit point dice number set.\r\n", ch);
	}
	break;
    case 19:            /** hitd_size **/ 
	i = atoi(arg2);
	if (i < 0 || i > 200)
	    send_to_char("Hit point dice size must be between 1 and 200.\r\n", ch);
	else {
	    GET_MANA(mob_p) = i;
	    send_to_char("Mobile hit point dice size set.\r\n", ch);
	}
	break;

    case 20:            /** mana **/
	i = atoi(arg2);
	if (i < 0 || i > 32767)
	    send_to_char("Mana must be bewteen 1 and 32767.\r\n", ch);
	else {
	    GET_MAX_MANA(mob_p) = i;
	    send_to_char("Mobile mana set.\r\n", ch);
	}
	break;
    case 21:            /** move **/
	i = atoi(arg2);
	if (i < 0 || i > 32767)
	    send_to_char("Movement must be between 1 and 32767.\r\n", ch);
	else {
	    GET_MAX_MOVE(mob_p) = i;
	    send_to_char("Mobile movement set.\r\n", ch);
	}
	break;
    case 22:            /** baredam **/
	i = atoi(arg2);
	if ( i < 1 || i > 125)
	    send_to_char("Bare hand damage must be between 1 and 125.\r\n", ch);
	else {
	    mob_p->mob_specials.shared->damnodice = i;
	    send_to_char("Mobile bare handed damage set.\r\n", ch);
	}
	break;
    case 23:            /** baredsize **/
	i = atoi(arg2);
	if (i < 1 || i > 125)
	    send_to_char("Bare handed damage dice size must be between 1 and 125.\r\n", ch);
	else {
	    mob_p->mob_specials.shared->damsizedice = i;
	    send_to_char("Mobile damage dice size set.\r\n", ch);
	}
	break;
    case 24:            /** gold **/
	i = atoi(arg2);
	if (i < 0 || i > 10000000)
	    send_to_char("Gold must be between 0 and 10,000,000.\r\n", ch);
	else {
	    GET_GOLD(mob_p) = i;
	    send_to_char("Mobile gold set.\r\n", ch);
	}
	break;
    case 25:            /** exp **/
	i = atoi(arg2);
	if (i < 0 || i > 200000000)
	    send_to_char("Experience must be between 0 and 200,000,000.\r\n", ch);
	else {
	    GET_EXP(mob_p) = i;
	    send_to_char("Mobile experience set.\r\n", ch);
	}
	break;
    case 26:            /** attack **/
	if ((i = search_block(arg2, attack_type, FALSE))<0) {
	    sprintf(buf, "Invalid attack type, '%s'.\r\n", arg2);
	    send_to_char(buf, ch);
	}
	else {
	    mob_p->mob_specials.shared->attack_type = i;
	    send_to_char("Mobile attack type set.\r\n", ch);
	}
	break;
    case 27:            /** position **/
	if ((i = search_block(arg2, position_types, FALSE))<0) {
	    sprintf(buf, "Invalid position, '%s'.\r\n", arg2);
	    send_to_char(buf, ch);
	}
	else {
	    GET_POS(mob_p) = i;
	    send_to_char("Mobile position set.\r\n", ch);
	}
	break;
    case 28:            /** sex **/
	if ((i = search_block(arg2, genders, FALSE))<0) {
	    sprintf(buf, "Invalid gender, '%s'.\r\n", arg2);
	    send_to_char(buf, ch);
	}
	else {
	    mob_p->player.sex = i;
	    send_to_char("Mobile gender set.\r\n", ch);
	}
	break;
    case 29:            /** remortchar_class **/
	if (!strncmp(arg2, "none", 4))
	    i = -1;
	else if ((i = search_block(arg2, pc_char_class_types, FALSE))<0) {
	    sprintf(buf, "Invalid char_class type, '%s'.\r\n", arg2);
	    send_to_char(buf, ch);
	    break;
	}
	mob_p->player.remort_char_class = i;
	send_to_char("Mobile Remort Class set.\r\n", ch);
	break;
    case 30:            /** cash **/
	i = atoi(arg2);
	if (i < 0 || i > 1000000)
	    send_to_char("Cash must be between 0 and 1,000,000.\r\n", ch);
	else {
	    GET_CASH(mob_p) = i;
	    send_to_char("Mobile cash credits set.\r\n", ch);
	}
	break;
    case 31:            /** hitroll **/
	i = atoi(arg2);
	if (i < -125 || i > 125)
	    send_to_char("Hitroll must be between -125 and 125.\r\n", ch);
	else {
	    GET_HITROLL(mob_p) = i;
	    send_to_char("Mobile hitroll set.\r\n", ch);
	}
	break;
    case 32:            /** damroll **/
	i = atoi(arg2);
	if (i < -125 || i > 125)
	    send_to_char("Damroll must be between -125 and 125.\r\n", ch);
	else {
	    GET_DAMROLL(mob_p) = i;
	    send_to_char("Mobile damroll set.\r\n", ch);
	}
	break;
    case 33:            /** ac **/
	i = atoi(arg2);
	if (i < -500 || i > 100)
	    send_to_char("Armor Class must be between -500 and 100.\r\n", ch);
	else {
	    GET_AC(mob_p) = i;
	    send_to_char("Mobile Armor Class set.\r\n", ch);
	}
	break;
    case 34:            /** char_class **/
	if ((i = search_block(arg2, pc_char_class_types, FALSE))<0) {
	    sprintf(buf, "Invalid char_class type, '%s'.\r\n", arg2);
	    send_to_char(buf, ch);
	}
	else {
	    GET_CLASS(mob_p) = i;
	    set_physical_attribs(mob_p);
	    send_to_char("Mobile Class set.\r\n", ch);
	}
	break;
    case 35:            /** race **/
	if ((i = search_block(arg2, player_race, FALSE))<0) {
	    sprintf(buf, "Invalid race, '%s'.\r\n", arg2);
	    send_to_char(buf, ch);
	}
	else {
	    GET_RACE(mob_p) = i;
	    set_physical_attribs(mob_p);
	    send_to_char("Mobile Race set.\r\n", ch);

	}
	break;
    case 36:            /** dpos **/
	if ((i = search_block(arg2, position_types, FALSE))<0) {
	    sprintf(buf, "Invalid default position, '%s'.\r\n", arg2);
	    send_to_char(buf, ch);
	}
	else {
	    GET_DEFAULT_POS(mob_p) = i;
	    send_to_char("Mobile default position set.\r\n", ch);
	}
	break;
    case 37:            /** stradd **/
	i = atoi(arg2);
	if (i < 0 || i > 100)
	    send_to_char("StrAdd must be between 0 and 100.\r\n", ch);
	else {
	    GET_ADD(mob_p) = i;
	    send_to_char("Mobile StrAdd set.\r\n", ch);
	}
	break;
    case 38:            /** height **/
	i = atoi(arg2);
	if (i < 1 || i > 10000)
	    send_to_char("Height must be between 1 and 10000.\r\n", ch);
	else {
	    GET_HEIGHT(mob_p) = i;
	    send_to_char("Mobile height set.\r\n", ch);
	}
	break;
    case 39:            /** weight **/
	i = atoi(arg2);
	if (i < 1 || i > 50000)
	    send_to_char("Weight must be between 1 and 50000.\r\n", ch);
	else {
	    GET_WEIGHT(mob_p) = i;
	    send_to_char("Mobile weight set.\r\n", ch);
	}
	break;
    case 40:              /* reply */
	if (!*argument) {
	    send_to_char(OLC_REPLY_USAGE, ch);
	    return;
	}
	half_chop(arg2, buf, argument);
	if (!mob_p)
	    send_to_char("Hey punk, you need an mobile in your editor first!!!\r\n",
			 ch);
	else if (!*argument)
	    send_to_char("Which reply would you like to deal with?\r\n",
			 ch);
	else if (!*buf)
	    send_to_char("Valid commands are: create, remove, edit, addkey.\r\n",
			 ch);
	else if (is_abbrev(buf, "remove")) {
	    if ((reply = locate_exdesc(argument,mob_p->mob_specials.response))) {
		REMOVE_FROM_LIST(reply, mob_p->mob_specials.response, next);
		if (reply->keyword)
		    free(reply->keyword);
		else
		    slog("WTF?? !reply->keyword??");

		if (reply->description)
		    free(reply->description);
		else
		    slog("WTF?? !reply->description??");

		free(reply);

		send_to_char("Response removed.\r\n", ch);
		UPDATE_MOBLIST(mob_p, tmp_mob, ->mob_specials.response);
	    } else
		send_to_char("No response.\r\n", ch);
       
	    return;
	} else if (is_abbrev(buf, "create")) {
	    if (find_exdesc(argument, mob_p->mob_specials.response)) {
		send_to_char("A response already exists with that keyword.\r\n"
			     "Use the 'olc mset reply remove' command to remove it, or the\r\n"
			     "'olc mset reply edit' command to change it, punk.\r\n", ch);
		return;
	    }
	    CREATE(nreply, struct extra_descr_data, 1); 
	    nreply->keyword  = str_dup(argument);
	    nreply->next = mob_p->mob_specials.response;
	    mob_p->mob_specials.response = nreply;
	    send_to_char(TED_MESSAGE, ch);
	    ch->desc->str = &mob_p->mob_specials.response->description;
	    ch->desc->max_str = MAX_STRING_LENGTH;
	    SET_BIT(PLR_FLAGS(ch), PLR_OLC);
	    act("$n begins to write a mobile response.", TRUE, ch, 0, 0, TO_ROOM);
	    return;
	} else if (is_abbrev(buf, "edit")) {
	    if ((reply = locate_exdesc(argument,mob_p->mob_specials.response))) {
		send_to_char("Use TED to modify the response.\r\n", ch);
		ch->desc->str = &reply->description;
		ch->desc->max_str = MAX_STRING_LENGTH;
		ch->desc->editor_mode = 1;
		ch->desc->editor_cur_lnum = get_line_count(reply->description);
		SET_BIT(PLR_FLAGS(ch), PLR_OLC);
		act("$n begins to edit a mobile response.", TRUE, ch, 0, 0, TO_ROOM);
	    } else
		send_to_char("No such response.  Use 'create' to make a new one.\r\n", ch);

	    return;
	} else if (is_abbrev(buf, "addkeyword")) {
	    half_chop(argument, arg1, arg2);
	    if ((reply = locate_exdesc(arg1,mob_p->mob_specials.response))) {
		if (!*arg2) {
		    send_to_char("What??  How about giving me some keywords to add...\r\n", ch);
		    return;
		} else {
		    strcpy(buf, reply->keyword);
		    strcat(buf, " ");
		    strcat(buf, arg2);
		    free(reply->keyword);
		    reply->keyword = str_dup(buf);
		    UPDATE_MOBLIST(mob_p, tmp_mob, ->mob_specials.response);
		    send_to_char("Keywords added.\r\n", ch);
		    return;
		}
	    } else
		send_to_char("There is no such response for this mobile.\r\n", ch);
	} else
	    send_to_char(OLC_REPLY_USAGE, ch);
	break;
    case 41:
	if (!*arg2 || (i = find_spec_index_arg(arg2)) < 0)
	    send_to_char("That is not a valid special.\r\n"
			 "Type show special mob to view a list.\r\n", ch);
	else if (!IS_SET(spec_list[i].flags, SPEC_MOB))
	    send_to_char("This special is not for mobiles.\r\n", ch);
	else if (IS_SET(spec_list[i].flags, SPEC_RES) && !OLCGOD(ch) && !OLCIMP(ch))
	    send_to_char("This special is reserved.\r\n", ch);
	else {
      
	    mob_p->mob_specials.shared->func = spec_list[i].func;
	    do_specassign_save(ch, SPEC_MOB);
	    send_to_char("Mobile special set.\r\n", ch);
	}
	break;

    case 42:           /*** morale ***/
	i = atoi(arg2);
	if (i < 0 || i > 125)
	    send_to_char("Morale must be between 1 and 125.\r\n", ch);
	else {
	    mob_p->mob_specials.shared->morale = i;
	    send_to_char("Mobile morale set.\r\n", ch);
	}
	break;

    case 43:              /** str_app **/
	i = atoi(arg2);
	if (mob_p->real_abils.str != 18) {
	    send_to_char("The mob must have an 18 strength.\r\n", ch);
	    return;
	}
	if (i < 0 || i > 100)
	    send_to_char("Strength apply must be between 0 and 100.\r\n", ch);
	else {
	    mob_p->aff_abils.str_add = mob_p->real_abils.str_add = i;
	    send_to_char("Mobile strength apply set.\r\n", ch);
	}
	break;

    case 44:                 /** move_buf **/
	if (MOB_SHARED(mob_p)->move_buf)
	    free(MOB_SHARED(mob_p)->move_buf);
	MOB_SHARED(mob_p)->move_buf = strdup(arg2);
	send_to_char("Mobile move_buf set.\r\n", ch);
	break;

    case 45:       /** lair **/
	i = atoi(arg2);
	mob_p->mob_specials.shared->lair = i;
	send_to_char("Mobile lair set.\r\n", ch);
	break;

    case 46:       /** leader **/
	i = atoi(arg2);
	mob_p->mob_specials.shared->leader = i;
	send_to_char("Mobile leader set.\r\n", ch);
	break;

    default:
	break;
    } 
  
#ifdef DMALLOC
    dmalloc_verify(0);
#endif
    if (mset_command != 25) /*mob exp */
	GET_EXP(mob_p) = mobile_experience(mob_p);
    if (!OLCIMP(ch))
	SET_BIT(MOB2_FLAGS(mob_p), MOB2_UNAPPROVED);

}

int write_mob_index(struct char_data *ch, struct zone_data *zone)
{
    int done = 0, i, j, found = 0, count = 0, *new_index;
    char fname[64];
    FILE *index;

    for (i = 0; mob_index[i] != -1; i++) {
	count++;
	if (mob_index[i] == zone->number) {
	    found = 1;
	    break;
	}
    }
  
    if (found == 1)
	return(1);
  
    CREATE(new_index, int, count+2);
  
    for (i = 0, j = 0;; i++) {
	if (mob_index[i] == -1) {
	    if (done == 0) {
		new_index[j] = zone->number;
		new_index[j+1] = -1;
	    }
	    else
		new_index[j] = -1;
	    break;
	}
	if (mob_index[i] > zone->number && done != 1) {
	    new_index[j] = zone->number;
	    j++;
	    new_index[j] = mob_index[i];
	    done = 1;
	}
	else
	    new_index[j] = mob_index[i];
	j++;
    }
  
    free(mob_index);
  
#ifdef DMALLOC
    dmalloc_verify(0);
#endif
  
    mob_index = new_index;
  
    sprintf(fname,"world/mob/index");
    if (!(index = fopen(fname, "w"))) {
	send_to_char("Could not open index file, mobile save aborted.\r\n", ch);
	return(0);
    }

    for (i = 0; mob_index[i] != -1; i++)
	fprintf(index, "%d.mob\n", mob_index[i]);

    fprintf(index, "$\n");
  
    send_to_char("Mobile index file re-written.\r\n", ch);
  
    fclose(index);
  
    return(1);
}

int 
save_mobs(struct char_data *ch)
{
    int                     m_vnum, espec_mob = 0;
    unsigned int            i, tmp;
    room_num                low  = 0;
    room_num                high = 0;
    char                    fname[64];
    char                    sbuf1[64], sbuf2[64], sbuf3[64], sbuf4[64], sbuf5[64];
    struct extra_descr_data *reply;
    struct zone_data *zone;
    struct char_data  *mob;
    FILE                    *file;
    FILE                    *realfile;
  
    if (GET_OLC_MOB(ch)) {
	mob = GET_OLC_MOB(ch);
	m_vnum = mob->mob_specials.shared->vnum;
	for (zone = zone_table; zone; zone = zone->next)
	    if (m_vnum >= zone->number*100 && m_vnum <= zone->top)
		break;
	if (!zone) {
	    sprintf(buf, "OLC: ERROR finding zone for mobile %d.", m_vnum);
	    slog(buf);
	    send_to_char("Unable to match mobile with zone error..\r\n", ch);
	    return 1;
	}
    } else
	zone = ch->in_room->zone;

    sprintf(fname,"world/mob/olc/%d.mob", zone->number);
    if (!(file = fopen(fname,"w")))
	return 1;
  
    if ((write_mob_index(ch, zone)) != 1) {
	fclose(file);
	return(1);
    }

    low  = zone->number * 100;
    high = zone->top;

    for (mob = mob_proto; mob; mob = mob->next) {
	if (mob->mob_specials.shared->vnum < low)
	    continue;
	if (mob->mob_specials.shared->vnum > high)
	    break;

	if (mob->mob_specials.shared->attack_type != 0 ||
	    GET_STR(mob) != 11 ||
	    GET_ADD(mob) != 0 ||
	    GET_INT(mob) != 11 ||
	    GET_WIS(mob) != 11 ||
	    GET_DEX(mob) != 11 ||
	    GET_CON(mob) != 11 ||
	    GET_CHA(mob) != 11 ||
	    GET_MAX_MANA(mob) != 10 ||
	    GET_MAX_MOVE(mob) != 50 ||
	    GET_WEIGHT(mob) != 200 ||
	    GET_HEIGHT(mob) != 198 ||
	    GET_MORALE(mob) != 100 ||
	    GET_MOB_LAIR(mob) >= 0 ||
	    GET_MOB_LEADER(mob) >= 0 ||
	    GET_REMORT_CLASS(mob) != -1 ||
	    GET_CASH(mob) != 0 ||
	    MOB_SHARED(mob)->move_buf)
	    espec_mob = 1;

	fprintf(file, "#%d\n", mob->mob_specials.shared->vnum);    
    
	if (mob->player.name)
	    fprintf(file, "%s", mob->player.name);
	fprintf(file, "~\n");
    
	if (mob->player.short_descr)
	    fprintf(file, "%s", mob->player.short_descr);
	fprintf(file, "~\n");
    
	if (mob->player.long_descr) {
	    tmp = strlen(mob->player.long_descr);
	    for (i = 0; i < tmp; i++)
		if (mob->player.long_descr[i] != '\r' &&
		    mob->player.long_descr[i] != '~')
		    fputc(mob->player.long_descr[i], file);
	}
	fprintf(file, "~\n");
    
	if (mob->player.description) {
	    tmp = strlen(mob->player.description);
	    for (i = 0; i < tmp; i++)
		if (mob->player.description[i] != '\r' &&
		    mob->player.description[i] != '~')
		    fputc(mob->player.description[i], file);
	}
	fprintf(file, "~\n");
    
	REMOVE_BIT(MOB_FLAGS(mob), MOB_WIMPY);
	num2str(sbuf1, MOB_FLAGS(mob));
	num2str(sbuf2, MOB2_FLAGS(mob));
	num2str(sbuf3, AFF_FLAGS(mob));
	num2str(sbuf4, AFF2_FLAGS(mob));
	num2str(sbuf5, AFF3_FLAGS(mob));
    
	fprintf(file, "%s %s %s %s %s %d ", sbuf1, sbuf2, sbuf3,
		sbuf4, sbuf5,
		GET_ALIGNMENT(mob));
    
	if (espec_mob == 1)
	    fprintf(file, "E\n");
	else
	    fprintf(file, "S\n");

	fprintf(file, "%d %d %d %dd%d+%d %dd%d+%d\n", 
		GET_LEVEL(mob),
		(20 - mob->points.hitroll),
		mob->points.armor / 10,
		mob->points.hit,
		mob->points.mana,
		mob->points.move,
		mob->mob_specials.shared->damnodice,
		mob->mob_specials.shared->damsizedice,
		mob->points.damroll);
    
	fprintf(file, "%d %d %d %d\n", GET_GOLD(mob),
		GET_EXP(mob),
		GET_RACE(mob),
		GET_CLASS(mob));
    
	fprintf(file, "%d %d %d %d\n", GET_POS(mob),
		GET_DEFAULT_POS(mob),
		GET_SEX(mob),
		mob->mob_specials.shared->attack_type);
	    
	if (espec_mob == 1) {
	    if (GET_STR(mob) != 11)
		fprintf(file, "Str: %d\n", GET_STR(mob));
	    if (GET_ADD(mob) != 0)
		fprintf(file, "StrAdd: %d\n", GET_ADD(mob));
	    if (GET_INT(mob) != 11)
		fprintf(file, "Int: %d\n", GET_INT(mob));
	    if (GET_WIS(mob) != 11)
		fprintf(file, "Wis: %d\n", GET_WIS(mob));
	    if (GET_DEX(mob) != 11)
		fprintf(file, "Dex: %d\n", GET_DEX(mob));
	    if (GET_CON(mob) != 11)
		fprintf(file, "Con: %d\n", GET_CON(mob));
	    if (GET_CHA(mob) != 11)
		fprintf(file, "Cha: %d\n", GET_CHA(mob));
	    if (GET_MAX_MANA(mob) != 10)
		fprintf(file, "MaxMana: %d\n", GET_MAX_MANA(mob));
	    if (GET_MAX_MOVE(mob) != 50)
		fprintf(file, "MaxMove: %d\n", GET_MAX_MOVE(mob));
	    if (GET_WEIGHT(mob) != 200)
		fprintf(file, "Weight: %d\n", GET_WEIGHT(mob));
	    if (GET_HEIGHT(mob) != 198)
		fprintf(file, "Height: %d\n", GET_HEIGHT(mob));
	    if (GET_CASH(mob) != 0)
		fprintf(file, "Cash: %d\n", GET_CASH(mob));
	    if (GET_MORALE(mob) != 100)
		fprintf(file, "Morale: %d\n", GET_MORALE(mob));
	    if (GET_MOB_LAIR(mob) >= 0)
		fprintf(file, "Lair: %d\n", GET_MOB_LAIR(mob));
	    if (GET_MOB_LEADER(mob) >= 0)
		fprintf(file, "Leader: %d\n", GET_MOB_LEADER(mob));
	    if (GET_REMORT_CLASS(mob) != -1)
		fprintf(file, "RemortClass: %d\n", GET_REMORT_CLASS(mob));
	    if (MOB_SHARED(mob)->move_buf)
		fprintf(file, "Move_buf: %s\n", MOB_SHARED(mob)->move_buf);
	    fprintf(file, "E\n");      
	}

	reply = mob->mob_specials.response;
	while (reply != NULL)  {
	    if (!reply->keyword || !reply->description)  {
		sprintf(buf,"OLCERROR: Response with no kywrd or desc, mob #%d.\n",
			mob->mob_specials.shared->vnum);
		slog(buf);
		sprintf(buf, "I didn't save your bogus response for mob %d.\r\n",
			mob->mob_specials.shared->vnum);
		send_to_char(buf, ch);
		reply = reply->next;
		continue;
	    }
	    fprintf(file,"R\n");
	    fprintf(file,"%s~\n",reply->keyword);
	    tmp = strlen(reply->description);
	    for(i=0;i<tmp;i++)
		if (reply->description[i] != '\r')
		    fputc(reply->description[i],file);
	    fprintf(file,"~\n");
	    reply = reply->next;
	}
    }

    fprintf(file,"$\n");

    sprintf(buf, "OLC: %s msaved %d.", GET_NAME(ch), zone->number);
    slog(buf);

    sprintf(fname,"world/mob/%d.mob", zone->number);
    realfile = fopen(fname,"w");
    if (realfile)  {
	fclose (file);
	sprintf(fname,"world/mob/olc/%d.mob", zone->number);
	if (!(file = fopen(fname,"r")))  {
	    slog("SYSERR: Failure to reopen olc mob file.");
	    send_to_char("OLC Error: Failure to duplicate mob file in main dir."
			 "\r\n", ch);
	    fclose(realfile);
	    return 1;
	}
	do  {
	    tmp = fread (buf, 1, 512, file);
	    if (fwrite (buf, 1, tmp, realfile) != tmp)  {
		slog("SYSERR: Failure to duplicate olc mob file in the main wld dir.");
		send_to_char("OLC Error: Failure to duplicate mob file in main dir."
			     "\r\n", ch);
		fclose(realfile);
		fclose(file);
		return 1;
	    }
	} while (tmp == 512);

	fclose (realfile);

    }
    fclose(file);

    REMOVE_BIT(zone->flags, ZONE_MOBS_MODIFIED);
    return 0;
}


int 
do_destroy_mobile(struct char_data *ch, int vnum)
{

    struct zone_data *zone = NULL;
    struct char_data *mob = NULL, *temp = NULL, *next_mob = NULL;
    struct extra_descr_data *resp = NULL;
    struct descriptor_data *d = NULL;
    struct memory_rec_struct *mem_r = NULL;

    if (!(mob = real_mobile_proto(vnum))) {
	send_to_char("ERROR: That mobile does not exist.\r\n", ch);
	return 1;
    }

    for (zone = zone_table; zone; zone = zone->next)
	if (vnum < zone->top)
	    break;

    if (!zone) {
	send_to_char("That mobile does not belong to any zone!!\r\n", ch);
	slog("SYSERR: mobile not in any zone.");
	return 1;
    }

    if (GET_IDNUM(ch) != zone->owner_idnum && GET_LEVEL(ch) < LVL_LUCIFER) {
	send_to_char("Oh, no you dont!!!\r\n", ch);
	sprintf(buf, "OLC: %s failed attempt to DESTROY mobile %d.",
		GET_NAME(ch), GET_MOB_VNUM(mob));
	mudlog(buf, BRF, GET_INVIS_LEV(ch), TRUE);
	return 1;
    }

    for (temp = character_list; temp; temp = next_mob) {
	next_mob = temp->next;
	if (GET_MOB_VNUM(temp) == GET_MOB_VNUM(mob))
	    extract_char(temp, FALSE);
    }

    REMOVE_FROM_LIST(mob, mob_proto, next);

    for (d = descriptor_list; d; d = d->next) {
	if (d->character && GET_OLC_MOB(d->character) == mob) {
	    GET_OLC_MOB(d->character) = NULL;
	    send_to_char("The mobile you were editing has been destroyed!\r\n", 
			 d->character);
	    break;
	}
    }

    if (GET_NAME(mob)) {
	free(GET_NAME(mob));
    }
    if (mob->player.title) {
	free(mob->player.title);
    }
    if (mob->player.short_descr) {
	free(mob->player.short_descr);
    }
    if (mob->player.long_descr) {
	free(mob->player.long_descr);
    }
    if (mob->player.description) {
	free(mob->player.description);
    }
#ifdef DMALLOC
    dmalloc_verify(0);
#endif
  
    while ((resp = mob->mob_specials.response)) {
	mob->mob_specials.response = resp->next;
	if (resp->keyword)
	    free(resp->keyword);
	if (resp->description)
	    free(resp->description);
	free (resp);
    }
#ifdef DMALLOC
    dmalloc_verify(0);
#endif
    mob->mob_specials.shared->proto = NULL;
    if (mob->mob_specials.shared->move_buf) {
	free(mob->mob_specials.shared->move_buf);
    }
    if (mob->mob_specials.mug) {
	free(mob->mob_specials.mug);
    }
    while ((mem_r = mob->mob_specials.memory)) {
	mob->mob_specials.memory = mem_r->next;
	free(mem_r);
    }
    
    if (mob->mob_specials.shared) {
	free(mob->mob_specials.shared);
    }
    top_of_mobt--;
    free(mob);
#ifdef DMALLOC
    dmalloc_verify(0);
#endif
    return 0;
}

int 
mobile_experience(struct char_data *mob)
{

    int exp = 0, tmp = 0;

    exp = 13 * ((GET_LEVEL(mob) * GET_LEVEL(mob) + 24) >> 3);
    tmp = exp_scale[(int) MIN(GET_LEVEL(mob) + 1, LVL_GRIMP)];
    tmp += (tmp * GET_LEVEL(mob)) / 100;
    exp = tmp / MAX(1, exp);
    exp = MAX(10, exp);

    if (GET_MAX_HIT(mob))
	exp += (exp * GET_MAX_HIT(mob)) / 
	    MAX(1, GET_LEVEL(mob) * GET_LEVEL(mob) * 50);
    else 
	exp += (exp * (((GET_HIT(mob)*(GET_MANA(mob) + 1)) / 2) + GET_MOVE(mob))) /
	    MAX(1, GET_LEVEL(mob) * GET_LEVEL(mob) * 50);

    exp += (exp * mob->mob_specials.shared->damnodice *
	    (mob->mob_specials.shared->damsizedice + 1)) / 120;

    if (IS_MAGE(mob) || IS_CLERIC(mob) || IS_LICH(mob))
	exp += (exp * GET_MAX_MANA(mob)) / 15000;
    if (IS_KNIGHT(mob) || IS_BARB(mob) || IS_RANGER(mob) || IS_WARRIOR(mob))
	exp += (exp * GET_MAX_MOVE(mob)) / 15000;
    if (IS_THIEF(mob))
	exp += (exp * GET_LEVEL(mob)) / 150;
    if (GET_CLASS(mob) == CLASS_ARCH || IS_DRAGON(mob))
	exp = (int) ( exp * 1.7 );
    else if (IS_DEVIL(mob) || IS_SLAAD(mob))
	exp = (int) ( exp * 1.4 );
    else if (NON_CORPOREAL_UNDEAD(mob))
	exp = (int) ( exp * 1.3 );
    else if (IS_GIANT(mob))
	exp = (int) ( exp * 1.2 );
    else if (IS_TROLL(mob))
	exp *= 2;

    exp += (exp * (GET_STR(mob) - 11)) / 20;
    exp += (exp * (GET_DEX(mob) - 11)) / 23;
    exp += (exp * (GET_INT(mob) - 11)) / 27;
    exp += (exp * (GET_WIS(mob) - 11)) / 29;

    exp += (exp * (GET_DAMROLL(mob))) / 200;
    exp += (exp * (GET_HITROLL(mob))) / 300;

    exp -= (exp * (MAX(-200, GET_AC(mob)) - 100)) / 200;
  
    if (MOB_FLAGGED(mob, MOB_NOBASH))
	exp = (int) ( exp * 1.2 );
    exp += (exp * GET_MORALE(mob)) / 300;

    if (AFF_FLAGGED(mob, AFF_INVISIBLE))
	exp = (int) ( exp * 1.25 );
    if (AFF_FLAGGED(mob, AFF_SENSE_LIFE))
	exp = (int) ( exp * 1.1 );
    if (AFF_FLAGGED(mob, AFF_SANCTUARY | AFF_NOPAIN))
	exp = (int) ( exp * 1.3 );
    if (AFF_FLAGGED(mob, AFF_ADRENALINE))
	exp = (int) ( exp * 1.1 );
    if (AFF_FLAGGED(mob, AFF_CONFIDENCE))
	exp = (int) ( exp * 1.1 );
    if (AFF_FLAGGED(mob, AFF_REGEN) || IS_TROLL(mob))
	exp = (int) ( exp * 1.3 );
    if (AFF_FLAGGED(mob, AFF_BLUR))
	exp = (int) ( exp * 1.2 );
    if (AFF2_FLAGGED(mob, AFF2_TRANSPARENT))
	exp = (int) ( exp * 1.2 );
    if (AFF2_FLAGGED(mob, AFF2_SLOW))
	exp = (int) ( exp * 0.7 );
    if (AFF2_FLAGGED(mob, AFF2_HASTE))
	exp = (int) ( exp * 1.4 );
    if (AFF2_FLAGGED(mob, AFF2_FIRE_SHIELD))
	exp = (int) ( exp * 1.25 );
    if (AFF2_FLAGGED(mob, AFF2_TRUE_SEEING))
	exp = (int) ( exp * 1.3 );
    if (AFF2_FLAGGED(mob, AFF2_DISPLACEMENT))
	exp = (int) ( exp * 1.25 );
    if (AFF2_FLAGGED(mob, AFF2_BLADE_BARRIER))
	exp = (int) ( exp * 1.3 );
    if (AFF2_FLAGGED(mob, AFF2_ENERGY_FIELD))
	exp = (int) ( exp * 1.15 );
    if (AFF3_FLAGGED(mob, AFF3_PRISMATIC_SPHERE))
	exp = (int) ( exp * 1.3 );

    exp = (exp / 10) * 10;

    exp = MAX(0, exp);
    return (exp);
}
  
int
olc_mimic_mob(struct char_data *ch, 
	      struct char_data *orig, struct char_data *targ, int mode)
{
  
    struct char_data *mob = NULL, *next_mob = NULL;

    if (mode) {  /* (mode) => mimicing prototype... else real mob */
	for (mob = character_list; mob; mob = next_mob) {
	    next_mob = mob->next;
	    if (IS_NPC(mob) && GET_MOB_VNUM(mob) == GET_MOB_VNUM(targ))
		extract_char(mob, FALSE);
	}
    }

    if (targ->mob_specials.shared->func != shop_keeper) {
	targ->mob_specials.shared->func = orig->mob_specials.shared->func;
	do_specassign_save(ch, SPEC_MOB);
    }

#ifdef DMALLOC
    dmalloc_verify(0);
#endif
    if (targ->player.name)
	free(targ->player.name);
    targ->player.name = strdup(orig->player.name);

    if (targ->player.short_descr)
	free(targ->player.short_descr);
    targ->player.short_descr = strdup(orig->player.short_descr);

    if (targ->player.long_descr)
	free(targ->player.long_descr);
    if (orig->player.long_descr)
	targ->player.long_descr = strdup(orig->player.long_descr);
    else
	targ->player.long_descr = NULL;

    if (targ->player.description)
	free(targ->player.description);
    if (orig->player.description)
	targ->player.description = strdup(orig->player.description);
    else
	targ->player.description = NULL;
#ifdef DMALLOC
    dmalloc_verify(0);
#endif
    MOB_FLAGS(targ)     = MOB_FLAGS(orig);
    MOB2_FLAGS(targ)    = MOB2_FLAGS(orig);
    AFF_FLAGS(targ)     = AFF_FLAGS(orig);
    AFF2_FLAGS(targ)    = AFF2_FLAGS(orig);
    AFF3_FLAGS(targ)    = AFF3_FLAGS(orig);
    GET_ALIGNMENT(targ) = GET_ALIGNMENT(orig);


    GET_LEVEL(targ) = GET_LEVEL(orig);
    targ->points.hitroll = orig->points.hitroll;
    targ->points.armor = orig->points.armor;

    /* max hit = 0 is a flag that H, M, V is xdy+z */
    targ->points.max_hit = orig->points.max_hit;
    targ->points.hit = orig->points.hit;
    targ->points.mana = orig->points.mana;
    targ->points.move = orig->points.move;

    targ->points.max_mana = orig->points.max_mana;
    targ->points.max_move = orig->points.max_move;

    targ->mob_specials.shared->damnodice = orig->mob_specials.shared->damnodice;
    targ->mob_specials.shared->damsizedice = 
	orig->mob_specials.shared->damsizedice;
    targ->points.damroll = orig->points.damroll;

    targ->player.char_class = orig->player.char_class;
    targ->player.race  = orig->player.race;

    GET_GOLD(targ) = GET_GOLD(orig);
    GET_EXP(targ) = GET_EXP(orig);

    targ->mob_specials.shared->attack_type = 
	orig->mob_specials.shared->attack_type;

    targ->char_specials.position = orig->char_specials.position;
    targ->mob_specials.shared->default_pos = 
	orig->mob_specials.shared->default_pos;
    targ->player.sex = orig->player.sex;

    targ->player.weight = orig->player.weight;
    targ->player.height = orig->player.height;
    
    targ->player.remort_char_class = orig->player.remort_char_class;

    targ->aff_abils.str = orig->aff_abils.str;
    targ->aff_abils.str_add = orig->aff_abils.str_add;
    targ->aff_abils.intel = orig->aff_abils.intel;
    targ->aff_abils.wis = orig->aff_abils.wis;
    targ->aff_abils.dex = orig->aff_abils.dex;
    targ->aff_abils.con = orig->aff_abils.con;
    targ->aff_abils.cha = orig->aff_abils.cha;

    targ->real_abils.str = orig->real_abils.str;
    targ->real_abils.str_add = orig->real_abils.str_add;
    targ->real_abils.intel = orig->real_abils.intel;
    targ->real_abils.wis = orig->real_abils.wis;
    targ->real_abils.dex = orig->real_abils.dex;
    targ->real_abils.con = orig->real_abils.con;
    targ->real_abils.cha = orig->real_abils.cha;

    return 0;

}
