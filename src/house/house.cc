//
// File: house.c                     -- Part of TempusMUD
//
// Copyright 1998 by John Watson, all rights reserved.
//

/* ************************************************************************
*   File: house.c                                       Part of TempusMUD *
*         See header file: house.h                                        *
*                                                                         *
*  CircleMUD:                                                             *
*  All rights reserved.  See license.doc for complete information.        *
*  Copyright (C) 1993, 94 by the Trustees of the Johns Hopkins University *
*  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
************************************************************************ */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <time.h>
#include <errno.h>


#include "structs.h"
#include "comm.h"
#include "handler.h"
#include "db.h"
#include "interpreter.h"
#include "utils.h"
#include "house.h"
#include "screen.h"

#define NAME(x) ((temp = get_name_by_id(x)) == NULL ? "<UNDEF>" : temp)

extern struct room_data *world;
extern struct index_data *obj_index;
extern struct descriptor_data *descriptor_list;
extern struct obj_data *obj_proto;	/* prototypes for objs		 */
extern int no_plrtext;

struct obj_data *Obj_from_store(FILE * fl, bool allow_inroom);
int Obj_to_store(struct obj_data * obj, FILE * fl);

struct house_control_rec house_control[MAX_HOUSES];
int num_of_houses = 0;

int
can_edit_house( struct char_data *ch, struct house_control_rec *house )
{
    struct char_file_u tmp_store;
    struct char_data *vict;
    int player_i=0;
    CHAR cbuf;

    if ( GET_IDNUM( ch ) == house->landlord )
	return 1;
    
    clear_char(&cbuf);
    
    if ( ( player_i = load_char( get_name_by_id( house->landlord ), &tmp_store ) ) > -1 ) {
	store_to_char( &tmp_store, &cbuf );
	vict = &cbuf;
    }
    else
	return 1;
    
    if ( GET_LEVEL( vict ) > GET_LEVEL( ch ) )
	return 0;

    return 1;
}

int 
recurs_obj_cost (struct obj_data * obj, bool mode, struct obj_data *top_o )
{
    if (obj)
	if (obj->in_obj && obj->in_obj != top_o) {
	    if (!mode) {         /** rent mode **/
		return ((IS_OBJ_STAT(obj, ITEM_NORENT) ? 0 : GET_OBJ_RENT(obj)) +
			recurs_obj_cost(obj->contains, mode, top_o) + 
			recurs_obj_cost(obj->next_content, mode, top_o));
	    } else              /** cost mode **/
		return (GET_OBJ_COST(obj) + recurs_obj_cost(obj->contains, mode, top_o)
			+ recurs_obj_cost(obj->next_content, mode, top_o));
	} else if (!mode) // rent mode
	    return ((IS_OBJ_STAT(obj, ITEM_NORENT) ? 0 : GET_OBJ_RENT(obj)) +
		    recurs_obj_cost(obj->contains, mode, top_o));
	else
	    return (GET_OBJ_COST(obj) + recurs_obj_cost(obj->contains, mode, top_o));

    return 0;
}

int
recurs_obj_contents(struct obj_data *obj, struct obj_data *top_o) 
{
    if (!obj)
	return 0;

    if (obj->in_obj && obj->in_obj != top_o)
	return (1 + recurs_obj_contents(obj->next_content, top_o) + 
		recurs_obj_contents(obj->contains, top_o));
  
    return (1 + recurs_obj_contents(obj->contains, top_o));
}

int 
House_get_filename(int atrium, char *filename)
{
    if (atrium < 0)
	return 0;

    sprintf(filename, "house/%d.house", atrium);
    return 1;
}

int 
HouseDoor_get_filename(int atrium, char *filename)
{
    if (atrium < 0)
	return 0;

    sprintf(filename, "house/doors/%d.door", atrium);
    return 1;
}

int 
find_house(room_num vnum)
{
    int i, j;

    for (i = 0; i < num_of_houses; i++)
	for (j = 0; j <= house_control[i].num_of_rooms; j++)
	    if (house_control[i].house_rooms[j] == vnum)
		return i;

    return -1;
}

struct house_control_rec *
real_house( room_num vnum )
{

    int i;

    if ( ( i = find_house( vnum ) ) >= 0 )
	return ( house_control + i );
	
    return 0;
}
void 
House_checkrent()
{
    int i, j;
    struct descriptor_data *d = NULL;
    struct room_data *rm = NULL;
    struct obj_data *obj = NULL;

    for (i = 0; i < num_of_houses; i++) {
	if (house_control[i].mode == HOUSE_PUBLIC)
	    continue;
	for (d = descriptor_list; d; d = d->next) {
	    if (d->character && (GET_IDNUM(d->character)==house_control[i].owner1 ||
				 GET_IDNUM(d->character)==house_control[i].owner2)) {
		break;
	    }
	}
	if (!d) {
	    if (house_control[i].rent_time >= HOUSE_PASSES_CHECK) {
		house_control[i].hourly_rent_sum /= (72 * HOUSE_PASSES_CHECK);
		house_control[i].rent_sum += house_control[i].hourly_rent_sum;
		if (house_control[i].mode == HOUSE_RENTAL)
		    house_control[i].rent_sum += house_control[i].rent_rate / 72;
		house_control[i].hourly_rent_sum = 0;
		house_control[i].rent_time = 0;
	    }
	
	    house_control[i].rent_time += HOUSE_PASS;
	    for (j = 0; j < house_control[i].num_of_rooms; j++) {
		if ((rm = real_room(house_control[i].house_rooms[j]))) {
		    for (obj = rm->contents; obj; obj = obj->next_content) {
			house_control[i].hourly_rent_sum += recurs_obj_cost(obj, false, NULL);
		    }
		}
	    }
	}
    }
}

void
House_count_obj(struct obj_data *obj)
{
    if (!obj)
	return;

    House_count_obj(obj->contains);
    House_count_obj(obj->next_content);

    // don't count NORENT items as being in house
    if (obj->shared->proto && !IS_OBJ_STAT(obj, ITEM_NORENT)) 
	obj->shared->house_count++;
}

void 
House_countobjs()
{
    struct obj_data *obj = NULL;
    struct room_data *rm = NULL;
    int i, j;

    for (obj = obj_proto; obj; obj = obj->next)
	obj->shared->house_count = 0;
  
    for (i = 0; i < num_of_houses; i++) {
	if (house_control[i].mode == HOUSE_PUBLIC)
	    continue;
	for (j = 0; j < house_control[i].num_of_rooms; j++) {
	    if ((rm = real_room(house_control[i].house_rooms[j]))) {
		House_count_obj(rm->contents);
	    }
	}
    }
}
    
int 
House_rentcost(int atrium)
{
    int j, pos, sum = 0;
    struct obj_data *obj = NULL;
    struct room_data *rm = NULL;

    if ((pos = find_house(atrium)) == -1)
	return 0;

    for (j = 0; j < house_control[pos].num_of_rooms; j++) {
	if ((rm = real_room(house_control[pos].house_rooms[j]))) {
	    for (obj = rm->contents; obj; obj = obj->next_content)
		sum += recurs_obj_cost(obj, false, NULL);
	}
    }
    if (house_control[pos].mode == HOUSE_RENTAL)
	sum += house_control[pos].rent_rate;

    return (sum);
}

