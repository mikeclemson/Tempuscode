//
// File: newbie_fly.spec                     -- Part of TempusMUD
//
// Copyright 1998 by John Watson, all rights reserved.
//

SPECIAL(newbie_fly)
{
  if( spec_mode == SPECIAL_DEATH ) return 0;
  if (cmd || FIGHTING(ch))
    return 0;
  CharacterList::iterator it = ch->in_room->people.begin();
  for( ; it != ch->in_room->people.end(); ++it ) {
    if (IS_AFFECTED((*it), AFF_INFLIGHT) || !CAN_SEE(ch, (*it)))
      continue;
    cast_spell(ch, (*it), 0, SPELL_FLY);
    return 1;
  }
  return 0;
}
