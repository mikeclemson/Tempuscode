//
// File: tester_util.spec                     -- Part of TempusMUD
//
// Copyright 1998 by John Watson, all rights reserved.
//

const char *tester_util_cmds[] = {
  "advance",
  "unaffect",
  "reroll",
  "stat",
  "goto",
  "restore",          /* 5 */
  "class",
  "race",
  "remort",
  "maxhit",
  "maxmana",          /* 10 */
  "maxmove",
  "nohassle",
  "roomflags",
  "align",
  "generation",
  "debug",
  "str",
  "int",
  "wis",
  "con",
  "dex",
  "cha",
  "maxstat",
  "\n"
};

#define TESTER_UTIL_USAGE \
"Options are:\r\n"        \
"advance <level>\r\n"   \
"unaffect\r\n"          \
"reroll\r\n"            \
"stat\r\n"              \
"goto\r\n"              \
"restore\r\n"           \
"class <char_class>\r\n"     \
"race <race>\r\n"       \
"remort <char_class>\r\n"    \
"maxhit <value>\r\n"    \
"maxmana <value>\r\n"   \
"maxmove <value>\r\n"   \
"nohassle\r\n"          \
"roomflags\r\n"         \
"align\r\n"             \
"generation\r\n"        \
"debug\r\n"                \
"str|con|int|wis|dex|cha <val>\r\n"

SPECIAL(tester_util)
{

  ACMD(do_wizutil);
  ACMD(do_goto);
  ACMD(do_set);
  ACMD(do_gen_tog);
  ACMD(do_stat);
  void do_start(struct char_data *ch, int mode);
  struct obj_data *obj = (struct obj_data *) me;
  char arg1[MAX_INPUT_LENGTH], arg2[MAX_INPUT_LENGTH];
  byte tcmd;
  int i;

  return 0;
  if (!cmd || !PLR_FLAGGED(ch, PLR_TESTER) || !CMD_IS("tester"))
    return 0;

  two_arguments(argument, arg1, arg2);
  
  if (!*arg1) {
    act("$p: ERROR: Activate what?", FALSE, ch, obj, 0, TO_CHAR);
    send_to_char(TESTER_UTIL_USAGE, ch);
    return 1;
  }
  
  if ((tcmd = search_block(arg1, tester_util_cmds, FALSE)) < 0) {
    sprintf(buf, "$p: Invalid command '%s'.", arg1);
    act(buf, FALSE, ch, obj, 0, TO_CHAR);
    send_to_char(TESTER_UTIL_USAGE, ch);
    return 1;
  }

	switch (tcmd) {
		case 0:                    /* advance */
			if (!*arg2)
				send_to_char("Advance to what level?\r\n", ch);
			else if (!is_number(arg2))
				send_to_char("The argument must be a number.\r\n", ch);
			else if ((i = atoi(arg2)) <= 0)
				send_to_char("That's not a level!\r\n", ch);
			else if (i >= LVL_AMBASSADOR)
				act("$p: Advance: I DON'T THINK SO!", FALSE, ch, obj, 0, TO_CHAR);
			else {
				if (i < GET_LEVEL(ch)) {
					do_start(ch, TRUE);
					GET_LEVEL(ch) = i;
				  } 
			  
				  act("$p hums and blinks for a moment.... you feel different!", FALSE,
				  ch, obj, 0, TO_CHAR);
				  gain_exp_regardless(ch, exp_scale[i] - GET_EXP(ch));

				  for (i = 1; i < MAX_SKILLS; i++) {
					  if (spell_info[i].min_level[(int)GET_CLASS(ch)] <= GET_LEVEL(ch) 
					  || (IS_REMORT(ch) 
					  &&  spell_info[i].min_level[(int)CHECK_REMORT_CLASS(ch)] <= GET_LEVEL(ch))) {
							GET_SKILL(ch, i) = LEARNED(ch);
					  }
				  }
				  GET_HIT(ch) = GET_MAX_HIT(ch);
				  GET_MANA(ch) = GET_MAX_MANA(ch);
				  GET_MOVE(ch) = GET_MAX_MOVE(ch);
				  save_char(ch, NULL);
			}
			break;
		case 1:                    /* unaffect */
			do_wizutil(ch, "me", 0, SCMD_UNAFFECT);
			break;
		case 2:                    /* reroll */
			do_wizutil(ch, "me", 0, SCMD_REROLL);
			break;
		case 3:                    /* stat */
			do_stat(ch, arg2, 0, 0);
			break;
		case 4:                    /* goto */
			do_goto(ch, arg2, 0, 0);
			break;
		case 5:                    /* restore */
			GET_HIT(ch) = GET_MAX_HIT(ch);
			GET_MANA(ch) = GET_MAX_MANA(ch);
			GET_MOVE(ch) = GET_MAX_MOVE(ch);
			send_to_char("You are fully healed!\r\n", ch);
			break;
		case 6:                    /* char_class  */
		case 7:                    /* race   */
		case 8:                    /* remort */
		case 9:                    /* maxhit */
		case 10:                   /* maxmana */
		case 11:                   /* maxmove */
			sprintf(buf, "me %s %s", arg1, arg2);
			do_set(ch, buf, 0, 0);
			break;
		case 12:
			do_gen_tog(ch, "", 0, SCMD_NOHASSLE);
			break;
		case 13:
			do_gen_tog(ch, "", 0, SCMD_ROOMFLAGS);
			break;
		case 14:
			if (!*arg2)
			  send_to_char("Set align to what?\r\n", ch);
			else {
			  GET_ALIGNMENT(ch) = atoi(arg2);
			  sprintf(buf, "Align set to %d.\r\n", GET_ALIGNMENT(ch));
			  send_to_char(buf, ch);
			}
			break;
		case 15:
			if (!*arg2)
			  send_to_char("Set gen to what?\r\n", ch);
			else {
			  GET_REMORT_GEN(ch) = atoi(arg2);
			  sprintf(buf, "gen set to %d.\r\n", GET_REMORT_GEN(ch));
			  send_to_char(buf, ch);
			}
			break;
		case 16:
			do_gen_tog(ch, "", 0, SCMD_DEBUG);
			break;
		case 17: // strength
		case 18: // intelligence
		case 19: // wisdom
		case 20: // constitution
		case 21: // dexterity
		case 22: // charisma
			sprintf(buf, "me %s %s", arg1, arg2);
			do_set(ch, buf, 0, 0);
			break;
		case 23: // Max Stats
			do_set(ch, "me str 25", 0, 0);
			do_set(ch, "me int 25", 0, 0);
			do_set(ch, "me wis 25", 0, 0);
			do_set(ch, "me con 25", 0, 0);
			do_set(ch, "me dex 25", 0, 0);
			do_set(ch, "me cha 25", 0, 0);
			send_to_char("Stat's Maxed.\r\n",ch);
			break;
		default:
			sprintf(buf, "$p: Invalid command '%s'.", arg1);
			send_to_char(TESTER_UTIL_USAGE, ch);
			break;
    }
	return 1;
}