void 
extract_norent (struct obj_data *obj)
{
    if (obj)  {
	extract_norent (obj->contains);
	if (obj->in_obj)
	    extract_norent (obj->next_content);
	if (IS_OBJ_STAT(obj,ITEM_NORENT) ||
	    (OBJ_TYPE(obj, ITEM_KEY) && !GET_OBJ_VAL(obj, 1)))
	    extract_obj (obj);
    }
}

/* Load all objects for a house */
int 
House_load(int atrium)
{
    FILE *fl;
    char fname[MAX_STRING_LENGTH];
    struct obj_data * tmpo;
    int pos;
    struct room_data *rnum;

    if ((pos = find_house(atrium)) == -1)
	return 0;
    if (house_control[pos].house_rooms[0] != atrium)
	return 0;
    if ((rnum = real_room(atrium)) == NULL)
	return 0;
    if (!House_get_filename(atrium, fname))
	return 0;
    if (!(fl = fopen(fname, "r"))) {
	/* no file found */
	return 0;
    }

    while (!feof(fl)) {

	tmpo = Obj_from_store( fl, true );

	if (!tmpo || !(rnum = tmpo->in_room)) 
	    return 0;

	if (IS_OBJ_STAT(tmpo,ITEM_NORENT) ||
	    (OBJ_TYPE(tmpo, ITEM_KEY) && !GET_OBJ_VAL(tmpo, 1))) {
	    extract_obj (tmpo);
	    continue;
	}
	else
	    extract_norent (tmpo);

	tmpo->in_room = NULL;
	obj_to_room(tmpo, rnum);
	if (ferror(fl)) {
	    perror("Reading house file: House_load.");
	    fclose(fl);
	    return 0;
	}
    }
  
    fclose(fl);
    return 1;
}

int
HouseDoor_load(int atrium)
{
    FILE *fl;
    char fname[MAX_STRING_LENGTH];
    int pos;
    struct room_data *rnum = NULL;
    struct room_data *other_room = NULL;
    house_door_elem file_door;

    if ((pos = find_house(atrium)) == -1)
	return 0;
    if (house_control[pos].house_rooms[0] != atrium)
	return 0;
    if ((rnum = real_room(atrium)) == NULL)
	return 0;
    if (!HouseDoor_get_filename(atrium, fname))
	return 0;
    if (!(fl = fopen(fname, "r")))      // file does not exist, no problem
	return 0;
    
    while (!feof(fl)) {
	if (!fread(&file_door, sizeof(house_door_elem), 1, fl))
	    break;
    
	if (!(other_room = real_room(file_door.room_num)))
	    continue;
    
	if (!ABS_EXIT(other_room, file_door.dir))
	    continue;
    
	SET_BIT(ABS_EXIT(other_room, file_door.dir)->exit_info, file_door.flags);
    }
    fclose(fl);
    return 1;
}
/* returns true if obj or one of its contents is norent */
int 
house_no_rent (struct obj_data * obj)
{
    if (obj)
	if (IS_OBJ_STAT(obj, ITEM_NORENT))
	    return 1;
	else if (obj->plrtext_len && no_plrtext)
	    return 1;
	else
	    if (obj->in_obj)
		return house_no_rent(obj->contains) + house_no_rent(obj->next_content);
	    else
		return house_no_rent(obj->contains);

    return 0;
}

int 
House_save(struct obj_data * obj, FILE * fp)
{
    if (obj) {
	House_save(obj->next_content, fp);
	if (!Obj_to_store(obj, fp)) {
	    return 0;
	}
    }
    return 1;
}

int
HouseDoor_save(struct room_data *room, FILE *fp, int index)
{
    int dir;
    struct room_data *toroom = NULL;
    struct room_direction_data *exit = NULL;
    house_door_elem file_door;
  
    for (dir = 0; dir < NUM_DIRS; dir++) {

	if ((exit = ABS_EXIT(room,  dir)) &&
	    IS_SET(exit->exit_info, EX_ISDOOR)) {

	    file_door.room_num = room->number;
	    file_door.dir      = dir;
	    file_door.flags    = exit->exit_info;
      
	    if (!fwrite(&file_door, sizeof(house_door_elem), 1, fp)) {
		perror("SYSERR: writing door file");
		return 0;
	    }
      
	    if ((toroom = ABS_EXIT(room, dir)->to_room) &&
		find_house(toroom->number) != index &&
		(exit = ABS_EXIT(toroom, rev_dir[dir])) &&
		room == exit->to_room) {

		file_door.room_num = toroom->number;
		file_door.dir      = dir;
		file_door.flags    = exit->exit_info;
		if (!fwrite(&file_door, sizeof(house_door_elem), 1, fp)) {
		    perror("SYSERR: writing door file");
		    return 0;
		}
		return 2;
	    }
	    else
		return 1;
	}
    }
    return 0;
}
/* Save all objects in a house */
void 
House_crashsave(int vnum)
{
    int pos, i, atrium;
    int door_count = 0;
    struct room_data *rnum;
    char buf[MAX_STRING_LENGTH];
    char buf2[MAX_STRING_LENGTH];
    FILE *fp=NULL, *fp_door=NULL;

    if ((pos = find_house(vnum)) == -1)
	return;
    atrium = house_control[pos].house_rooms[0];

    // open house object file
    if (!House_get_filename(atrium, buf))
	return;
    if (!(fp = fopen(buf, "wb"))) {
	perror("SYSERR: Error saving house file");
	return;
    }

    // open house door file
    if (HouseDoor_get_filename(atrium, buf2) &&
	(!(fp_door = fopen(buf2, "w")))) {
	sprintf(buf, "SYSERR: Error saving door file '%s'", buf2);
	perror(buf);
    }

    for(i = 0; i < house_control[pos].num_of_rooms; i++)  {
	if ((rnum=real_room(house_control[pos].house_rooms[i])) == NULL)  {
	    fclose(fp);
	    return;
	}

	// save objects
	if (House_save(rnum->contents, fp)) {
	    REMOVE_BIT(ROOM_FLAGS(rnum), ROOM_HOUSE_CRASH);
	}
	// save doors
	if (fp_door)
	    door_count += HouseDoor_save(rnum, fp_door, pos);
    }
    fclose(fp);
    if (fp_door) {
	fclose(fp_door);
	if (!door_count) { // no doors were written, delete the 0 length file
	    unlink(buf2);
	}
    }
}


/* Delete a house save file */
void 
House_delete_file(int atrium)
{
    char buf[MAX_INPUT_LENGTH], fname[MAX_INPUT_LENGTH];
    FILE *fl;

    if (!House_get_filename(atrium, fname))
	return;
    if (!(fl = fopen(fname, "rb"))) {
	if (errno != ENOENT) {
	    sprintf(buf, "SYSERR: Error deleting house file #%d. (1)", atrium);
	    perror(buf);
	}
	return;
    }
    fclose(fl);
    if (unlink(fname) < 0) {
	sprintf(buf, "SYSERR: Error deleting house file #%d. (2)", atrium);
	perror(buf);
    }
}


