#ifndef _PROG_H_
#define _PROG_H_

#include "constants.h"

class Creature;

enum prog_evt_type {
	PROG_TYPE_NONE,
	PROG_TYPE_MOBILE,
	PROG_TYPE_OBJECT,
	PROG_TYPE_ROOM
};

enum prog_evt_phase {
	PROG_EVT_BEGIN,
	PROG_EVT_HANDLE,
	PROG_EVT_AFTER
};

enum prog_evt_kind {
	PROG_EVT_COMMAND,
	PROG_EVT_IDLE,
	PROG_EVT_FIGHT,
	PROG_EVT_GIVE,
	PROG_EVT_ENTER,
	PROG_EVT_LEAVE,
	PROG_EVT_LOAD,
	PROG_EVT_TICK,
    PROG_EVT_SPELL,
    PROG_EVT_COMBAT,
    PROG_EVT_DEATH
};

enum prog_cmd_kind {
    PROG_CMD_HALT,
    PROG_CMD_RESUME,
    PROG_CMD_BEFORE,
    PROG_CMD_HANDLE,
    PROG_CMD_AFTER,
    PROG_CMD_OR
};

struct prog_evt {
	prog_evt_phase phase;
	prog_evt_kind kind;
	int cmd;
	char *args;

	Creature *subject;
	void *object;
	int object_type;
};

struct prog_var {
	struct prog_var *next;
	char *key;
	char *value;
};

struct prog_state_data {
	struct prog_var *var_list;
};

struct prog_env {
	struct prog_env *next;	// next prog environment
	int exec_pt;				// the line number we're executing
	int executed;				// the number of non-handlers we've executed
    int speed;					// default wait between commands
    int wait;					// the number of seconds to wait
    prog_evt_type owner_type;				// type of the owner
	void *owner;				// pointer to the actual owner
	Creature *target;			// target of prog
	prog_evt evt;				// copy of event that caused prog to trigger
	prog_state_data *state;		// state record of owner
};

void destroy_attached_progs(void *self);
bool trigger_prog_cmd(void *owner, prog_evt_type owner_type, Creature *ch, int cmd, char *argument);
bool trigger_prog_spell(void *owner, prog_evt_type owner_type, Creature *ch, int cmd);
bool trigger_prog_move(void *owner, prog_evt_type owner_type, Creature *ch, special_mode mode);
void trigger_progs_after(Creature *ch, int cmd, char *argument);
void trigger_prog_idle(void *owner, prog_evt_type owner_type);
void trigger_prog_combat(void *owner, prog_evt_type owner_type);
void trigger_prog_tick(void *owner, prog_evt_type owner_type);
void trigger_prog_load(Creature *self);
void trigger_prog_fight(Creature *ch, Creature *self);
void trigger_prog_give(Creature *ch, Creature *self, struct obj_data *obj);
void trigger_prog_death(void *owner, prog_evt_type owner_type, Creature *ch);
prog_env *prog_start(prog_evt_type owner_type, void *owner, Creature *target, prog_evt *evt);
void prog_free(struct prog_env *prog);
void prog_update(void);
void prog_update_pending(void);
int prog_count(void);
void prog_state_free(prog_state_data *state);
char *prog_get_alias_list(char *args);
void prog_compile(Creature *ch, void *owner, prog_evt_type owner_type);
void prog_display_obj(Creature *ch, Creature *target);
char *prog_get_text(void *owner, prog_evt_type owner_type);

#endif
