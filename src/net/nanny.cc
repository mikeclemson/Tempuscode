/* ************************************************************************
*   File: nanny.cc                                      Part of CircleMUD *
*  Usage: parse user commands, search for specials, call ACMD functions   *
*                                                                         *
*  All rights reserved.  See license.doc for complete information.        *
*                                                                         *
*  Copyright (C) 1993, 94 by the Trustees of the Johns Hopkins University *
*  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
************************************************************************ */

//
// File: nanny.cc                           -- Part of TempusMUD
//
// All modifications and additions are
// Copyright 1998 by John Watson, all rights reserved.
//

#define __nanny_c__

#include <arpa/telnet.h>
#include "structs.h"
#include "comm.h"
#include "interpreter.h"
#include "db.h"
#include "utils.h"
#include "spells.h"
#include "handler.h"
#include "mail.h"
#include "screen.h"
#include "help.h"
#include "char_class.h"
#include "clan.h"
#include "vehicle.h"
#include "house.h"
#include "login.h"
#include "matrix.h"
#include "bomb.h"
#include "security.h"
#include "quest.h"

extern char *motd;
extern char *ansi_motd;
extern char *imotd;
extern char *ansi_imotd;
extern char *background;
extern char *MENU;
extern char *WELC_MESSG;
extern char *START_MESSG;
extern struct player_index_element *player_table;
extern int top_of_p_table;
extern int restrict;
extern int num_of_houses;
extern int mini_mud;
extern int mud_moved;
extern int log_cmds;
extern struct spell_info_type spell_info[];
extern struct house_control_rec house_control[];
extern int shutdown_count;


// external functions
ACMD(do_hcollect_help);
void do_start(struct Creature * ch, int mode);
void show_mud_date_to_char(struct Creature *ch);
void show_programs_to_char(struct Creature *ch, int char_class);
void handle_network(struct descriptor_data *d,char *arg);
int general_search(struct Creature *ch, struct special_search_data *srch, int mode);
void roll_real_abils(struct Creature * ch);
void print_attributes_to_buf(struct Creature *ch, char *buff);
void polc_input(struct descriptor_data * d, char *str);

int isbanned(char *hostname, char *blocking_hostname);
int Valid_Name(char *newname);
int _parse_name(char *arg, char *name);
int reserved_word(char *argument);
char *diag_conditions(struct Creature *ch);


// internal functions
void set_desc_state(int state,struct descriptor_data *d );
void echo_on(struct descriptor_data * d);
void echo_off(struct descriptor_data * d);

void notify_cleric_moon(struct Creature *ch);