/* List all objects in a house file */
void 
House_listrent(struct char_data * ch, int vnum)
{
}




/******************************************************************
 *  Functions for house administration (creation, deletion, etc.  *
 *****************************************************************/


/* Save the house control information */
void 
House_save_control(void)
{
    FILE *fl;

    if (!(fl = fopen(HCONTROL_FILE, "wb"))) {
	perror("SYSERR: Unable to open house control file");
	return;
    }
    // write all the house control recs in one fell swoop.  Pretty nifty, eh?
    fwrite(house_control, sizeof(struct house_control_rec), num_of_houses, fl);

    fclose(fl);
}


/* call from boot_db - will load control recs, load objs, set atrium bits */
/* should do sanity checks on vnums & remove invalid records */
void 
House_boot(void)
{
    struct house_control_rec temp_house;
    struct room_data *rnum, *real_atrium;
    FILE *fl;
    int tmpsize, i;

    memset((char *)house_control,0,sizeof(struct house_control_rec)*MAX_HOUSES);

    if (!(fl = fopen(HCONTROL_FILE, "rb"))) {
	slog("House control file does not exist.");
	return;
    }
    while (!feof(fl) && num_of_houses < MAX_HOUSES) {

	tmpsize = fread(&temp_house, sizeof(struct house_control_rec), 1, fl);

	if (tmpsize < 1)
	    break;
    
	// make sure none of the files are messed up
	temp_house.num_of_guests = MIN(temp_house.num_of_guests, MAX_GUESTS);
	
	temp_house.rent_time =            0;
	temp_house.hourly_rent_sum =      0;

	if (get_name_by_id(temp_house.owner1) == NULL)
	    if (get_name_by_id(temp_house.owner2) == NULL)
		temp_house.owner1 = 0; 
	    else  {
		temp_house.owner1 = temp_house.owner2; 
		temp_house.owner2 = -1;
	    }

	if ((temp_house.owner2 != -1) && 
	    (get_name_by_id(temp_house.owner2) == NULL))
	    temp_house.owner2 = -1;

    
	if ((real_atrium = real_room(temp_house.house_rooms[0])) == NULL) {
	    sprintf(buf, "Atrium %d does not exist.", temp_house.house_rooms[0]);
	    slog(buf);
	    continue;		 /*      house doesn't have an atrium -- skip  */
	}

	if ((find_house(temp_house.house_rooms[0])) >= 0) 
	    continue;			/* this vnum is already a house -- skip */

	house_control[num_of_houses++] = temp_house;

	for (i=0; i<temp_house.num_of_rooms; i++)  {
	    if ((rnum = real_room(temp_house.house_rooms[i])) == NULL) {
		sprintf(buf, "House room %d of house %d does not exist.", 
			temp_house.house_rooms[i], temp_house.house_rooms[0]);
		slog(buf);
	    } else
		SET_BIT(ROOM_FLAGS(rnum), ROOM_HOUSE);
	}

	SET_BIT(ROOM_FLAGS(real_atrium), ROOM_ATRIUM);

	House_load(temp_house.house_rooms[0]);
	HouseDoor_load(temp_house.house_rooms[0]);
    }

    fclose(fl);

    House_save_control();
}



// usage message
#define HCONTROL_DESTROY_FORMAT \
"Usage: hcontrol destroy <atrium vnum>\r\n"
#define HCONTROL_ADD_FORMAT \
"Usage: hcontrol add    <atrium vnum> room/guest/owner <newroom/name>\r\n"
#define HCONTROL_DELETE_FORMAT \
"Usage: hcontrol delete <atrium vnum> room/guest/owner <newroom/name>\r\n"
#define HCONTROL_SET_FORMAT \
"Usage: hcontrol set <atrium vnum> [rate|current|mode|landlord] public/private\r\n"
#define HCONTROL_SHOW_FORMAT \
"Usage: hcontrol show [guests/rooms/all <atrium vnum>]\r\n" \
"                     [+/-atrium +/-rooms +/-build +/-owner +/-co-owner\r\n" \
"                      +/-mode +/-lag +/-rate +/-current +/-landlord]\r\n"
#define HCONTROL_BUILD_FORMAT \
"Usage: hcontrol build <player name> <atrium vnum> <top vnum>\r\n"

#define HCONTROL_FORMAT \
( HCONTROL_BUILD_FORMAT \
  HCONTROL_DESTROY_FORMAT \
  HCONTROL_ADD_FORMAT \
  HCONTROL_DELETE_FORMAT \
  HCONTROL_SET_FORMAT \
  HCONTROL_SHOW_FORMAT \
  "Usage: hcontrol save/recount\r\n" \
  "Usage: hcontrol where\r\n" )
  

void 
print_room_contents_to_buf(struct char_data *ch, char *buf, 
			   struct room_data *real_house_room)
{
    static char tmpbuf[256];
    int count;

    struct obj_data *obj, *cont;
    if (PRF_FLAGGED(ch, PRF_HOLYLIGHT))
	sprintf(buf2, " %s[%s%5d%s]%s", CCGRN(ch, C_NRM),
		CCNRM(ch, C_NRM), real_house_room->number, CCGRN(ch, C_NRM),
		CCNRM(ch, C_NRM));
    else
	strcpy(buf2, "");

    sprintf(tmpbuf, "Room%s: %s%s%s\r\n", buf2, 
	    CCCYN(ch, C_NRM), real_house_room->name, CCNRM(ch, C_NRM));
    strcat(buf,tmpbuf);

    for (obj = real_house_room->contents; obj; obj = obj->next_content) {
	count = recurs_obj_contents(obj, NULL) - 1;
	if (count > 0)
	    sprintf(buf2, "     (contains %d)\r\n", count);
	else
	    strcpy(buf2, "\r\n");
	sprintf(tmpbuf, "   %s%-35s%s  %10d Au%s", CCGRN(ch, C_NRM),
		obj->short_description, CCNRM(ch, C_NRM), 
		recurs_obj_cost(obj, false, NULL), buf2);
	strcat(buf,tmpbuf);
	if (obj->contains) {
	    for (cont = obj->contains; cont; cont = cont->next_content)  {
		count = recurs_obj_contents(cont, obj) - 1;
		if (count > 0)
		    sprintf(buf2, "     (contains %d)\r\n", count);
		else
		    strcpy(buf2, "\r\n");
	
		sprintf(tmpbuf, "     %s%-33s%s  %10d Au%s", CCGRN(ch, C_NRM),
			cont->short_description, CCNRM(ch, C_NRM), 
			recurs_obj_cost(cont, false, obj), buf2);
		strcat(buf,tmpbuf);
	    }
	}
    }
}

