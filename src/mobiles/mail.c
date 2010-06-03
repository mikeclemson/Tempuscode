/* ************************************************************************
*   File: mail.c                                        Part of CircleMUD *
*  Usage: Internal funcs and player spec-procs of mud-mail system         *
*                                                                         *
*  All rights reserved.  See license.doc for complete information.        *
*                                                                         *
*  Copyright (C) 1993, 94 by the Trustees of the Johns Hopkins University *
*  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
************************************************************************ */
/******* MUD MAIL SYSTEM MAIN FILE ***************************************
Written by Jeremy Elson (jelson@cs.jhu.edu)
Rewritten by John Rothe (forget@tempusmud.com)
*************************************************************************/
//
// File: mail.c                      -- Part of TempusMUD
//
// All modifications and additions are
// Copyright 1998 by John Watson, all rights reserved.
//
#ifdef HAS_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <ctype.h>
#include <string.h>
#include <time.h>
#include <fstream>
#include <stack>
#include <errno.h>

#include "actions.h"
#include "structs.h"
#include "utils.h"
#include "comm.h"
#include "db.h"
#include "interpreter.h"
#include "handler.h"
#include "mail.h"
#include "editor.h"
#include "screen.h"
#include "clan.h"
#include "materials.h"
#include "player_table.h"
#include "accstr.h"

// From cityguard.cc
void call_for_help(struct creature *ch, struct creature *attacker);

using namespace std;

// The vnum of the "letter" object
const int MAIL_OBJ_VNUM  = 1204;

list<struct obj_data *> load_mail(char *path);

// returns 0 for no mail. 1 for mail.
int
has_mail(long id)
{
    fstream mail_file;

    if(! playerIndex.exists(id) )
        return 0;
    mail_file.open(get_mail_file_path(id), ios_in);

    if (!mail_file.is_open())
        return 0;
    return 1;
}

int
can_receive_mail(long id)
{
    long length = 0;
    fstream mail_file;
    if(! playerIndex.exists(id) )
        return 0;
    mail_file.open(get_mail_file_path(id), ios_in);

    if (!mail_file.is_open())
        return 1;
    mail_file.seekg(0, ios_end);
    length = mail_file.tellg();
    mail_file.close();
    if (length >= MAX_MAILFILE_SIZE) {
        purge_mail(id);
    }
    return 1;
}

int
mail_box_status(long id)
{
    // 0 is normal
    // 1 is frozen
    // 2 is buried
    // 3 is deleted
    // 4 is failure

    struct creature *victim;
    int flag = 0;

    victim = new struct creature(true);
    if (!victim->loadFromXML(id)) {
        delete victim;
        return 4;
    }

    if (PLR_FLAGGED(victim, PLR_FROZEN))
        flag = 1;
    if (PLR2_FLAGGED(victim, PLR2_BURIED))
        flag = 2;
    if (PLR_FLAGGED(victim, PLR_DELETED))
        flag = 3;

    delete victim;
    return flag;
}

// Like it says, store the mail.
// Returns 0 if mail not stored.
int
store_mail(long to_id, long from_id, const char *txt, list<string> cc_list,
           struct obj_data *obj_list)
{
    char *mail_file_path;
    FILE *ofile;
    char *time_str, *obj_string = NULL;
    struct obj_data *obj, *temp_o;
    struct stat stat_buf;
    time_t now = time(NULL);
    list<struct obj_data*> mailBag;

    // NO zero length mail!
    // This should never happen.
    if (!txt || !strlen(txt)) {
        send_to_char(get_char_in_world_by_idnum(from_id),
                     "Why would you send a blank message?\r\n");
        return 0;
    }

    // Recipient is frozen, buried, or deleted
    if (!can_receive_mail(to_id)) {
        send_to_char(get_char_in_world_by_idnum(from_id),
                     "%s doesn't seem to be able to receive mail.\r\n",
            playerIndex.getName(to_id));
        return 0;
    }

    if(! playerIndex.exists(to_id) ) {
        errlog("Toss_Mail Error, recipient idnum %ld invalid.", to_id);
        return 0;
    }

    mail_file_path = get_mail_file_path(to_id);

    // If we already have a mail file then we have to read in the mail and write it all back out.
    if (stat(mail_file_path, &stat_buf) == 0)
        mailBag = load_mail(mail_file_path);

    obj = read_object(MAIL_OBJ_VNUM);
    time_str = asctime(localtime(&now));
    *(time_str + strlen(time_str) - 1) = '\0';