// deal with newcomers and other non-playing sockets
void
nanny(struct descriptor_data * d, char *arg)
{
    char buf[MAX_STRING_LENGTH];
	char tmp_name[MAX_INPUT_LENGTH];
    int player_i, load_result=0;
    struct char_file_u tmp_store;
    struct Creature *tmp_ch;
    struct descriptor_data *k, *next;
    extern struct descriptor_data *descriptor_list;
    extern int max_bad_pws;
    struct room_data *load_room = NULL, *h_rm = NULL, *rm = NULL;
    struct clan_data *clan = NULL;
    struct clanmember_data *member = NULL;
    struct obj_data *obj = NULL, *next_obj;
    int percent, i, j, cur, rooms;
    int polc_char = 0;

    int load_char(char *name, struct char_file_u * char_element);
	int parse_player_class(char *arg, int timeframe);
    int parse_time_frame(char *arg);

    skip_spaces(&arg);

    switch (STATE(d)) {
		case CON_GET_NAME:        // wait for input of name
			if( mud_moved ) {
				set_desc_state( CON_CLOSE,d );
				return;
			}
			if (d->character == NULL) {
				CREATE(d->character, struct Creature, 1);
				clear_char(d->character);
				CREATE(d->character->player_specials, struct player_special_data, 1);
				GET_LOADROOM(d->character) = -1;
				GET_HOLD_LOADROOM(d->character) = -1;
				d->character->desc = d;
			}
			if (!*arg)
				close_socket(d);
			if (!strcasecmp(arg, "new")) {
				set_desc_state(CON_NAME_PROMPT, d);
				return;
			}
			//
			// port olc name
			//

			else if (strlen(arg) > 7 && !strncasecmp(arg,"polc-",5)) {
				strcpy(tmp_name,arg+5);
				if ((player_i = load_char(tmp_name, &tmp_store)) == -1) {
					SEND_TO_Q("Invalid Port OLC name, please try another.\r\n", d);
					return;
				}
				sprintf(tmp_store.name,"polc-%s",tmp_store.name);
				store_to_char(&tmp_store, d->character);
				GET_PFILEPOS(d->character) = player_i;

				if (!PLR_FLAGGED(d->character, PLR_POLC)) {
					free_char (d->character);
					if ((player_i = load_char(tmp_name, &tmp_store)) == -1) {
						SEND_TO_Q("Invalid Port OLC name, please try another.\r\n", d);
						return;
					}
				}

				REMOVE_BIT(PLR_FLAGS(d->character), PLR_WRITING | PLR_MAILING | PLR_OLC | PLR_QUESTOR);
				set_desc_state( CON_PASSWORD,d );
			}

			//
			// normal name
			//

			if ( ( _parse_name(arg, tmp_name)) || strlen(tmp_name) < 2 ||
				strlen(tmp_name) > MAX_NAME_LENGTH  || strlen(tmp_name) < 3 ) {
				SEND_TO_Q("\r\nInvalid name, please try another.\r\n", d);
				return;
			}

			if ( fill_word(strcpy(buf, tmp_name)) || reserved_word(buf)) {
				SEND_TO_Q( "\r\nReserved name, please try another.\r\n", d);
				return;
			}

			player_i = load_char(tmp_name, &tmp_store);
			if (player_i == -1 || tmp_store.char_specials_saved.act & PLR_DELETED) {
				SEND_TO_Q("\r\nThat character does not exist.  Please try another.\r\n", d);
				return;
			}

			store_to_char(&tmp_store, d->character);
			// set this up for the dyntext check
			d->old_login_time = tmp_store.last_logon;

			GET_PFILEPOS(d->character) = player_i;

			/* Obsolete------------------------------------
			if (PLR_FLAGGED(d->character, PLR_DELETED)) 
				free_char(d->character);
				CREATE(d->character, struct Creature, 1);
				clear_char(d->character);
				CREATE(d->character->player_specials, struct player_special_data, 1);
				d->character->desc = d;
				CREATE(d->character->player.name, char, strlen(tmp_name) + 1);
				strcpy(d->character->player.name, CAP(tmp_name));
				GET_PFILEPOS(d->character) = player_i;
				GET_WAS_IN(d->character) = NULL;
				Crash_delete_file(GET_NAME(d->character), CRASH_FILE);
				Crash_delete_file(GET_NAME(d->character), IMPLANT_FILE);

				set_desc_state( CON_NAME_CNFRM,d );
			----------------------------------------------*/
				// undo it just in case they are set
			REMOVE_BIT(PLR_FLAGS(d->character),
					   PLR_WRITING | PLR_MAILING | PLR_OLC |
					   PLR_QUESTOR);
			REMOVE_BIT(PRF2_FLAGS(d->character), PRF2_WORLDWRITE);

			if(d->character->getLevel() < LVL_AMBASSADOR) {
				GET_QLOG_LEVEL(d->character) = 0;
			}

			// make sure clan is valid
			if ((clan = real_clan(GET_CLAN(d->character)))) {
				if (!(member = real_clanmember(GET_IDNUM(d->character), clan)))
					GET_CLAN(d->character) = 0;
			} else if (GET_CLAN(d->character))
				GET_CLAN(d->character) = 0;

			set_desc_state( CON_PASSWORD,d );
			break;
		case CON_NAME_PROMPT:		// new character making
			// player unknown -- make new character
			if ( ( _parse_name(arg, tmp_name)) || strlen(tmp_name) < 2 ||
				strlen(tmp_name) > MAX_NAME_LENGTH  || strlen(tmp_name) < 3 ) {
				SEND_TO_Q("\r\nInvalid name, please try another.\r\n", d);
				return;
			}

			if ( fill_word(strcpy(buf, tmp_name)) || reserved_word(buf)) {
				SEND_TO_Q( "\r\nReserved name, please try another.\r\n", d);
				return;
			}

			if (!Valid_Name(tmp_name)) {
				SEND_TO_Q("\r\nInvalid name, please use another.\r\n", d);
				return;
			}

			player_i = load_char(tmp_name, &tmp_store);
			if (player_i != -1 && !(tmp_store.char_specials_saved.act & PLR_DELETED)) {
				SEND_TO_Q("\r\nThat character name is already in use.  Please try another.\r\n", d);
				return;
			}

			CREATE(d->character->player.name, char, strlen(tmp_name) + 1);
			strcpy(d->character->player.name, CAP(tmp_name));
			GET_PFILEPOS(d->character) = player_i;
			GET_WAS_IN(d->character) = NULL;
			Crash_delete_file(GET_NAME(d->character), CRASH_FILE);
			Crash_delete_file(GET_NAME(d->character), IMPLANT_FILE);
			set_desc_state( CON_NAME_CNFRM,d );
			break;
		case CON_NAME_CNFRM:        // wait for conf. of new name
			if (tolower(*arg) == 'y') {
				if (isbanned(d->host, buf2) >= BAN_NEW) {
					mudlog(LVL_GOD, NRM, true,
						"Request for new char %s denied from [%s] (siteban)",
						GET_NAME(d->character), d->host);
					SEND_TO_Q("Sorry, new characters are not allowed from your site!\r\n", d);
					set_desc_state( CON_CLOSE,d );
					return;
				}
				if (restrict) {
					SEND_TO_Q("Sorry, new players can't be created at the moment.\r\n", d);
					mudlog(LVL_GOD, NRM, true,
						"Request for new char %s denied from %s (wizlock)",
						GET_NAME(d->character), d->host);
					set_desc_state( CON_CLOSE,d );
					return;
				}
				mudlog(LVL_GOD, NRM, true,
					"New player [%s] connect from %s.", GET_NAME(d->character),
					d->host);

				sprintf(buf,"Creating new character '%s'.\r\n\r\n",
						GET_NAME(d->character));
				SEND_TO_Q(buf, d);
				set_desc_state( CON_NEWPASSWD,d );
			} else if (*arg == 'n' || *arg == 'N') {
				free(d->character->player.name);
				d->character->player.name = NULL;
				set_desc_state( CON_GET_NAME,d );
			} else {
				SEND_TO_Q("Please type Yes or No.\r\n", d);
			}
			break;
		case CON_PASSWORD:        // get pwd for known player
			// turn echo back on
			echo_on(d);

			if (!*arg)
				close_socket(d);
			else {
				if (strncmp(CRYPT(arg, GET_PASSWD(d->character)),
							GET_PASSWD(d->character), MAX_PWD_LENGTH)) {
					mudlog(MAX(LVL_GOD,
							PLR_FLAGGED(d->character, PLR_INVSTART) ?
							GET_LEVEL(d->character) :
							GET_INVIS_LVL(d->character)), CMP, true,
						"Bad PW: %s [%s]", GET_NAME(d->character), d->host);

					GET_BAD_PWS(d->character)++;
					save_char(d->character, real_room(GET_LOADROOM(d->character)));
					if (++(d->bad_pws) >= max_bad_pws) {    // 3 strikes and you're out.
						SEND_TO_Q("Wrong password... disconnecting.\r\n", d);
						set_desc_state( CON_CLOSE,d );
					} else {
						SEND_TO_Q("Wrong password.\r\n", d);
						echo_off(d);
					}
					return;
				}
				load_result = GET_BAD_PWS(d->character);
				GET_BAD_PWS(d->character) = 0;

				if (!strncasecmp(GET_NAME(d->character),"polc-",5))
					polc_char = TRUE;

				if (!polc_char)
					save_char(d->character, real_room(GET_LOADROOM(d->character)));

				if (isbanned(d->host, buf2) == BAN_SELECT &&
					!PLR_FLAGGED(d->character, PLR_SITEOK)) {
					SEND_TO_Q("Sorry, this char has not been cleared for login from your site!\r\n", d);
					set_desc_state( CON_CLOSE,d );
					mudlog(LVL_GOD, NRM, true,
						"Connection attempt for %s denied from %s",
						GET_NAME(d->character), d->host);
					return;
				}
				if (GET_LEVEL(d->character) < restrict &&
					(d->character->isTester() || restrict > LVL_ETERNAL)) {
					SEND_TO_Q("The game is temporarily restricted.. try again later.\r\n", d);
					set_desc_state( CON_CLOSE,d );
					mudlog(LVL_GOD, NRM, true,
						"Request for login denied for %s [%s] (wizlock)",
						GET_NAME(d->character), d->host);
					return;
				}
				// first, check for other logins
				for (k = descriptor_list; k; k = next) {
					next = k->next;
					if (k != d && k->character && !k->character->in_room &&
						GET_IDNUM(k->character) == GET_IDNUM(d->character) &&
						!strcmp(GET_NAME(k->character),GET_NAME(d->character))) {
						slog("socket close on multi login (%s)",
								GET_NAME(d->character));
						close_socket(k);
					}
				}
				// check to see if this is a port olc descriptor

				/*
				 * next, check to see if this person is already logged in, but
				 * switched.  If so, disconnect the switched persona.
				 */
				for (k = descriptor_list; k; k = k->next) {
					if (k->original &&
						(GET_IDNUM(k->original) == GET_IDNUM(d->character))) {
						SEND_TO_Q("Disconnecting for return to unswitched char.\r\n", k);
						STATE(k) = CON_CLOSE;
						free_char(d->character);
						d->character = k->original;
						d->character->desc = d;
						d->original = NULL;
						d->character->char_specials.timer = 0;
						if (k->character)
							k->character->desc = NULL;
						k->character = NULL;
						k->original = NULL;
						SEND_TO_Q("Reconnecting to unswitched char.\r\n", d);
						REMOVE_BIT(PLR_FLAGS(d->character), PLR_MAILING | PLR_WRITING | PLR_OLC);
						set_desc_state( CON_PLAYING,d );

						if (shutdown_count > 0) {
							sprintf(buf,
									"\r\n\007\007:: NOTICE :: Tempus will be rebooting in [%d] second%s ::\r\n",
									shutdown_count, shutdown_count == 1 ? "" : "s");
							send_to_char(d->character, "%s", buf);
						}

						if (has_mail(GET_IDNUM(d->character)))
							SEND_TO_Q("You have mail waiting.\r\n", d);

						notify_cleric_moon(d->character);

						mudlog(MAX(LVL_GOD, GET_INVIS_LVL(d->character)), NRM,
							true,
							"%s [%s] has reconnected.",
							GET_NAME(d->character), d->host);
						return;
					}
				}

				// now check for linkless and usurpable
				CreatureList::iterator cit = characterList.begin();
				for ( ; cit != characterList.end(); ++cit ) {
					tmp_ch = *cit;
					if (!IS_NPC(tmp_ch) &&
						GET_IDNUM(d->character) == GET_IDNUM(tmp_ch) &&
						!strcmp(GET_NAME(tmp_ch),GET_NAME(d->character))) {
						if (!tmp_ch->desc) {
							SEND_TO_Q("Reconnecting.\r\n", d);

							if (shutdown_count > 0) {
								sprintf(buf,
										"\r\n\007\007:: NOTICE :: Tempus will be rebooting in [%d] second%s ::\r\n",
										shutdown_count, shutdown_count == 1 ? "" : "s");
								send_to_char(d->character, "%s", buf);
							}

							mudlog(MAX(LVL_GOD, GET_INVIS_LVL(d->character)), NRM, true,
								"%s [%s] has reconnected.", GET_NAME(d->character),
								d->host);
							if (!polc_char) {
								if (GET_WAS_IN(tmp_ch)) {
									if (tmp_ch->in_room) {
										act("$n is jerked back to reality!", TRUE, tmp_ch, 0, 0, TO_ROOM);
										char_from_room(tmp_ch,false);
									}
									char_to_room(tmp_ch, GET_WAS_IN(tmp_ch),false);
									GET_WAS_IN(tmp_ch) = NULL;
								}
								act("$n has reconnected.", TRUE, tmp_ch, 0, 0, TO_ROOM);
								if (has_mail(GET_IDNUM(d->character)))
									SEND_TO_Q("You have mail waiting.\r\n", d);

								notify_cleric_moon(d->character);

							}

							// check for dynamic text updates (news, inews, etc...)
							check_dyntext_updates(d->character, CHECKDYN_RECONNECT);


						} else {
							mudlog(MAX(LVL_GOD, GET_INVIS_LVL(tmp_ch)),
								NRM, true,
								"%s has re-logged:[%s]. disconnecting old socket.",
								GET_NAME(tmp_ch), d->host);
							SEND_TO_Q("This body has been usurped!\r\n", tmp_ch->desc);
							STATE(tmp_ch->desc) = CON_CLOSE;
							tmp_ch->desc->character = NULL;
							tmp_ch->desc = NULL;
							SEND_TO_Q("You take over your own body, already in use!\r\n", d);
							if (shutdown_count > 0) {
								sprintf(buf,
										"\r\n\007\007:: NOTICE :: Tempus will be rebooting in [%d] second%s ::\r\n",
										shutdown_count, shutdown_count == 1 ? "" : "s");
								send_to_char(d->character, "%s", buf);
							}

							if (!polc_char) {
								if (AWAKE(tmp_ch))
									act("$n suddenly keels over in pain, surrounded by a white "
										"aura...", TRUE, tmp_ch, 0, 0, TO_ROOM);
								else
									act("$n's body goes into convulsions!",TRUE,tmp_ch,0,0,TO_ROOM);
								act("$n's body has been taken over by a new spirit!",
									TRUE, tmp_ch, 0, 0, TO_ROOM);
							}
						}
						tmp_ch->desc = d;
						d->character = tmp_ch;
						tmp_ch->char_specials.timer = 0;
						d->character->player_specials->olc_obj = NULL;
						REMOVE_BIT(PLR_FLAGS(d->character), PLR_MAILING | PLR_WRITING |
								   PLR_OLC);
						if (polc_char)
							set_desc_state( CON_PORT_OLC,d );
						else
							set_desc_state( CON_PLAYING,d );
						return;
					}
				}

				if (!polc_char) {
					if (!mini_mud) {
						SEND_TO_Q("\033[H\033[J", d);
						if (GET_LEVEL(d->character) >= LVL_IMMORT) {
							if (clr(d->character, C_NRM))
								SEND_TO_Q(ansi_imotd, d);
							else
								SEND_TO_Q(imotd, d);
						} else {
							if (clr(d->character, C_NRM))
								SEND_TO_Q(ansi_motd, d);
							else
								SEND_TO_Q(motd, d);
						}
					}
				}

				
				mudlog(MAX(LVL_GOD,
					 PLR_FLAGGED(d->character, PLR_INVSTART) ?
					 GET_LEVEL(d->character) :
					 GET_INVIS_LVL(d->character)), CMP, true,
					"%s [%s] has connected.", GET_NAME(d->character), d->host);
				if (polc_char) {
					set_desc_state( CON_PORT_OLC,d );
				}
				else {
					if (load_result) {
						sprintf(buf, "\r\n\r\n\007\007\007"
								"%s%d LOGIN FAILURE%s SINCE LAST SUCCESSFUL LOGIN.%s\r\n",
								CCRED(d->character, C_SPR), load_result,
								(load_result > 1) ? "S" : "", CCNRM(d->character, C_SPR));
						SEND_TO_Q(buf, d);
					}
					set_desc_state( CON_RMOTD,d );
					//set_desc_state( CON_MENU,d );
				}
			}
			break;

		case CON_NEWPASSWD:
		case CON_CHPWD_GETNEW:
			if (!*arg || strlen(arg) > MAX_PWD_LENGTH || strlen(arg) < 3 ||
				!str_cmp(arg, GET_NAME(d->character))) {
				SEND_TO_Q("\r\nIllegal password.\r\n", d);
				return;
			}
			strncpy(GET_PASSWD(d->character), CRYPT(arg, GET_NAME(d->character)), MAX_PWD_LENGTH);
			*(GET_PASSWD(d->character) + MAX_PWD_LENGTH) = '\0';

			if (STATE(d) == CON_NEWPASSWD)
				set_desc_state( CON_CNFPASSWD,d );
			else
				set_desc_state( CON_CHPWD_VRFY,d );

			break;

		case CON_CNFPASSWD:
		case CON_CHPWD_VRFY:
			if (strncmp(CRYPT(arg, GET_PASSWD(d->character)), GET_PASSWD(d->character),
						MAX_PWD_LENGTH)) {
				SEND_TO_Q("\r\nPasswords don't match... start over.\r\n", d);
				if (STATE(d) == CON_CNFPASSWD)
					set_desc_state( CON_NEWPASSWD,d );
				else
					set_desc_state( CON_CHPWD_GETNEW,d );
				return;
			}
			echo_on(d);

			if (STATE(d) == CON_CNFPASSWD) {
				set_desc_state( CON_QCOLOR,d );
			} else {
				save_char(d->character, real_room(GET_LOADROOM(d->character)));
				echo_on(d);
				SEND_TO_Q("\r\nPassword changed.\r\n", d);
				set_desc_state( CON_MENU,d );
			}

			break;

		case CON_QCOLOR:
			switch (*arg) {
			case 'y':
			case 'Y':
				SET_BIT(PRF_FLAGS(d->character), PRF_COLOR_2);
				sprintf(buf, "\r\nYou will receive colors in the %sNORMAL%s format.\r\n",
						CCCYN(d->character, C_NRM), CCNRM(d->character, C_NRM));
				SEND_TO_Q(buf, d);
				SEND_TO_Q(CCYEL(d->character, C_NRM), d);
				SEND_TO_Q("If this is not acceptable, you may change modes at any time.\r\n\r\n", d);
				SEND_TO_Q(CCNRM(d->character, C_NRM), d);
				break;
			case 'n':
			case 'N':
				SEND_TO_Q("\r\nVery well.  You will not receive colored text.\r\n", d);
				break;
			default:
				SEND_TO_Q("\r\nYou must specify either 'Y' or 'N'.\r\n", d);
				return;
			}

			set_desc_state( CON_QSEX,d );
			return;

		case CON_QSEX:        // query sex of new user
			if (is_abbrev(arg, "male"))
				d->character->player.sex = SEX_MALE;
			else if (is_abbrev(arg, "female"))
				d->character->player.sex = SEX_FEMALE;
			else {
				SEND_TO_Q("That is not a sex...\r\n", d);
				break;
			}

			set_desc_state( CON_QTIME_FRAME,d );
			break;

		case CON_QTIME_FRAME:
			if (is_abbrev(arg, "past"))
				set_desc_state(CON_RACE_PAST, d);
			else if (is_abbrev(arg, "future"))
				set_desc_state(CON_RACE_FUTURE, d);
			else {
				SEND_TO_Q("\r\nThat's not a choice.\r\n\r\n", d);
				return;
			}
			break;
		case CON_RACEHELP_P:
			show_race_menu_future(d);
			set_desc_state( CON_RACE_PAST,d );
			break;

		case CON_RACEHELP_F:
			show_race_menu_future(d);
			set_desc_state( CON_RACE_FUTURE,d );
			break;

		case CON_RACE_PAST:
			GET_RACE(d->character) = parse_pc_race(d, arg, TIME_PAST);
			if (GET_RACE(d->character) == CLASS_UNDEFINED) {
				SEND_TO_Q(CCRED(d->character, C_NRM), d);
				SEND_TO_Q("\r\nThat's not a choice.\r\n", d);
				SEND_TO_Q(CCNRM(d->character, C_NRM), d);
				break;
			} else if (GET_RACE(d->character) == -2) {
				set_desc_state( CON_RACEHELP_P,d );
				break;
			}
			GET_CLASS(d->character) = CLASS_UNDEFINED;
			set_desc_state( CON_QCLASS_PAST,d );
			break;
		case CON_RACE_FUTURE:
			if ((GET_RACE(d->character) = parse_pc_race(d, arg, TIME_FUTURE)) == -1) {
				SEND_TO_Q(CCRED(d->character, C_NRM), d);
				SEND_TO_Q("\r\nThat's not a choice.\r\n", d);
				SEND_TO_Q(CCNRM(d->character, C_NRM), d);
				break;
			} else if (GET_RACE(d->character) == -2) {
				set_desc_state( CON_RACEHELP_F,d );
				break;
			}
			GET_CLASS(d->character) = CLASS_UNDEFINED;
			set_desc_state( CON_QCLASS_FUTURE,d );
			break;
		case CON_CLASSHELP_P:
			show_char_class_menu(d, TIME_PAST);
			set_desc_state( CON_QCLASS_PAST,d );
			break;

		case CON_CLASSHELP_F:
			show_char_class_menu(d, TIME_FUTURE);
			set_desc_state( CON_QCLASS_FUTURE,d );
			break;

		case CON_QCLASS_PAST:
			GET_CLASS(d->character) = parse_player_class(arg, TIME_PAST);
			if (GET_CLASS(d->character) == CLASS_UNDEFINED) {
				SEND_TO_Q(CCRED(d->character, C_NRM), d);
				SEND_TO_Q("\r\nThat's not a character class.\r\n", d);
				SEND_TO_Q(CCNRM(d->character, C_NRM), d);
				return;
			}

			for (i=0;i < NUM_PC_RACES;i++) {
				if (race_restr[i][0] == GET_RACE(d->character))
					break;
			}

			if (!race_restr[i][GET_CLASS(d->character)+1]) {
					SEND_TO_Q(CCGRN(d->character, C_NRM), d);
					SEND_TO_Q("\r\nThat character class is not allowed to your race!\r\n", d);
					GET_CLASS(d->character) = CLASS_UNDEFINED;
			} else
				set_desc_state( CON_QALIGN,d );
			break;
		case CON_QCLASS_FUTURE:
			GET_CLASS(d->character) = parse_player_class(arg, TIME_FUTURE);
			if (GET_CLASS(d->character) == CLASS_UNDEFINED) {
				SEND_TO_Q(CCRED(d->character, C_NRM), d);
				SEND_TO_Q("\r\nThat's not a character class.\r\n", d);
				SEND_TO_Q(CCNRM(d->character, C_NRM), d);
				return;
			}

			for (i=0;i < NUM_PC_RACES;i++)
				if (race_restr[i][0] == GET_RACE(d->character))
					break;
			if (!race_restr[i][GET_CLASS(d->character)+1]) {
					SEND_TO_Q(CCGRN(d->character, C_NRM), d);
					SEND_TO_Q("\r\nThat character class is not allowed to your race!\r\n", d);
					GET_CLASS(d->character) = CLASS_UNDEFINED;
			} else
				set_desc_state( CON_QALIGN,d );
			break;
		case CON_QCLASS_REMORT:
			GET_REMORT_CLASS(d->character) = parse_player_class(arg, TIME_TIMELESS);
			if (GET_REMORT_CLASS(d->character) == CLASS_UNDEFINED) {
				SEND_TO_Q(CCRED(d->character, C_NRM), d);
				SEND_TO_Q("\r\nThat's not a character class.\r\n", d);
				SEND_TO_Q(CCNRM(d->character, C_NRM), d);
				return;
			}

			for (i=0;i < NUM_PC_RACES;i++)
				if (race_restr[i][0] == GET_RACE(d->character))
					break;
			if (!race_restr[i][GET_REMORT_CLASS(d->character)+1]) {
				SEND_TO_Q(CCGRN(d->character, C_NRM), d);
				SEND_TO_Q("\r\nThat character class is not allowed to your race!\r\n", d);
			} else if (GET_REMORT_CLASS(d->character) == GET_CLASS(d->character)) {
				SEND_TO_Q(CCGRN(d->character, C_NRM), d);
				SEND_TO_Q("\r\nYou can't remort to your primary class!\r\n", d);
				
			} else if ((GET_CLASS(d->character) == CLASS_MONK &&
					(GET_REMORT_CLASS(d->character) == CLASS_KNIGHT ||
					GET_REMORT_CLASS(d->character) == CLASS_CLERIC)) ||
					((GET_CLASS(d->character) == CLASS_CLERIC ||
					GET_CLASS(d->character) == CLASS_KNIGHT) &&
					GET_REMORT_CLASS(d->character) == CLASS_MONK)) {
				// No being a monk and a knight or cleric
				SEND_TO_Q(CCGRN(d->character, C_NRM), d);
				SEND_TO_Q("\r\nYour religious beliefs are in conflict with that class!\r\n", d);
				
			} else {
				if (GET_CLASS(d->character) == CLASS_VAMPIRE)
					GET_CLASS(d->character) = GET_OLD_CLASS(d->character);
				mudlog(LVL_IMMORT, BRF, true,
					"%s has remorted to gen %d as a %s/%s",
					GET_NAME(d->character), GET_REMORT_GEN(d->character),
					pc_char_class_types[(int)GET_CLASS(d->character)],
					pc_char_class_types[(int)GET_REMORT_CLASS(d->character)]);
				set_desc_state( CON_MENU,d );
			}
			break;
		case CON_QALIGN:
			if ( IS_DROW(d->character) )
				{
				GET_ALIGNMENT(d->character) = -666;
				set_desc_state( CON_QREROLL,d );
				break;
				}
			else if ( IS_MONK( d->character ) )
				{
				GET_ALIGNMENT(d->character) = 0;
				set_desc_state( CON_QREROLL,d );
				break;
				}

			if (is_abbrev(arg, "evil"))
				d->character->char_specials.saved.alignment = -666;
			else if (is_abbrev(arg, "good"))
				d->character->char_specials.saved.alignment = 777;
			else if (is_abbrev(arg, "neutral")) {
				if (GET_CLASS(d->character) == CLASS_KNIGHT ||
					GET_CLASS(d->character) == CLASS_CLERIC) {
					SEND_TO_Q(CCGRN(d->character, C_NRM), d);
					SEND_TO_Q("Characters of your character class must be either Good, or Evil.\r\n", d);
					break;
				}
				d->character->char_specials.saved.alignment = 0;
			} else {
				SEND_TO_Q(CCRED(d->character, C_NRM), d);
				SEND_TO_Q("Thats not a choice.\r\n", d);
				SEND_TO_Q(CCNRM(d->character, C_NRM), d);
				break;
			}

			if (GET_CLASS(d->character) == CLASS_CLERIC)
				GET_DEITY(d->character) = DEITY_GUIHARIA;
			else if (GET_CLASS(d->character) == CLASS_KNIGHT)
				if (IS_GOOD(d->character))
					GET_DEITY(d->character) = DEITY_JUSTICE;
				else
					GET_DEITY(d->character) = DEITY_ARES;
			else
				GET_DEITY(d->character) = DEITY_GUIHARIA;

			set_desc_state( CON_QREROLL,d );
			break;
		case CON_QREROLL:
			if (is_abbrev(arg, "reroll")) {
				roll_real_abils(d->character);
				d->wait = 4;
				break;
			} else if (is_abbrev(arg, "keep")) {
				save_char(d->character, NULL);
				mudlog(LVL_GOD, NRM, true,
					"%s [%s] new player.", GET_NAME(d->character), d->host);
				GET_HOME(d->character) = HOME_NEWBIE_SCHOOL;
				population_record[HOME_NEWBIE_SCHOOL]++;

				if (GET_PFILEPOS(d->character) < 0)
					GET_PFILEPOS(d->character) = create_entry(GET_NAME(d->character));
				init_char(d->character);
				save_char(d->character, NULL);
				set_desc_state( CON_EXDESC,d );
				start_text_editor(d,&d->character->player.description,true, MAX_CHAR_DESC-1);

			} else
				SEND_TO_Q("You must type 'reroll' or 'keep'.\r\n", d);
			break;

		case CON_RMOTD:        // read CR after printing motd
			if (!mini_mud)
				SEND_TO_Q("\033[H\033[J", d);
			set_desc_state( CON_MENU,d );
			break;

		case CON_MENU:        // get selection from main menu
			switch (*arg) {
			case '0':
				SEND_TO_Q("\033[H\033[J", d);
				SEND_TO_Q("Connection terminated.\r\n", d);
				close_socket(d);
				break;

			case '1':
				// this code is to prevent people from multiply logging in
				struct room_data *theroom;
				for (k = descriptor_list; k; k = next) {
					next = k->next;
					if (!k->connected && k->character &&
						!str_cmp(GET_NAME(k->character), GET_NAME(d->character))) {
						SEND_TO_Q("Your character has been deleted.\r\n", d);
						set_desc_state( CON_CLOSE,d );
						return;
					}
				}
				if (!mini_mud)    SEND_TO_Q("\033[H\033[J", d);

				reset_char(d->character);

				// if we're not a new char, check loadroom and rent
				if (GET_LEVEL(d->character)) {
					// If we're buried, tell em and drop link.
					if (PLR2_FLAGGED(d->character, PLR2_BURIED)) {
						sprintf(buf,"You lay fresh flowers on the grave of %s.\r\n",GET_NAME(d->character));
						SEND_TO_Q( buf, d);
						mudlog(LVL_GOD, NRM, true,
							"Disconnecting %s. - Character is buried.",
							GET_NAME(d->character));
						set_desc_state( CON_CLOSE,d );
						return;
					}
					theroom = real_room(GET_LOADROOM(d->character));
					if(theroom && House_can_enter(d->character,theroom->number)
					   && clan_house_can_enter(d->character, theroom) ) {
						d->character->in_room = theroom;
					} else {
						if(theroom) {
							mudlog(LVL_DEMI, NRM, true,
								"%s unable to load in <clan>house room %d. Loadroom unset.",
								GET_NAME(d->character),GET_LOADROOM(d->character));
						}
						GET_LOADROOM(d->character) = -1;
					}
					if(GET_LOADROOM(d->character) == -1 &&
					   GET_HOLD_LOADROOM(d->character) == -1) {
						REMOVE_BIT(PLR_FLAGS(d->character), PLR_LOADROOM);
					}

					if (PLR_FLAGGED(d->character, PLR_INVSTART))
						GET_INVIS_LVL(d->character) = (GET_LEVEL(d->character) > LVL_LUCIFER ?
													   LVL_LUCIFER : GET_LEVEL(d->character));

					load_result = Crash_load(d->character);

					// In case of error, break out
					if (load_result == -1)
						break;

					if (load_result && d->character->in_room == NULL) {
						d->character->in_room = NULL;
					}
				} else { // otherwise null the loadroom
					d->character->in_room = NULL;
				}

				save_char(d->character, NULL);
				send_to_char(d->character, CCRED(d->character, C_NRM));
				send_to_char(d->character, CCBLD(d->character, C_NRM));
				send_to_char(d->character, WELC_MESSG);
				send_to_char(d->character, CCNRM(d->character, C_NRM));
				characterList.add(d->character);

				if( d->character->in_room == NULL )
					load_room = d->character->getLoadroom();
				else
					load_room = d->character->in_room;

				char_to_room(d->character, load_room);
				load_room->zone->enter_count++;

				if (!(PLR_FLAGGED(d->character, PLR_LOADROOM)) &&
					GET_HOLD_LOADROOM(d->character) > 0 &&
					real_room(GET_HOLD_LOADROOM(d->character))) {
					GET_LOADROOM(d->character) = GET_HOLD_LOADROOM(d->character);
					SET_BIT(PLR_FLAGS(d->character), PLR_LOADROOM);
					GET_HOLD_LOADROOM(d->character) = NOWHERE;
				}
				show_mud_date_to_char(d->character);
				send_to_char(d->character, "\r\n");

				set_desc_state( CON_PLAYING,d );
				if( GET_LEVEL(d->character) < LVL_IMMORT )
					REMOVE_BIT(PRF2_FLAGS(d->character), PRF2_NOWHO);
				if (!GET_LEVEL(d->character)) {
					send_to_char(d->character, START_MESSG);
					sprintf(buf,
							" ***> New adventurer %s has entered the realm. <***\r\n",
							GET_NAME(d->character));
					send_to_newbie_helpers(buf);
					do_start(d->character, 0);

					d->old_login_time = time(0);  // clear login time so we dont get news updates
					// New characters shouldn't get old mail.
					if(has_mail(GET_IDNUM(d->character))) {
					   if(purge_mail(GET_IDNUM(d->character))>0) {
						   slog("SYSERR: Purging pre-existing mailfile for new character.(%s)",
								GET_NAME(d->character));
					   }
					}

				}
				else if (load_result == 2) {    // rented items lost
					send_to_char(d->character, "\r\n\007You could not afford your rent!\r\n"
								 "Some of your possesions have been repossesed to cover your bill!\r\n");
				}

				act("$n has entered the game.", TRUE, d->character, 0, 0, TO_ROOM);
				look_at_room(d->character, d->character->in_room, 0);

				// Remove the quest prf flag (for who list) if they're
				// not in an active quest.
				if( PRF_FLAGGED(d->character, PRF_QUEST) )
				{
					Quest *quest = quest_by_vnum( GET_QUEST(d->character) );
					if( GET_QUEST(d->character) == 0 || quest == NULL 
					|| quest->getEnded() >= 0 || !quest->isPlaying(GET_IDNUM(d->character)) ) 
					{
						REMOVE_BIT(PRF_FLAGS(d->character), PRF_QUEST );
						GET_QUEST(d->character) = 0;
					}
				}

				REMOVE_BIT( PLR_FLAGS( d->character), PLR_CRYO );

				if (has_mail(GET_IDNUM(d->character)))
					send_to_char(d->character, "You have mail waiting.\r\n");

				notify_cleric_moon(d->character);

				// check for dynamic text updates (news, inews, etc...)
				check_dyntext_updates(d->character, CHECKDYN_UNRENT);

				if (shutdown_count > 0) {
					sprintf(buf,
							"\r\n\007\007:: NOTICE :: Tempus will be rebooting in [%d] second%s ::\r\n",
							shutdown_count, shutdown_count == 1 ? "" : "s");
					send_to_char(d->character, "%s", buf);
				}
				if(GET_LEVEL(d->character) < LVL_GOD) {
					for (i = 0; i < num_of_houses; i++) {
						if (house_control[i].mode != HOUSE_PUBLIC &&
							house_control[i].owner1 == GET_IDNUM(d->character) &&
							house_control[i].rent_sum &&
							(h_rm = real_room(house_control[i].house_rooms[0]))) {
							for (j=0, percent=1, rooms=0; j < house_control[i].num_of_rooms; j++) {
								if ((rm = real_room(house_control[i].house_rooms[j]))) {
									for (cur=0, obj = rm->contents; obj; obj = obj->next_content)
										cur += recurs_obj_contents(obj, NULL);
									if (cur > MAX_HOUSE_ITEMS) {
										sprintf(buf, "WARNING:  House room [%s%s%s] contains %d items.\r\n",
												CCCYN(d->character, C_NRM), rm->name, CCNRM(d->character, C_NRM), cur);
										SEND_TO_Q(buf, d);

										// add one factor for every MAX_HOUSE_ITEMS over
										percent += (cur / MAX_HOUSE_ITEMS);
										rooms++;

									}
								}
							}
							if (percent > 1) {
								sprintf(buf, "You exceeded the house limit in %d room%s.\n"
										"Your house rent is multiplied by a factor of %d.\n",
										rooms, percent > 1 ? "s" : "", percent);
								SEND_TO_Q(buf, d);
								slog("%s exceeded house limit in %d rooms, %d multiplier charged.",
										GET_NAME(d->character), rooms, percent);

								// actually multiply it
								house_control[i].rent_sum *= percent;
							}
							sprintf(buf,
									"You have accrued costs of %d %s on property: %s.\r\n",
									house_control[i].rent_sum,
									(cur = (h_rm->zone->time_frame == TIME_ELECTRO)) ?
									"credits" : "coins", h_rm->name);
							SEND_TO_Q(buf, d);
							if (cur) {        // credits
								if ((GET_ECONET(d->character) + GET_CASH(d->character))
									< house_control[i].rent_sum) {
									house_control[i].rent_sum -= GET_ECONET(d->character);
									house_control[i].rent_sum -= GET_CASH(d->character);
									GET_ECONET(d->character) = GET_CASH(d->character) = 0;
								} else {
									GET_CASH(d->character) -= house_control[i].rent_sum;
									if (GET_CASH(d->character) < 0) {
										GET_ECONET(d->character) += GET_CASH(d->character);
										GET_CASH(d->character) = 0;
									}
									house_control[i].rent_sum = 0;
								}
							} else {          /** gold economy **/
								if (GET_GOLD(d->character) < house_control[i].rent_sum) {
									house_control[i].rent_sum -= GET_GOLD(d->character);
									GET_GOLD(d->character) = 0;
								} else {
									GET_GOLD(d->character) -= house_control[i].rent_sum;
									house_control[i].rent_sum = 0;
								}
							}

							if (house_control[i].rent_sum > 0) {     // bank is universal
								if (GET_BANK_GOLD(d->character) < house_control[i].rent_sum) {
									house_control[i].rent_sum -= GET_BANK_GOLD(d->character);
									GET_BANK_GOLD(d->character) = 0;
									SEND_TO_Q("You could not afford the rent.\r\n"
											  "Some of your items have been repossessed.\r\n", d);
									slog("House-rent (%d) - equipment lost.",
											house_control[i].house_rooms[0]);
									for (j = 0;
										 j < house_control[i].num_of_rooms &&
											 house_control[i].rent_sum > 0; j++) {
										if ((h_rm = real_room(house_control[i].house_rooms[j]))) {
											for (obj = h_rm->contents;
												 obj && house_control[i].rent_sum > 0;
												 obj = next_obj) 
											{
												next_obj = obj->next_content;
												int cost = recurs_obj_cost(obj, true, NULL);
												slog( "Removing object: '%s' [%d] (cost: %d)", obj->short_description, GET_OBJ_VNUM(obj), cost);
												house_control[i].rent_sum -= cost;
												extract_obj(obj);
											}
										}
									}
								} else
									GET_BANK_GOLD(d->character) -= house_control[i].rent_sum;
							}

							house_control[i].rent_sum = 0;
							house_control[i].hourly_rent_sum = 0;
							house_control[i].rent_time = 0;
							House_crashsave(h_rm->number);
						}
					}
				}
				break;

			case '2':
				SEND_TO_Q("\033[H\033[J", d);
				SEND_TO_Q("Other players will be usually be able to determine your general\r\n"
						  "size, as well as your race and gender, by looking at you.  What\r\n"
						  "else is noticable about your character?\r\n", d);
				if (d->character->player.description) {
					SEND_TO_Q("Old description:\r\n", d);
					SEND_TO_Q(d->character->player.description, d);
					free(d->character->player.description);
					d->character->player.description = NULL;
				}
				start_text_editor(d, &d->character->player.description, true, MAX_CHAR_DESC-1);
				set_desc_state( CON_EXDESC,d );
				break;

			case '3':
				SEND_TO_Q("\033[H\033[J", d);
				page_string(d, background);
				set_desc_state( CON_RMOTD,d );
				break;

			case '4':
				SEND_TO_Q("\033[H\033[J", d);
				sprintf(buf, "Changing password for %s.\r\n", GET_NAME(d->character));
				SEND_TO_Q(buf, d);
				set_desc_state( CON_CHPWD_GETOLD,d );
				break;

			case '5':
				SEND_TO_Q("\033[H\033[J", d);
				sprintf(buf, "\r\nDeleting character '%s', level %d.\r\n",
						GET_NAME(d->character), GET_LEVEL(d->character));
				SEND_TO_Q(buf, d);
				set_desc_state( CON_DELCNF1,d );
				break;

			default:
				if (!mini_mud)
					SEND_TO_Q("\033[H\033[J", d);
				SEND_TO_Q("\r\nThat's not a menu choice!\r\n", d);
				break;
			}

			break;

		case CON_CHPWD_GETOLD:
			if (strncmp(CRYPT(arg, GET_PASSWD(d->character)), GET_PASSWD(d->character), MAX_PWD_LENGTH)) {
				echo_on(d);
				SEND_TO_Q("\033[H\033[J", d);
				SEND_TO_Q("\r\nIncorrect password.  ---  Password unchanged\r\n", d);
				set_desc_state( CON_MENU,d );
				return;
			} else {
				set_desc_state( CON_CHPWD_GETNEW,d );
				return;
			}
			break;

		case CON_DELCNF1:
			echo_on(d);
			if (strncmp(CRYPT(arg, GET_PASSWD(d->character)), GET_PASSWD(d->character), MAX_PWD_LENGTH)) {
				SEND_TO_Q("\r\nIncorrect password. -- Deletion aborted.\r\n", d);
				set_desc_state( CON_MENU,d );
			} else {
				set_desc_state( CON_DELCNF2,d );
			}
			break;

		case CON_DELCNF2:
			if (!strcmp(arg, "yes") || !strcmp(arg, "YES")) {
				if (PLR_FLAGGED(d->character, PLR_FROZEN)) {
					SEND_TO_Q(CCCYN(d->character, C_NRM), d);
					SEND_TO_Q(CCBLD(d->character, C_NRM), d);
					SEND_TO_Q("You try to kill yourself, but the ice stops you.\r\n", d);
					SEND_TO_Q(CCNRM(d->character, C_NRM), d);
					SEND_TO_Q("Character not deleted.\r\n\r\n", d);
					set_desc_state( CON_CLOSE,d );
					return;
				}
				if (GET_LEVEL(d->character) < LVL_GRGOD)
					SET_BIT(PLR_FLAGS(d->character), PLR_DELETED);
				save_char(d->character, real_room(GET_LOADROOM(d->character)));
				Crash_delete_file(GET_NAME(d->character), CRASH_FILE);
				Crash_delete_file(GET_NAME(d->character), IMPLANT_FILE);
				population_record[GET_HOME(d->character)]--;

				if ((clan = real_clan(GET_CLAN(d->character))) &&
					(member = real_clanmember(GET_IDNUM(d->character), clan))) {
					REMOVE_MEMBER_FROM_CLAN(member, clan);

					free(member);
					save_clans();
				}

				sprintf(buf, "Character '%s' deleted!\r\n"
						"Goodbye.\r\n", GET_NAME(d->character));
				SEND_TO_Q(buf, d);
				mudlog(LVL_GOD, NRM, true,
					"%s (lev %d) has self-deleted.", GET_NAME(d->character),
					GET_LEVEL(d->character));
				set_desc_state( CON_CLOSE,d );
				return;
			} else {
				SEND_TO_Q("\r\nCharacter not deleted.\r\n", d);
				set_desc_state( CON_MENU,d );
			}
			break;

		case CON_AFTERLIFE:
			SEND_TO_Q("\033[H\033[J", d);
			set_desc_state( CON_MENU,d );
			break;

		case CON_CLOSE:
			close_socket(d); break;

		case CON_NETWORK:
			handle_network(d, arg); break;

		case CON_PORT_OLC:
			polc_input (d, arg); break;

		default:
			slog("SYSERR: Nanny: illegal state of con'ness; closing connection");
			close_socket(d);
			break;
    }
}