void 
hcontrol_list_house_rooms(struct char_data *ch, room_num atrium_vnum)
{
    int i, which_house;
    room_num virt_i_room;
    struct room_data *real_i_room;
    char errorbuf[256];

    buf[0] = 0;

    if ((which_house = find_house(atrium_vnum)) < 0) {
	send_to_char("No such house exists.\r\n", ch);
	return;
    }
    for (i = 0; i < house_control[which_house].num_of_rooms; i++) {
	virt_i_room = house_control[which_house].house_rooms[i];
	if ((real_i_room = real_room(virt_i_room)) != NULL) {
	    print_room_contents_to_buf(ch, buf, real_i_room);
	} else {
	    sprintf(errorbuf, "Virtual Room [%5d] of House [%5d] does not exist.",
		    virt_i_room, atrium_vnum);
	    slog(errorbuf);
	}
    }
    page_string(ch->desc, buf, 0);
}

void 
hcontrol_list_house_guests(struct char_data *ch, room_num vnum)
{
    int i, j;
    char *temp;

    if ((i = find_house(vnum)) < 0) {
	send_to_char("No such house exists.\r\n", ch);
	return;
    }

    if (house_control[i].num_of_guests) {
	strcpy(buf, "     Guests: ");
	for (j = 0; j < house_control[i].num_of_guests; j++) {
	    sprintf(buf2, "%s ", NAME(house_control[i].guests[j]));
	    strcat(buf, CAP(buf2));
	}
	strcat(buf, "\r\n");
	page_string(ch->desc, buf, 0);
    }
    else
	send_to_char("No guests defined.\r\n",ch);
}

#define HC_LIST_HOUSES_ATRIUM    0x001
#define HC_LIST_HOUSES_ROOMS     0x002
#define HC_LIST_HOUSES_BUILD     0x004
#define HC_LIST_HOUSES_OWNER     0x008
#define HC_LIST_HOUSES_MODE      0x010
#define HC_LIST_HOUSES_LAG       0x020
#define HC_LIST_HOUSES_RATE      0x040
#define HC_LIST_HOUSES_CURRENT   0x080
#define HC_LIST_HOUSES_LANDLORD  0x100
#define HC_LIST_HOUSES_CO_OWNER  0x200

const char *hc_list_houses_args[] = {
    "atrium",
    "rooms",
    "build",
    "owner",
    "mode",
    "lag",
    "rate",
    "current",
    "landlord",
    "co-owner",
    "\n"
};
#define HC_LIST_HOUSES_DEFAULT ( HC_LIST_HOUSES_ATRIUM | HC_LIST_HOUSES_ROOMS | HC_LIST_HOUSES_OWNER | HC_LIST_HOUSES_MODE | HC_LIST_HOUSES_LANDLORD )

int
hcontrol_list_houses_modebits(CHAR *ch,  char *args )
{
    int retval = HC_LIST_HOUSES_DEFAULT;
    char tmparg[MAX_INPUT_LENGTH];
    int index;
    
    if ( ! strncasecmp( args, "all", 3 ) )
	args = one_argument( args, tmparg );

    for ( ; ; ) {
	
	args = one_argument( args, tmparg );
	
	if ( !*tmparg || !*( tmparg + 1 ) )
	    break;

	if ( *tmparg != '+' && *tmparg != '-' ) {
	    send_to_char( "Modifiers must be + or -.\r\n", ch );
	    continue;
	}

	if ( ( index = search_block( tmparg + 1, hc_list_houses_args, 0 ) ) < 0 ) {
	    sprintf( buf, "Unknown hc show all argument: '%s'\r\n", tmparg+1 );
	    send_to_char( buf, ch );
	    continue;
	}

	if ( *tmparg == '+' )
	    SET_BIT( retval, ( 1 << index ) );
	else if ( *tmparg == '-' )
	    REMOVE_BIT( retval, ( 1 << index ) );
	else
	    send_to_char( "To set or remove a bit, use + or -.\r\n", ch );

    }
    
    return retval;
}
    
void
hcontrol_list_houses(struct char_data * ch, char *args )
{
    int i;
    char *timestr, *temp;
    char built_on[50], own_name[50], mode_buf[8];
    int modebits;
    char outbuf[ MAX_STRING_LENGTH];
    struct house_control_rec *h = 0;

    if (!num_of_houses) {
	send_to_char("No houses have been defined.\r\n", ch);
	return;
    }
    
    modebits = hcontrol_list_houses_modebits( ch, args );

    *buf = 0;
    *buf2 = 0;
    *outbuf = 0; 
    if ( IS_SET( modebits, HC_LIST_HOUSES_ATRIUM ) ) {
	strcat( buf,  "Atrium " );
	strcat( buf2, "------ " );
    }
    if ( IS_SET( modebits, HC_LIST_HOUSES_ROOMS ) ) {
	strcat( buf,  "Rooms " );
	strcat( buf2, "----- " );
    }
    if ( IS_SET( modebits, HC_LIST_HOUSES_BUILD ) ) {
	strcat( buf,  "Build  " );
	strcat( buf2, "------ " );
    }
    if ( IS_SET( modebits, HC_LIST_HOUSES_OWNER ) ) {
	strcat( buf,  "Owner         " );
	strcat( buf2, "------------- " );
    }
    if ( IS_SET( modebits, HC_LIST_HOUSES_CO_OWNER ) ) {
	strcat( buf,  "Co-Owner      " );
	strcat( buf2, "------------- " );
    }
    if ( IS_SET( modebits, HC_LIST_HOUSES_MODE ) ) {
	strcat( buf,  "Mode " );
	strcat( buf2, "---- " );
    }
    if ( IS_SET( modebits, HC_LIST_HOUSES_LAG ) ) {
	strcat( buf,  "Lag " );
	strcat( buf2, "--- " );
    }
    if ( IS_SET( modebits, HC_LIST_HOUSES_RATE ) ) {
	strcat( buf,  "Rate      " );
	strcat( buf2, "--------- " );
    }
    if ( IS_SET( modebits, HC_LIST_HOUSES_CURRENT ) ) {
	strcat( buf,  "Current    " );
	strcat( buf2, "---------- " );
    }
    if ( IS_SET( modebits, HC_LIST_HOUSES_LANDLORD ) ) {
	strcat( buf,  "Landlord      " );
	strcat( buf2, "------------- " );
    }
    strcat(buf, "\r\n");
    strcat(buf2, "\r\n" );
    
    strcpy( outbuf, buf );
    strcat( outbuf, buf2 );

    for (i = 0; i < num_of_houses; i++) {
	
	h = house_control + i;

	if ( IS_SET( modebits, HC_LIST_HOUSES_ATRIUM ) ) {
	    sprintf( outbuf, "%s%6d ", outbuf, h->house_rooms[ 0 ] );
	}
	if ( IS_SET( modebits, HC_LIST_HOUSES_ROOMS ) ) {
	    sprintf( outbuf, "%s%5d ", outbuf, h->num_of_rooms );
	}
	if ( IS_SET( modebits, HC_LIST_HOUSES_BUILD ) ) {
	    if ( h->built_on ) {
		timestr = asctime( localtime( & ( h->built_on ) ) );
		*( timestr + 10 ) = 0;
		strcpy( built_on, timestr );
		strcpy( built_on, built_on + 4 );
	    } else
		strcpy( built_on, "Unknown" );
	    sprintf( outbuf, "%s%-6s ", outbuf, built_on );
	}
	if ( IS_SET( modebits, HC_LIST_HOUSES_OWNER ) ) {
	    strcpy( own_name, NAME( h->owner1 ) );
	    strcpy( own_name, CAP( own_name ) );
	    sprintf( outbuf, "%s%-13s ", outbuf, own_name );
	}
	if ( IS_SET( modebits, HC_LIST_HOUSES_CO_OWNER ) ) {
	    if ( h->owner2 > 0 )  {
		strcpy( own_name, NAME( h->owner2 ) );
		strcpy( own_name, CAP( own_name ) );
	    }
	    else
		strcpy( own_name, "" );
	    sprintf( outbuf, "%s%-13s ", outbuf, own_name );

	}
	if ( IS_SET( modebits, HC_LIST_HOUSES_MODE ) ) {
	    if ( h->mode == HOUSE_PRIVATE )
		strcpy( mode_buf, "PRV" );
	    else if ( h->mode == HOUSE_RENTAL )
		strcpy( mode_buf, "RNT" );
	    else if ( h->mode == HOUSE_PUBLIC )
		strcpy( mode_buf, "PUB" );
	    else
		strcpy( mode_buf, "" );

	    sprintf( outbuf, "%s%4s ", outbuf, mode_buf );
	}
	if ( IS_SET( modebits, HC_LIST_HOUSES_LAG ) ) {
	    sprintf( outbuf, "%s%3d ", outbuf, h->rent_time );
	}
	if ( IS_SET( modebits, HC_LIST_HOUSES_RATE ) ) {
	    sprintf( outbuf, "%s%9d ", outbuf, House_rentcost( h->house_rooms[0] ) );
	}
	if ( IS_SET( modebits, HC_LIST_HOUSES_CURRENT ) ) {
	    sprintf( outbuf, "%s%9d ", outbuf, h->rent_sum );
	}
	if ( IS_SET( modebits, HC_LIST_HOUSES_LANDLORD ) ) {
	    strcpy( own_name, NAME( h->landlord ) );
	    strcpy( own_name, CAP( own_name ) );
	    sprintf( outbuf, "%s%-15s ", outbuf, own_name );
	}
	
	strcat( outbuf, "\r\n" );

    }

    page_string( ch->desc, outbuf, 1 );
}