	acc_string_clear();
    acc_sprintf(" * * * *  Tempus Mail System  * * * *\r\n"
		"Date: %s\r\n  To: %s\r\nFrom: %s",
		time_str, playerIndex.getName(to_id), playerIndex.getName(from_id));

    if (!cc_list.empty()) {
		list<string>_iterator si;

		for (si = cc_list.begin(); si != cc_list.end(); si++)
			acc_strcat((si == cc_list.begin()) ? "\r\n  CC: ":", ",
				si->c_str(), NULL);
	}
    if (obj_list) {
        acc_strcat("\r\nPackages attached to this mail:\r\n", NULL);
        temp_o = obj_list;
        while (temp_o) {
            obj_string = tmp_sprintf("  %s\r\n", temp_o->name);
            acc_strcat(obj_string, NULL);
            temp_o = temp_o->next_content;
        }
    }
	acc_strcat("\r\n\r\n", txt, NULL);

    obj->action_desc = strdup(acc_get_string());

    obj->plrtext_len = strlen(obj->action_desc) + 1;
    mailBag.push_back(obj);

    if ((ofile = fopen(mail_file_path, "w")) == NULL) {
        errlog("Unable to open xml mail file '%s': %s",
             mail_file_path, strerror(errno) );
        return 0;
    }
    else {
        list<struct obj_data*>_iterator oi;

        fprintf(ofile, "<objects>");
        for (oi = mailBag.begin(); oi != mailBag.end(); oi++) {
            (*oi)->saveToXML(ofile);
            extract_obj(*oi);
        }
        if (obj_list) {
            temp_o = obj_list;
            while (temp_o) {
                temp_o->saveToXML(ofile);
                extract_obj(temp_o);
                temp_o = temp_o->next_content;
            }
        }
        fprintf(ofile, "</objects>");
        fclose(ofile);
    }

    return 1;
}

int
purge_mail(long idnum)
{
    fstream mail_file;
    mail_file.open(get_mail_file_path(idnum), ios_in);
    if (!mail_file.is_open()) {
        return 0;
    }
    mail_file.close();
    remove(get_mail_file_path(idnum));
    return 1;
}

// Pull the mail out of the players mail file if he has one.
// Create the "letters" from the file, and plant them on him without
//     telling him.  We'll let the spec say what it wants.
// Returns the number of mails received.
int
receive_mail(struct creature * ch, list<struct obj_data *> &olist)
{
    int num_letters = 0;
    int counter = 0;
    char *path = get_mail_file_path( GET_IDNUM(ch) );
    bool container = false;
    list<struct obj_data *> mailBag;

    mailBag = load_mail(path);

    if (mailBag.size() > MAIL_BAG_THRESH )
        container = true;

    list<struct obj_data *>_iterator oi;

    struct obj_data *obj = NULL;
    if (container)
        obj = read_object(MAIL_BAG_OBJ_VNUM);

    for (oi = mailBag.begin(); oi != mailBag.end(); oi++) {
        counter++;

        if (GET_OBJ_VNUM(*oi) == MAIL_OBJ_VNUM)
            num_letters++;
        else
            olist.push_back((*oi));

        if ((counter > MAIL_BAG_OBJ_CONTAINS) && container) {
            obj_to_char(obj, ch);
            obj = read_object(MAIL_BAG_OBJ_VNUM);
            counter = 0;
        }
        if (obj) {
            obj_to_obj(*oi, obj);
        }
        else {
            obj_to_char(*oi, ch);
        }
    }

    if (obj)
        obj_to_char(obj, ch);

    unlink(path);

   return num_letters;
}

list<struct obj_data *> load_mail(char *path)
{
	int axs = access(path, W_OK);
    list<struct obj_data *> mailBag;

	if( axs != 0 ) {
		if( errno != ENOENT ) {
			errlog("Unable to open xml mail file '%s': %s",
				 path, strerror(errno) );
			return mailBag;
		} else {
			return mailBag; // normal no eq file
		}
	}
    xmlDocPtr doc = xmlParseFile(path);
    if (!doc) {
        errlog("XML parse error while loading %s", path);
        return mailBag;
    }

    xmlNodePtr root = xmlDocGetRootElement(doc);
    if (!root) {
        xmlFreeDoc(doc);
        errlog("XML file %s is empty", path);
        return mailBag;
    }

	for ( xmlNodePtr node = root->xmlChildrenNode; node; node = node->next ) {
        if ( xmlMatches(node->name, "object") ) {
			struct obj_data *obj = create_obj();
			if(! obj->loadFromXML(NULL, NULL, NULL, node) ) {
				extract_obj(obj);
			}
            else {
                mailBag.push_back(obj);
            }
        }
	}

    xmlFreeDoc(doc);

    return mailBag;
}