void
make_prompt(struct descriptor_data * d)
{
	char prompt[MAX_INPUT_LENGTH];
    char colorbuf[ 100 ];

	// No prompt for the wicked
	if (STATE(d) == CON_CLOSE )
		return;

	// Check for the text editor being used
	if (d->character && d->text_editor) {
			sprintf(prompt, "%-2d%s]%s ",
		d->editor_cur_lnum,
		CCBLU_BLD(d->character,C_NRM),
		CCNRM(d->character,C_NRM));

		SEND_TO_Q(prompt,d);
		d->need_prompt = false;
		return;
		}

	// Handle all other states
	switch ( STATE(d) )
	{
		case CON_PLAYING:			// Playing - Nominal state
			*prompt = '\0';

			if (GET_INVIS_LVL(d->character))
				sprintf(prompt,"%s%s(%si%d%s)%s ",prompt,CCMAG(d->character, C_NRM),
					CCRED(d->character, C_NRM), GET_INVIS_LVL(d->character),
					CCMAG(d->character, C_NRM), CCNRM(d->character, C_NRM));
			else if (IS_MOB(d->character))
				sprintf(prompt, "%s%s[NPC]%s ", prompt,
					CCCYN(d->character, C_NRM), CCNRM(d->character, C_NRM));

			if (PRF_FLAGGED(d->character, PRF_DISPHP))
				sprintf(prompt, "%s%s%s< %s%d%s%sH%s ", prompt,
						CCWHT(d->character, C_SPR), CCBLD(d->character, C_CMP),
						CCGRN(d->character, C_SPR), GET_HIT(d->character),
						CCNRM(d->character, C_SPR),
						CCYEL_BLD(d->character, C_CMP), CCNRM(d->character, C_SPR));

			if (PRF_FLAGGED(d->character, PRF_DISPMANA))
				sprintf(prompt, "%s%s%s%d%s%sM%s ", prompt,
						CCBLD(d->character, C_CMP), CCMAG(d->character, C_SPR),
						GET_MANA(d->character), CCNRM(d->character, C_SPR),
						CCYEL_BLD(d->character, C_CMP), CCNRM(d->character, C_SPR));
		
			if (PRF_FLAGGED(d->character, PRF_DISPMOVE))
				sprintf(prompt, "%s%s%s%d%s%sV%s ", prompt,
						CCCYN(d->character, C_SPR), CCBLD(d->character, C_CMP),
						GET_MOVE(d->character), CCNRM(d->character, C_SPR),
						CCYEL_BLD(d->character, C_CMP), CCNRM(d->character, C_SPR));

			if ( PRF2_FLAGGED( d->character, PRF2_DISPALIGN ) ) {
				
				if( IS_GOOD( d->character ) ) {
				sprintf( colorbuf, "%s", CCCYN( d->character, C_SPR ) );
			} else if ( IS_EVIL( d->character ) ) {
				sprintf( colorbuf, "%s", CCRED( d->character, C_SPR ) );
				} else {
				sprintf( colorbuf, "%s", CCWHT(d->character, C_SPR ) );
				}

				sprintf( prompt, "%s%s%s%d%s%sA%s ", prompt,
						colorbuf, CCBLD( d->character,C_CMP ),
						GET_ALIGNMENT( d->character ), CCNRM( d->character, C_SPR ),
						CCYEL_BLD( d->character, C_CMP ), CCNRM( d->character,C_SPR ) );
			}

			if (FIGHTING(d->character) &&
				PRF2_FLAGGED(d->character, PRF2_AUTO_DIAGNOSE))
				sprintf(prompt, "%s%s(%s)%s ", prompt, CCRED(d->character, C_NRM),
						diag_conditions(FIGHTING(d->character)),
						CCNRM(d->character, C_NRM));
			
			sprintf(prompt, "%s%s%s>%s ", prompt, CCWHT(d->character, C_NRM),
					CCBLD(d->character, C_CMP), CCNRM(d->character, C_NRM));
			SEND_TO_Q(prompt,d);
			d->output_broken = FALSE;
			break;
		case CON_CLOSE:				// Disconnecting
			break;
		case CON_GET_NAME:			// By what name ..?
			SEND_TO_Q( "Enter your name, or 'new' for a new character:  ",d ); break;
		case CON_NAME_PROMPT:			// By what name ..?
			SEND_TO_Q( "By which name do you wish to be known? ",d ); break;
		case CON_NAME_CNFRM:		// Did I get that right, x?
			sprintf( buf,"Did I get that right, %s (Y/N)? ",GET_NAME(d->character));
			SEND_TO_Q( buf,d );
			break;
		case CON_PASSWORD:			// Password:
			SEND_TO_Q( "Password: ",d ); break;
		case CON_NEWPASSWD:			// Give me a password for x
			SEND_TO_Q( "          What shall be your password: ",d ); break;
		case CON_CNFPASSWD:			// Please retype password:
		case CON_CHPWD_VRFY:		// Verify new password
			SEND_TO_Q( "          Please retype password: ",d ); break;
		case CON_QCOLOR:			// Start with color?
			SEND_TO_Q( "          Is your terminal compatible to receive colors (Y/N)? ",d );
			break;
		case CON_QSEX:				// Sex?
			sprintf( buf,"%s          What do you choose as your sex %s(M/F)%s?%s ",
				CCCYN(d->character, C_NRM), CCGRN(d->character, C_NRM),
				CCCYN(d->character, C_NRM), CCNRM(d->character, C_NRM));
			SEND_TO_Q( buf,d );
			break;
		case CON_RACE_PAST:			// Racial Query
		case CON_RACE_FUTURE:		// Racial Query
			sprintf( prompt,"\r\n%s          What race are you a member of? %s",
				CCCYN(d->character, C_NRM),CCNRM(d->character,C_NRM));
			SEND_TO_Q( prompt,d );
            break;
		case CON_QTIME_FRAME:		// Query for overall time frame
			sprintf( prompt,"\r\n%s          From what time period do you hail? %s",
				CCCYN(d->character, C_NRM),CCNRM(d->character,C_NRM));
			SEND_TO_Q( prompt,d );
            break;
		case CON_QCLASS_PAST:
		case CON_QCLASS_FUTURE:
		case CON_QCLASS_REMORT:
			sprintf( prompt,"\r\n%s          Choose your profession from the above list: %s",
				CCCYN(d->character, C_NRM),CCNRM(d->character,C_NRM));
			SEND_TO_Q( prompt,d );
			break;
		case CON_QALIGN:			// Alignment Query
			if ( IS_DROW(d->character) ) 
				{
				SEND_TO_Q("    The Drow race is inherently evil.  Thus you begin your life as evil.\r\n\r\n          Press return to continue.\r\n", d);
				break;
				}
			else if ( IS_MONK(d->character) )
				{
				SEND_TO_Q("    The monastic ideology requires that you remain neutral in alignment.\r\nTherefore you begin your life with a perfect neutrality.\r\n\r\n",d );
                SEND_TO_Q("\r\n*** PRESS RETURN: ", d);
				break;
				}
			else if ( GET_CLASS(d->character) == CLASS_KNIGHT ||
				 GET_CLASS(d->character) == CLASS_CLERIC)
				{
				sprintf(buf, "%s          Do you wish to be %sGood%s or %sEvil%s?%s ",
						CCCYN(d->character, C_NRM), CCGRN(d->character, C_NRM),
						CCCYN(d->character, C_NRM), CCGRN(d->character, C_NRM),
						CCCYN(d->character, C_NRM), CCNRM(d->character, C_NRM));
				}
			else
				{
				sprintf(buf, "%s          Do you wish to be %sGood%s, %sNeutral%s, or %sEvil%s?%s ",
						CCCYN(d->character, C_NRM), CCGRN(d->character, C_NRM),
						CCCYN(d->character, C_NRM), CCGRN(d->character, C_NRM),
						CCCYN(d->character, C_NRM), CCGRN(d->character, C_NRM),
						CCCYN(d->character, C_NRM), CCNRM(d->character, C_NRM));
				}

			SEND_TO_Q(buf, d);
			break;
		case CON_QREROLL:		
			print_attributes_to_buf( d->character,buf2 );
			SEND_TO_Q("\033[H\033[J", d);
			sprintf(buf,"%s\r\n                              CHARACTER ATTRIBUTES\r\n*******************************************************************************\r\n\r\n\r\n",CCCYN(d->character,C_NRM));
			SEND_TO_Q(buf,d);
			SEND_TO_Q(buf2, d);
			SEND_TO_Q("\r\n", d);
			sprintf( buf,"%sWould you like to %sREROLL%s or %sKEEP%s these attributes?%s ",
				CCCYN(d->character,C_NRM),CCGRN(d->character,C_NRM),
				CCCYN(d->character,C_NRM),CCGRN(d->character,C_NRM),
				CCCYN(d->character,C_NRM),CCNRM(d->character,C_NRM));
			SEND_TO_Q("Would you like to REROLL or KEEP these attributes? ",d);
			break;
		case CON_RMOTD:				// PRESS RETURN after MOTD
			SEND_TO_Q("\r\n  Press return to continue.", d);
			break;
		case CON_MENU:				// Your choice: (main menu)
			show_menu(d); break;
		case CON_EXDESC:			// Enter a new description:
			break;
		case CON_CHPWD_GETOLD:		// Changing passwd: get old
			SEND_TO_Q("\r\nEnter your old password, or press enter to cancel: ",d);
			break;
		case CON_CHPWD_GETNEW:		// Changing passwd: get new
			SEND_TO_Q("\r\nEnter a new password: ",d ); break;
		case CON_DELCNF1:			// Delete confirmation 1
			SEND_TO_Q("\r\nFor verification, please enter your password: ",d ); break;
		case CON_DELCNF2:			// Delete confirmation 2
			SEND_TO_Q(CCRED(d->character,C_SPR),d );
			SEND_TO_Q("\r\nYOU ARE ABOUT TO DELETE THIS CHARACTER PERMANENTLY.\r\n"
					  "ARE YOU ABSOLUTELY SURE?\r\n\r\n",d);
			SEND_TO_Q(CCNRM(d->character,C_SPR),d );
			SEND_TO_Q("Please type \"yes\" to confirm: ",d);
			break;
		case CON_AFTERLIFE:			// After dies, before menu
			SEND_TO_Q("\r\n\r\nPlease press return to continue into the afterlife...\r\n",d); break;
		case CON_RACEHELP_P:		
		case CON_CLASSHELP_P:		
		case CON_RACEHELP_F:		
		case CON_CLASSHELP_F:		
			SEND_TO_Q("\r\n  Press return to continue.", d);
			break;
		case CON_NETWORK:
            SEND_TO_Q("> ",d ); break;
		case CON_PORT_OLC:			// Using port olc interface
			break;
		default:
			slog( "Invalid state for descriptor" );
	}
	d->need_prompt = false;
}