void 
hcontrol_build_house(struct char_data * ch, char *arg)
{
    char arg1[MAX_INPUT_LENGTH];
    struct house_control_rec temp_house;
    room_num virt_top_room, virt_atrium;
    struct room_data *real_atrium, *real_house;
    int owner, number_of_rooms = 0, i, j, find_error;

    if (num_of_houses >= MAX_HOUSES) {
	send_to_char("Max houses already defined.\r\n", ch);
	return;
    }
    /* FIRST arg: player's name */ 
    arg = one_argument(arg, arg1);
    if (!*arg1) {
	send_to_char(HCONTROL_FORMAT, ch);
	return;
    }
    if ((owner = get_id_by_name(arg1)) < 0) {
	sprintf(buf, "Unknown player '%s'.\r\n", arg1);
	send_to_char(buf, ch);
	return;
    }
    /* SECOND arg: house's vnum */
    arg = one_argument(arg, arg1);
    if (!*arg1) {
	send_to_char(HCONTROL_FORMAT, ch);
	return;
    }
    virt_atrium = atoi(arg1);
    if ((find_error = find_house(virt_atrium)) >= 0) {
	sprintf(buf, "Room [%d] already exists as room of house [%d]\r\n", 
		virt_atrium, house_control[find_error].house_rooms[0]);
	send_to_char(buf, ch);
	return;
    }
    if ((real_atrium = real_room(virt_atrium)) == NULL) {
	send_to_char("No such room exists.\r\n", ch);
	return;
    }
    /* THIRD arg: Top room of the house.  Inbetween rooms will be added */
    arg = one_argument(arg, arg1);
    if (!*arg1) {
	send_to_char(HCONTROL_FORMAT, ch);
	return;
    }
    virt_top_room = atoi(arg1);
    if (virt_top_room < virt_atrium) {
	send_to_char("Top room number is less than Atrium room number.\r\n", ch);
	return;
    }
    for (i=0,j=0; i < (virt_top_room-virt_atrium+1); i++) {
	if ((real_house = real_room(virt_atrium + i)) != NULL) {
	    if ((find_error = find_house(virt_atrium + i)) >= 0) {
		sprintf(buf, "Room [%d] already exits as room of house [%d]\r\n", 
			virt_atrium + i, house_control[find_error].house_rooms[0]);
		send_to_char(buf, ch);
		return;
	    }
	    temp_house.house_rooms[j++] = virt_atrium + i;
	    number_of_rooms++; 
	}
    }
    
    temp_house.title[0] = 0;
    temp_house.num_of_rooms = number_of_rooms;
    temp_house.built_on = time(0);
    temp_house.mode = HOUSE_PRIVATE;
    temp_house.owner1 = owner;
    temp_house.owner2 = -1;
    temp_house.num_of_guests = 0;
    temp_house.base_cost = BASE_HSRM_COST*number_of_rooms;
    temp_house.last_payment = 0;
    temp_house.rent_sum = 0;
    temp_house.rent_rate = 0;
    temp_house.rent_time = 0;
    temp_house.landlord = GET_IDNUM( ch );

    house_control[num_of_houses++] = temp_house;

    for (i = 0; i < number_of_rooms; i++)
	SET_BIT(ROOM_FLAGS(real_room(temp_house.house_rooms[i])), ROOM_HOUSE );
    SET_BIT(ROOM_FLAGS(real_room(temp_house.house_rooms[0])), ROOM_ATRIUM);
    House_crashsave(virt_atrium);

    send_to_char("House built.  Mazel tov!\r\n", ch);
    House_save_control();
}

void 
hcontrol_destroy_house(struct char_data * ch, char *arg)
{
    int i, j;
    struct room_data *room_rnum;

    if (!*arg) {
	send_to_char(HCONTROL_FORMAT, ch);
	return;
    }
    if (((i = find_house(atoi(arg))) < 0) || 
	(house_control[i].house_rooms[0] != atoi(arg)))  {
	send_to_char("Unknown house.\r\n", ch);
	return;
    }

    if ( ! can_edit_house( ch, house_control + i ) ) {
	send_to_char( "You cannot edit that house.\r\n", ch );
	return;
    }

    for (j=0;j<house_control[i].num_of_rooms;j++)  {
	if ((room_rnum = real_room(house_control[i].house_rooms[j])) == NULL) {
	    sprintf(buf,"SYSERR: House had invalid vnum: %d",
		    house_control[i].house_rooms[j]);
	    slog(buf);
	}
	else {
	    REMOVE_BIT(ROOM_FLAGS(room_rnum), ROOM_HOUSE | ROOM_HOUSE_CRASH);
	    if (i == 0)
		REMOVE_BIT(ROOM_FLAGS(room_rnum), ROOM_ATRIUM);
	}
    }

    House_delete_file(house_control[i].house_rooms[0]);

    for (j = i; j < num_of_houses - 1; j++)
	house_control[j] = house_control[j + 1];

    num_of_houses--;

    send_to_char("House deleted.\r\n", ch);
    House_save_control();
}


