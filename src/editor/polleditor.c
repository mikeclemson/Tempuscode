//
// File: polleditor.cc                        -- part of TempusMUD
//

#ifdef HAS_CONFIG_H
#include "config.h"
#endif

#include <ctype.h>
#include <fcntl.h>
// Tempus Includes
#include "screen.h"
#include "desc_data.h"
#include "comm.h"
#include "db.h"
#include "utils.h"
#include "login.h"
#include "interpreter.h"
#include "boards.h"
#include "mail.h"
#include "editor.h"
#include "tmpstr.h"
#include "accstr.h"
#include "help.h"
#include "comm.h"

void voting_add_poll(const char *header, const char *text);

struct polleditor_data {
    char *header;
};

void
free_polleditor(struct editor *editor)
{
    struct polleditor_data *poll_data =
        (struct polleditor_data *)editor->mode_data;
    free(poll_data->header);
    free(poll_data);
}

void
polleditor_finalize(struct editor *editor, const char *text)
{
    struct polleditor_data *poll_data =
        (struct polleditor_data *)editor->mode_data;
    voting_add_poll(poll_data->header, text);

    if (IS_PLAYING(editor->desc))
        act("$n nods with satisfaction as $e saves $s work.",
            true, editor->desc->creature, 0, 0, TO_NOTVICT);

    free_polleditor(editor);
}

void
polleditor_cancel(struct editor *editor)
{
    free_polleditor(editor);
}

void
start_editing_poll(struct descriptor_data *d, const char *header)
{
    struct polleditor_data *poll_data;
    if (d->text_editor) {
        errlog("Text editor object not null in start_editing_poll");
        REMOVE_BIT(PLR_FLAGS(d->creature),
            PLR_WRITING | PLR_OLC | PLR_MAILING);
        return;
    }

    SET_BIT(PLR_FLAGS(d->creature), PLR_WRITING);

    d->text_editor = make_editor(d, MAX_MAIL_SIZE);
    CREATE(poll_data, struct polleditor_data, 1);
    d->text_editor->finalize = polleditor_finalize;
    d->text_editor->cancel = polleditor_cancel;
    d->text_editor->mode_data = poll_data;

    poll_data->header = strdup(header);

    emit_editor_startup(d->text_editor);
    editor_display(d->text_editor, 0, 0);
}