void
make_menu( struct descriptor_data *d )
	{
	switch ( STATE(d) )
		{
		case CON_NEWPASSWD:			// Password:
		case CON_CHPWD_GETOLD:
			SEND_TO_Q( "\033[H\033[J",d );
			sprintf(buf,"%s\r\n                                  SET PASSWORD\r\n*******************************************************************************\r\n%s",CCCYN(d->character,C_NRM),CCNRM(d->character,C_NRM));
			SEND_TO_Q(buf,d);
            SEND_TO_Q("\r\n\r\n    In order to protect your character against intrusion, you must\r\n"
                    "choose a password to use on this system.\r\n\r\n",d);
			break;
		case CON_DELCNF1:
			SEND_TO_Q( "\033[H\033[J",d );
			sprintf(buf,"%s\r\n                                DELETE CHARACTER\r\n*******************************************************************************\r\n%s",CCRED(d->character,C_NRM),CCNRM(d->character,C_NRM));
			SEND_TO_Q(buf,d);
			break;
		case CON_QCOLOR:			// Start with color?
			SEND_TO_Q( "\033[H\033[J",d );
			sprintf(buf,"%s\r\n                                   ANSI COLOR\r\n*******************************************************************************\r\n",CCCYN(d->character,C_NRM));
			SEND_TO_Q(buf,d);
            SEND_TO_Q("\r\n\r\nThis game supports ANSI color standards.\r\n\r\n",d );
			break;
		case CON_QSEX:				// Sex?
			SEND_TO_Q( "\033[H\033[J",d );
			sprintf(buf,"%s\r\n                                     GENDER\r\n*******************************************************************************\r\n\r\n\r\n",CCCYN(d->character,C_NRM));
			SEND_TO_Q(buf,d);
			break;
		case CON_RACE_PAST:			// Racial Query
			SEND_TO_Q( "\033[H\033[J",d );
			sprintf(buf,"%s\r\n                                      RACE\r\n*******************************************************************************\r\n",CCCYN(d->character,C_NRM));
			SEND_TO_Q(buf,d);
			show_race_menu_past(d);
            break;
		case CON_RACE_FUTURE:		// Racial Query
			SEND_TO_Q( "\033[H\033[J",d );
			sprintf(buf,"%s\r\n                                      RACE\r\n*******************************************************************************\r\n",CCCYN(d->character,C_NRM));
			SEND_TO_Q(buf,d);
			show_race_menu_future(d);
			break;
		case CON_QTIME_FRAME:		// Query for overall time frame
			SEND_TO_Q( "\033[H\033[J",d );
			sprintf(buf,"%s\r\n                                      TIME\r\n*******************************************************************************\r\n",CCCYN(d->character,C_NRM));
			SEND_TO_Q(buf,d);
			show_time_menu( d );
			break;
		case CON_QCLASS_PAST:
			SEND_TO_Q( "\033[H\033[J",d );
			sprintf(buf,"%s\r\n                                   PROFESSION\r\n*******************************************************************************\r\n\r\n\r\n",CCCYN(d->character,C_NRM));
			SEND_TO_Q(buf,d);
			show_char_class_menu(d, TIME_PAST);
			break;
		case CON_QCLASS_FUTURE:
			SEND_TO_Q( "\033[H\033[J",d );
			sprintf(buf,"%s\r\n                                   PROFESSION\r\n*******************************************************************************\r\n\r\n\r\n",CCCYN(d->character,C_NRM));
			SEND_TO_Q(buf,d);
			show_char_class_menu(d, TIME_FUTURE);
			break;
		case CON_QCLASS_REMORT:
			SEND_TO_Q( "\033[H\033[J",d );
			sprintf(buf,"%s\r\n                                   PROFESSION\r\n*******************************************************************************\r\n\r\n\r\n",CCCYN(d->character,C_NRM));
			SEND_TO_Q(buf,d);
            show_char_class_menu(d);
			break;
		case CON_CLASSHELP_P:		
			SEND_TO_Q( "\033[H\033[J",d );
			sprintf(buf,"%s\r\n                                   PROFESSION\r\n*******************************************************************************\r\n\r\n\r\n",CCCYN(d->character,C_NRM));
			SEND_TO_Q(buf,d);
			break;
		case CON_CLASSHELP_F:		
			SEND_TO_Q( "\033[H\033[J",d );
			sprintf(buf,"%s\r\n                                   PROFESSION\r\n*******************************************************************************\r\n\r\n\r\n",CCCYN(d->character,C_NRM));
			SEND_TO_Q(buf,d);
			break;
		case CON_QALIGN:			// Alignment Query
			SEND_TO_Q( "\033[H\033[J",d );
			sprintf(buf,"%s\r\n                                   ALIGNMENT\r\n*******************************************************************************\r\n",CCCYN(d->character,C_NRM));
			SEND_TO_Q(buf,d);
			SEND_TO_Q("\r\n\r\n    ALIGNMENT is a measure of your philosophies and morals.\r\n\r\n", d);
			break;
		case CON_EXDESC:
			SEND_TO_Q("\033[H\033[J", d);
			sprintf(buf,"%s\r\n                                  DESCRIPTION\r\n*******************************************************************************\r\n",CCCYN(d->character,C_NRM));
			SEND_TO_Q(buf,d);
			SEND_TO_Q("\r\n\r\n    Other players will usually be able to determine your general\r\n"
					  "size, as well as your race and gender, by looking at you.  What\r\n"
					  "else is noticable about your character?\r\n\r\n", d);
			break;
		case CON_NETWORK:
			SEND_TO_Q("\033[H\033[J",d );
			SEND_TO_Q("GLOBAL NETWORK SYSTEMS CLI\r\n",d);
			SEND_TO_Q("-------------------------------------------------------------------------------\r\n",d);
			SEND_TO_Q("Enter commands at prompt.  Use '@' to escape.  Use '?' for help.\r\n",d);
		default:
            break;
		}
	}