void 
hcontrol_pay_house(struct char_data * ch, char *arg)
{

}
void
hcontrol_add_to_house(struct char_data *ch, char *arg)
{
    char arg1[MAX_INPUT_LENGTH];
    room_num room_vnum, virt_atrium;
    struct room_data *room_rnum;
    int find_error, j, id;
    struct house_control_rec *h = 0;
    
    // first arg is the atrium vnum
    arg = one_argument(arg, arg1);
    if ( !*arg1 || !*arg ) {
	send_to_char(HCONTROL_ADD_FORMAT, ch);
	return;
    }

    virt_atrium = atoi(arg1);

    if ( ! ( h = real_house( virt_atrium ) ) ) {
	send_to_char("No such house exists.\r\n", ch);
	return;
    }
    
    if ( ! can_edit_house( ch, h ) ) {
	send_to_char( "You cannot edit that house.\r\n", ch );
	return;
    }
    
    // turn arg into the final argument and arg1 into the room/guest/owner arg
    arg = one_argument( arg, arg1 );
    skip_spaces( &arg );

    if ( !*arg1 || !*arg ) {
	send_to_char( HCONTROL_ADD_FORMAT, ch );
	return;
    }

    if ( is_abbrev( arg1, "room" ) ) {

	room_vnum = atoi( arg );
	if ( ( room_rnum = real_room( room_vnum ) ) == NULL )
	    send_to_char( "No such room exists.\r\n", ch );
	else if ( ( find_error = find_house( room_rnum->number ) ) >= 0 ) {
	    sprintf( buf, "Room [%d] already exists as room of house [%d]\r\n",
		     room_vnum, house_control[ find_error ].house_rooms[ 0 ] );
	    send_to_char( buf, ch );
	}
	else if ( h->num_of_rooms >= MAX_ROOMS_PER_HOUSE ) {
	    send_to_char( "House has max rooms.\r\n", ch );
	} else {
	    j = ++( h->num_of_rooms );
	    h->house_rooms[ j-1 ] = room_vnum;
	    h->base_cost += BASE_HSRM_COST;
	    SET_BIT( ROOM_FLAGS( room_rnum ), ROOM_HOUSE);
	    House_crashsave( virt_atrium );
	    send_to_char( "Room added.\r\n", ch );
	    House_save_control( );
	}
	return;
    }

    else if ( is_abbrev( arg1, "guest" ) ) {

	if ( ( id = get_id_by_name( arg ) ) < 0 ) {
	    send_to_char( "No such player.\r\n", ch );
	    return;
	}
	if ( ( id == h->owner1 ) || ( id == h->owner2 ) ) {
	    send_to_char( "That is one of the owners you fool!\r\n", ch );
	    return;
	}
	for ( j = 0; j < h->num_of_guests; j++ ) {
	    if ( h->guests[ j ] == id ) {
		send_to_char( "They are already guests you fool!\r\n", ch );
		return;
	    }
	}
	if ( h->num_of_guests >= MAX_GUESTS ) {
	    send_to_char( "Max guests already reached.\r\n", ch );
	    return;
	}
	
	j = h->num_of_guests++;
	h->guests[ j ] = id;

	House_save_control();

	send_to_char("Guest added.\r\n", ch);
	return;
    }

    else if (is_abbrev(arg1, "owner")) {

	if ( h->owner2 >= 0 ) {
	    send_to_char( "House already has two owners.\r\n", ch );
	    return;
	}
	if ( ( id = get_id_by_name( arg ) ) < 0 ) {
	    send_to_char( "No such player.\r\n", ch );
	    return;
	}

	if ( get_name_by_id( h->owner1 ) )
	    h->owner2 = id;
	else
	    h->owner1 = id;

	House_save_control( );

	send_to_char( "Owner added.\r\n", ch );
	return;
    }
    send_to_char( HCONTROL_ADD_FORMAT, ch );
}
void 
hcontrol_delete_from_house(struct char_data *ch, char *arg)
{
    char arg1[MAX_INPUT_LENGTH];
    room_num room_vnum, virt_atrium;
    struct room_data *room_rnum;
    int find_error, j, id;
    struct house_control_rec *h = 0;
    
    // first arg is the atrium vnum
    arg = one_argument(arg, arg1);
    if ( !*arg1 || !*arg ) {
	send_to_char(HCONTROL_DELETE_FORMAT, ch);
	return;
    }

    virt_atrium = atoi(arg1);

    if ( ! ( h = real_house( virt_atrium ) ) ) {
	send_to_char("No such house exists.\r\n", ch);
	return;
    }
    
    if ( ! can_edit_house( ch, h ) ) {
	send_to_char( "You cannot edit that house.\r\n", ch );
	return;
    }
    
    // turn arg into the final argument and arg1 into the room/guest/owner arg
    arg = one_argument( arg, arg1 );
    skip_spaces( &arg );

    if ( !*arg1 || !*arg ) {
	send_to_char( HCONTROL_DELETE_FORMAT, ch );
	return;
    }

    // delete room
    if ( is_abbrev( arg1, "room" ) ) {

	room_vnum = atoi( arg );
	if ( ( room_rnum = real_room( room_vnum ) ) == NULL )
	    send_to_char( "No such room exists.\r\n", ch );
	else if ( ( find_error = find_house( room_rnum->number ) ) < 0 ) {
	    send_to_char("This room isn't in ANY house!\r\n", ch);
	} else if ( h != real_house( room_rnum->number ) ) {
	    sprintf(buf, "This room belongs to house [%d]!\r\n",
		    house_control[ find_error ].house_rooms[ 0 ]);
	    send_to_char(buf, ch);
	} else {
	    for ( j = 0; j < h->num_of_rooms; j++ )
		if ( h->house_rooms[ j ] == room_vnum )
		    break;
	    
	    if ( j == 0 ) {
		send_to_char( "You cannot delete the atrium room.\r\n", ch );
		return;
	    }

	    for ( j++; j < h->num_of_rooms; j++ )
		h->house_rooms[ j - 1 ] = h->house_rooms[ j ];
	    
	    h->num_of_rooms--;
	    REMOVE_BIT( ROOM_FLAGS( room_rnum ), ROOM_HOUSE | ROOM_HOUSE_CRASH );
	    if ( virt_atrium == room_vnum ) {
		REMOVE_BIT( ROOM_FLAGS( room_rnum ), ROOM_ATRIUM );
		SET_BIT( ROOM_FLAGS( real_room(h->house_rooms[0] ) ), ROOM_ATRIUM );
		virt_atrium = room_rnum->number;
	    }
	    House_crashsave( virt_atrium );
	    House_save_control();
	    
	    send_to_char("Room deleted.\r\n", ch);
	    
	}
	return;
    }
    else if ( is_abbrev( arg1, "guest" ) ) {
	if ( ( id = get_id_by_name(arg ) ) < 0 ) {
	    send_to_char( "No such player.\r\n", ch );
	    return;
	}
	if ( ( id == h->owner1 ) || ( id == h->owner2 ) ) {
	    send_to_char( "That is one of the owners you fool!\r\n", ch );
	    return;
	}
	for ( j = 0; j < h->num_of_guests; j++) {
	    if ( h->guests[j] == id ) {
		for ( ; j < h->num_of_guests; j++ )
		    h->guests[j] = h->guests[j + 1];
		h->num_of_guests--;
		send_to_char("Guest deleted.\r\n", ch);
		return;
	    }
	}

	send_to_char("That guest was not found in this house.\r\n", ch);
	return;
    }
    else if (is_abbrev(arg1, "owner")) {
	if ((id = get_id_by_name(arg)) < 0) {
	    send_to_char("No such player.\r\n", ch);
	    return;
	}

	if (h->owner1 == id) {
	    h->owner1 =
		h->owner2;
	    h->owner2 = -1;
	} else if (h->owner2 == id) {
	    h->owner2 = -1;
	} else {
	    send_to_char("That player is not an owner of this house!\r\n", ch);
	    return;
	}

	send_to_char("Owner deleted.\r\n", ch);
	House_save_control();
	return;
    }
    send_to_char(HCONTROL_FORMAT, ch);
}