/*****************************************************************
** Below is the spec_proc for a postmaster using the above       **
** routines.  Written by Jeremy Elson (jelson@server.cs.jhu.edu) **
** Fixed by John Rothe (forget@tempusmud.com) and changes owned  **
** John Watson.                                                  **
*****************************************************************/

SPECIAL(postmaster)
{
	if (spec_mode == SPECIAL_TICK) {
		if (me->to_c()->isFighting() && !number(0, 4)) {
			call_for_help(me->to_c(), me->to_c()->findRandomCombat());
			return 1;
		}
		return 0;
	}

    if( spec_mode != SPECIAL_CMD )
        return 0;

    if (!ch || !ch->desc || IS_NPC(ch))
        return 0;                /* so mobs don't get caught here */

    if (!(CMD_IS("mail") || CMD_IS("check") || CMD_IS("receive")))
        return 0;

    if (CMD_IS("mail")) {
        postmaster_send_mail(ch, me->to_c(), argument);
        return 1;
    } else if (CMD_IS("check")) {
        postmaster_check_mail(ch, me->to_c());
        return 1;
    } else if (CMD_IS("receive")) {
        postmaster_receive_mail(ch, me->to_c());
        return 1;
    } else
        return 0;
}

void
postmaster_send_mail(struct creature *ch, struct creature *mailman,
    char *arg)
{
    long recipient;
    char buf[MAX_STRING_LENGTH];
    struct mail_recipient_data *mail_list, *n_mail_to;
    int total_cost = 0;
    struct clan_data *clan = NULL;
    struct clanmember_data *member = NULL;
    int status = 0;
    char **tmp_char = NULL;

    if (GET_LEVEL(ch) < MIN_MAIL_LEVEL) {
        sprintf(buf2, "Sorry, you have to be level %d to send mail!",
            MIN_MAIL_LEVEL);
        perform_tell(mailman, ch, buf2);
        return;
    }
    arg = one_argument(arg, buf);

    if (!*buf) {                /* you'll get no argument from me! */
        strcpy(buf2, "You need to specify an addressee!");
        perform_tell(mailman, ch, buf2);
        return;
    }

    mail_list = NULL;

    if (!strcasecmp(buf, "clan")) {
        if (!(clan = real_clan(GET_CLAN(ch)))) {
            perform_tell(mailman, ch, "You are not a member of any clan!");
            return;
        }
        for (member = clan->member_list; member; member = member->next) {
            if (!member->no_mail) {
                total_cost += STAMP_PRICE;
                CREATE(n_mail_to, struct mail_recipient_data, 1);
                n_mail_to->next = mail_list;
                n_mail_to->recpt_idnum = member->idnum;
                mail_list = n_mail_to;
            }
        }
    } else {

        while (*buf) {
            if ((recipient = playerIndex.getID(buf)) <= 0) {
                perform_tell(mailman, ch,
					tmp_sprintf("No one by the name '%s' is registered here!",
						buf));
            } else if ((status = mail_box_status(recipient)) > 0) {
                // 0 is normal
                // 1 is frozen
                // 2 is buried
                // 3 is deleted
                switch (status) {
                case 1:
                    sprintf(buf2, "%s's mailbox is frozen shut!", buf);
                    break;
                case 2:
                    sprintf(buf2, "%s is buried! Go put it on their grave!",
                        buf);
                    break;
                case 3:
                    sprintf(buf2,
                        "No one by the name '%s' is registered here!", buf);
                    break;
                default:
                    sprintf(buf2,
                        "I don't have an address for %s. Try back later!",
                        buf);
                }
                perform_tell(mailman, ch, buf2);
            } else {
                if (recipient == 1)    // fireball
                    total_cost += 1000000;
                else
                    total_cost += STAMP_PRICE;

                CREATE(n_mail_to, struct mail_recipient_data, 1);
                n_mail_to->next = mail_list;
                n_mail_to->recpt_idnum = recipient;
                mail_list = n_mail_to;
            }
            arg = one_argument(arg, buf);
        }
    }
    if (!total_cost || !mail_list) {
        perform_tell(mailman, ch,
            "Sorry, you're going to have to specify some valid recipients!");
        return;
    }
    // deduct cost of mailing
    if (GET_LEVEL(ch) < LVL_AMBASSADOR) {

        // gold
        if (ch->in_room->zone->time_frame != TIME_ELECTRO) {
            if (GET_GOLD(ch) < total_cost) {
                sprintf(buf2, "The postage will cost you %d coins.",
                    total_cost);
                perform_tell(mailman, ch, buf2);
                strcpy(buf2, "...which I see you can't afford.");
                perform_tell(mailman, ch, buf2);
                while ((n_mail_to = mail_list)) {
                    mail_list = n_mail_to->next;
                    free(n_mail_to);
                }
                return;
            }
            GET_GOLD(ch) -= total_cost;
        } else {                // credits
            if (GET_CASH(ch) < total_cost) {
                sprintf(buf2, "The postage will cost you %d credits.",
                    total_cost);
                perform_tell(mailman, ch, buf2);
                strcpy(buf2, "...which I see you can't afford.");
                perform_tell(mailman, ch, buf2);
                while ((n_mail_to = mail_list)) {
                    mail_list = n_mail_to->next;
                    free(n_mail_to);
                }
                return;
            }
            GET_CASH(ch) -= total_cost;
        }
    }

    act("$n starts to write some mail.", true, ch, 0, 0, TO_ROOM);
    sprintf(buf2, "I'll take %d coins for the postage.", total_cost);
    perform_tell(mailman, ch, buf2);

    tmp_char = reinterpret_cast<char **>(malloc(sizeof(char *)));
    *(tmp_char) = NULL;

    start_editing_mail(ch->desc, mail_list);
}