void
set_desc_state(int state,struct descriptor_data *d)
	{
	if ( STATE(d) == CON_PASSWORD ||
		STATE(d) == CON_NEWPASSWD ||
		STATE(d) == CON_CHPWD_GETOLD ||
		STATE(d) == CON_DELCNF1 ||
		STATE(d) == CON_CNFPASSWD )
		echo_on(d);
	STATE(d) = state;
    make_menu( d );
	if ( STATE(d) == CON_PASSWORD ||
		STATE(d) == CON_NEWPASSWD ||
		STATE(d) == CON_CHPWD_GETOLD ||
		STATE(d) == CON_DELCNF1 ||
		STATE(d) == CON_CNFPASSWD )
		echo_off(d);
	if (CON_AFTERLIFE == state) {
		d->inbuf[0] = '\0';
		d->wait = 5 RL_SEC;
	}

	d->need_prompt = true;
	}

/*
 * Turn off echoing (specific to telnet client)
 */
void 
echo_off(struct descriptor_data *d)
{
    char off_string[] =
    {
        (char) IAC,
        (char) WILL,
        (char) TELOPT_ECHO,
        (char) 0,
    };

    SEND_TO_Q(off_string, d);
}


/*
 * Turn on echoing (specific to telnet client)
 */
void 
echo_on(struct descriptor_data *d)
{
    char on_string[] =
    {
        (char) IAC,
        (char) WONT,
        (char) TELOPT_ECHO,
        (char) TELOPT_NAOFFD,
        (char) TELOPT_NAOCRD,
        (char) 0,
    };

    SEND_TO_Q(on_string, d);
}


int 
_parse_name(char *arg, char *name)
{
    int i;

    /* skip whitespaces */
    for (; isspace(*arg); arg++);

    for (i = 0; (*name = *arg); arg++, i++, name++)
        if (!isalpha(*arg))
            return 1;

    if (!i)
        return 1;

    return 0;
}


/* *************************************************************************
*  Stuff for controlling the non-playing sockets (get name, pwd etc)       *
************************************************************************* */


/* locate entry in p_table with entry->name == name. -1 mrks failed search */
int 
find_name(char *name)
{
    int i;

    for (i = 0; i <= top_of_p_table; i++) {
        if (!str_cmp((player_table + i)->name, name))
            return i;
    }

    return -1;
}



const char *reserved[] =
{
    "self",
    "me",
    "all",
    "room",
    "someone",
    "something",
    "\n"
};

int reserved_word(char *argument)
{
    return (search_block(argument, reserved, TRUE) >= 0);
}

#undef __interpreter_c__