void 
hcontrol_set_house(struct char_data *ch, char *arg)
{
    char arg1[256], arg2[256];
    room_num atrium_vnum;
    int pos;
  
    arg = two_arguments( arg, arg1, arg2 );
    skip_spaces(&arg);

    if ( !*arg1 )
	send_to_char( HCONTROL_FORMAT, ch );
    else if ( !isdigit( *arg1 ) )
	send_to_char("The atrium vnum must be a number.\r\n", ch);
    else if ( !*arg2 )
	send_to_char( HCONTROL_FORMAT, ch );
    else {
	atrium_vnum = atoi( arg1 );
	if ( ( pos = find_house( atrium_vnum ) ) < 0 ) {
	    send_to_char( "No such house you IDIOT!!\r\n", ch );
	    return;
	}
	if ( ! can_edit_house( ch, house_control + pos ) ) {
	    send_to_char( "You cannot edit that house.\r\n", ch );
	    return;
	}
	if ( is_abbrev(arg2, "rate" ) ) {
	    if ( !*arg )
		send_to_char( "Set rental rate to what?\r\n", ch );
	    else {
		one_argument(arg, arg1);
		house_control[pos].rent_rate = atoi(arg1);
		sprintf(buf, "House <%d> rental rate set to %d/day.\r\n", atrium_vnum,
			house_control[pos].rent_rate);
		send_to_char(buf, ch);
	    }
	} else if (is_abbrev(arg2, "current")) {
	    if (!*arg)
		send_to_char("Set current rent sum to what?\r\n", ch);
	    else {
		one_argument(arg, arg1);
		house_control[pos].rent_sum = atoi(arg1);
		sprintf(buf, "House <%d> current set to %d.\r\n", atrium_vnum,
			house_control[pos].rent_sum);
		send_to_char(buf, ch);
	    }
	} else if (is_abbrev(arg2, "mode")) {
	    if (!*arg) {
		send_to_char("Set mode to PUB, PRIV, or RENT?\r\n", ch);
		return;
	    } else if (is_abbrev(arg, "public"))
		house_control[pos].mode = HOUSE_PUBLIC;
	    else if (is_abbrev(arg, "private"))
		house_control[pos].mode = HOUSE_PRIVATE;
	    else if (is_abbrev(arg, "rental"))
		house_control[pos].mode = HOUSE_RENTAL;
	    else {
		send_to_char("You must specify PUBLIC, PRIVATE, or RENTAL!!\r\n", ch);
		return;
	    }  
	    send_to_char("Mode set successful.\r\n", ch);
	    
	}
	else if ( is_abbrev( arg2, "landlord" ) ) {
	    int idnum;

	    if ( ! *arg ) {
		send_to_char( "Set who as the landlord?\r\n", ch );
		return;
	    }
	    
	    if ( ( idnum = get_id_by_name( arg ) ) <= 0 ) {
		sprintf( buf, "There is no such player, '%s'\r\n", arg );
		send_to_char( buf, ch );
		return;
	    }

	    house_control[ pos ].landlord = idnum;

	    send_to_char( "Landlord set.\r\n", ch );
	    return;
	}
	else if ( is_abbrev( arg2, "owner" ) ) {
	    int idnum;

	    if ( ! *arg ) {
		send_to_char( "Set who as the owner?\r\n", ch );
		return;
	    }
	    
	    if ( ( idnum = get_id_by_name( arg ) ) <= 0 ) {
		sprintf( buf, "There is no such player, '%s'\r\n", arg );
		send_to_char( buf, ch );
		return;
	    }

	    house_control[ pos ].owner1 = idnum;

	    send_to_char( "Owner set.\r\n", ch );
	    return;
	}
	    
	House_crashsave(atrium_vnum);
	House_save_control();
    }  
}

void 
hcontrol_where_house(struct char_data *ch, char *arg)
{
    struct house_control_rec *h;
    char *temp = 0;

    h = real_house( ch->in_room->number );
    
    if ( ! h ) {
	send_to_char( "You are not in a house.\r\n", ch );
	return;
    }
    
    sprintf( buf,
	     "You are in house atrium vnum: %s[%s%6d%s]%s\n"
	     "                       Owner: %s\n",
	     CCGRN( ch, C_NRM ), CCNRM( ch, C_NRM ),
	     h->house_rooms[ 0 ],
	     CCGRN( ch, C_NRM ), CCNRM( ch, C_NRM ),
	     NAME( h->owner1 ) );
    send_to_char( buf, ch );
    
}