void
postmaster_check_mail(struct creature *ch, struct creature *mailman)
{
    char buf2[256];

    if (has_mail(GET_IDNUM(ch))) {
        strcpy(buf2, "You have mail waiting.");
    } else
        strcpy(buf2, "Sorry, you don't have any mail waiting.");
    perform_tell(mailman, ch, buf2);
}

void
postmaster_receive_mail(struct creature *ch, struct creature *mailman)
{
    char *to_char = NULL, *to_room = NULL;
    int num_mails = 0;
    list<struct obj_data *> olist;

    if (!has_mail(GET_IDNUM(ch))) {
        to_char = tmp_sprintf("Sorry, you don't have any mail waiting.");
        perform_tell(mailman, ch, to_char);
        return;
    }

    num_mails = receive_mail(ch, olist);

    if (num_mails == 0) {
        to_char = tmp_sprintf("Sorry, you don't have any mail waiting.");
        perform_tell(mailman, ch, to_char);
        return;
    }
    else if (num_mails > MAIL_BAG_THRESH) {
        num_mails = num_mails / MAIL_BAG_OBJ_CONTAINS + 1;
        to_char = tmp_sprintf("$n gives you %d bag%s of mail", num_mails,
               (num_mails > 1 ? "s" : ""));
        to_room = tmp_sprintf("$n gives $N %d bag%s of mail", num_mails,
               (num_mails > 1 ? "s" : ""));
    }
    else {
        to_char = tmp_sprintf("$n gives you %d piece%s of mail", num_mails,
            (num_mails > 1 ? "s" : ""));
        to_room = tmp_sprintf("$n gives $N %d piece%s of mail", num_mails,
            (num_mails > 1 ? "s" : ""));
    }

    if (olist.size()) {
        if (olist.size() > 1) {
            to_room = tmp_strcat(to_room, " and some packages.", NULL);
        }
        else {
            to_room = tmp_strcat(to_room, " and a package.", NULL);
        }

        list<struct obj_data *>_iterator li = olist.begin();
        unsigned counter = 0;
        for (; li != olist.end(); ++li) {
            counter++;
            if (counter == olist.size()) {
                to_char = tmp_strcat(to_char, " and ", (*li)->name, ".", NULL);
            }
            else {
                to_char = tmp_strcat(to_char, ", ", (*li)->name, NULL);
            }
        }
    }
    else {
        to_char = tmp_strcat(to_char, ".", NULL);
        to_room = tmp_strcat(to_room, ".", NULL);
    }

    act(to_char, false, mailman, 0, ch, TO_VICT);
    act(to_room, false, mailman, 0, ch, TO_NOTVICT);

    save_player_to_xml(ch);
}