/* The hcontrol command itself, used by imms to create/destroy houses */
ACMD(do_hcontrol)
{
    char arg1[MAX_INPUT_LENGTH], arg2[MAX_INPUT_LENGTH];
    room_num atrium_vnum;

    half_chop(argument, arg1, arg2);

    if ( is_abbrev(arg1, "save") && GET_LEVEL(ch) >= LVL_GOD ) {
	House_save_control();
	House_save_all(0);
	send_to_char("Saved.\r\n", ch);
    }
    else if ( is_abbrev(arg1, "recount") && GET_LEVEL(ch) >= LVL_GOD ) {
	House_countobjs();
	send_to_char("Objs recounted.\r\n", ch);
    }
    else if ( is_abbrev(arg1, "build") && GET_LEVEL(ch) >= LVL_GOD )
	hcontrol_build_house(ch, arg2);
    else if ( is_abbrev(arg1, "destroy") && GET_LEVEL(ch) >= LVL_GOD )
	hcontrol_destroy_house(ch, arg2);
    else if ( is_abbrev(arg1, "pay") && GET_LEVEL(ch) >= LVL_GOD )
	hcontrol_pay_house(ch, arg2);
    else if ( is_abbrev(arg1, "add") && GET_LEVEL(ch) >= LVL_GOD )
	hcontrol_add_to_house(ch, arg2); 
    else if ( is_abbrev(arg1, "delete") && GET_LEVEL(ch) >= LVL_GOD )
	hcontrol_delete_from_house(ch, arg2); 
    else if ( is_abbrev(arg1, "set") && GET_LEVEL(ch) >= LVL_GOD )
	hcontrol_set_house(ch, arg2);
    else if ( is_abbrev( arg1, "where" ) && GET_LEVEL( ch ) >= LVL_GOD )
	hcontrol_where_house( ch, arg2 );
    else if ( is_abbrev(arg1, "show") && GET_LEVEL(ch) >= LVL_DEMI) {  
	if ( !*arg2 || *arg2 == '+' || *arg2 == '-' || ! strncasecmp( arg2, "all", 3 ) )
	    hcontrol_list_houses(ch, arg2);
	else {
	    argument = one_argument(arg2, arg1);
	    if (is_abbrev(arg1, "rooms")) {
		argument = one_argument(argument, arg1);
		if (!*arg1) {
		    send_to_char(HCONTROL_SHOW_FORMAT, ch);
		    return;
		}
		atrium_vnum = atoi(arg1);
		hcontrol_list_house_rooms(ch, atrium_vnum);
	    } else if (is_abbrev(arg1, "guests")) {
		argument = one_argument(argument, arg1);
		if (!*arg1) {
		    send_to_char(HCONTROL_SHOW_FORMAT, ch);
		    return;
		}
		atrium_vnum = atoi(arg1);
		hcontrol_list_house_guests(ch, atrium_vnum);
	    } else
		send_to_char(HCONTROL_SHOW_FORMAT, ch);
	}  
    }
    else
	send_to_char(HCONTROL_FORMAT, ch);
}

/* The house command, used by mortal house owners to assign guests */
ACMD(do_house)
{
    int i, j, id, k, found = FALSE;
    char *temp;

    one_argument(argument, arg);

    if (!IS_SET(ROOM_FLAGS(ch->in_room), ROOM_HOUSE))
	send_to_char("You must be in your house to set guests.\r\n", ch);
    else if ((i = find_house(ch->in_room->number)) < 0)
	send_to_char("Um.. this house seems to be screwed up.\r\n", ch);
    else if ( ( GET_IDNUM(ch) != house_control[i].owner1 ) && 
	      ( GET_IDNUM(ch) != house_control[i].owner2 ) &&
	      GET_LEVEL(ch) < LVL_CREATOR ) 
	send_to_char("Only the owner can set guests.\r\n", ch);
    else if (!*arg) {
	send_to_char("Guests of your house:\r\n", ch);
	if ( house_control[i].num_of_guests == 0 )
	    send_to_char("  None.\r\n", ch);
	else
	    for (j = 0; j < house_control[i].num_of_guests; j++) {
		strcpy(buf, NAME(house_control[i].guests[j]));
		send_to_char(strcat(CAP(buf), "\r\n"), ch);
	    }
    } else if ( ( id = get_id_by_name( arg ) ) < 0 )
	send_to_char("No such player.\r\n", ch);
    else {
	for (j = 0; j < house_control[i].num_of_guests; j++) {
	    if (house_control[i].guests[j] == id) {
		for (; j < house_control[i].num_of_guests; j++)
		    house_control[i].guests[j] = house_control[i].guests[j + 1];
		house_control[i].num_of_guests--;
		send_to_char("Guest deleted.\r\n", ch);
		found = TRUE;
		break;
	    }
	}
	if (!found) {
	    j = house_control[i].num_of_guests++;

	    if (j >= MAX_GUESTS) {
		send_to_char("Sorry, you have the maximum number of guests already.\r\n", ch);
		house_control[i].num_of_guests--;
		return;
	    } else {
		house_control[i].guests[j] = id;
		send_to_char("Guest added.\r\n", ch);
	    }
	}

	found = FALSE;
	for (j = 0; j < house_control[i].num_of_guests; j++) {
	    if (!get_name_by_id(house_control[i].guests[j])) {
		for (k = j; k < house_control[i].num_of_guests; k++)
		    house_control[i].guests[k] = house_control[i].guests[k + 1];
		house_control[i].num_of_guests--;
		found++;
	    }
	}

	if (found) {
	    sprintf(buf,
		    "%d bogus guest%s deleted.\r\n", found, found == 1 ? "" : "s");
	    send_to_char(buf, ch);
	}
	House_save_control();
    }
}

/* Misc. administrative functions */

/* crash-save all the houses */
void 
House_save_all(bool mode)
{
    int i, j;
    struct room_data *atrium, *room;

    for (i = 0; i < num_of_houses; i++) {
	if (!(atrium = real_room(house_control[i].house_rooms[0])))
	    continue;

	if (mode && house_control[i].rent_time) {
	    house_control[i].hourly_rent_sum /= (24 * house_control[i].rent_time);
	    house_control[i].rent_sum += house_control[i].hourly_rent_sum;
	    house_control[i].hourly_rent_sum = 0;
	    house_control[i].rent_time =       0;
	    House_crashsave(house_control[i].house_rooms[0]);
	    return;
	} 

	for (j = 0; j < house_control[i].num_of_rooms; j++) {
	    if ((room = real_room(house_control[i].house_rooms[j])) &&
		ROOM_FLAGGED(room, ROOM_HOUSE_CRASH)) {
		SET_BIT(ROOM_FLAGS(atrium),ROOM_HOUSE_CRASH);
		break;
	    }
	}
	if (IS_SET(ROOM_FLAGS(atrium), ROOM_HOUSE_CRASH))
	    House_crashsave(house_control[i].house_rooms[0]);
    }
}


/* note: arg passed must be house vnum, so there. */
int 
House_can_enter(struct char_data * ch, room_num room_vnum)
{
    int i, j;

    if (GET_LEVEL(ch) >= LVL_GOD || (i = find_house(room_vnum)) < 0)
	return 1;

    if (IS_NPC(ch)) {
	// so charmies can walk around in the master's house
	if (AFF_FLAGGED(ch, AFF_CHARM) && ch->master)
	    return (House_can_enter(ch->master, room_vnum));
	return 0;
    }

    switch (house_control[i].mode) {
    case HOUSE_PUBLIC:
	return 1;
    case HOUSE_PRIVATE:
    case HOUSE_RENTAL:
	if ((GET_IDNUM(ch) == house_control[i].owner1) ||
	    (GET_IDNUM(ch) == house_control[i].owner2))
	    return 1;
	for (j = 0; j < house_control[i].num_of_guests; j++)
	    if (GET_IDNUM(ch) == house_control[i].guests[j])
		return 1;
	return 0;
	break;
    }

    return 0;
}
