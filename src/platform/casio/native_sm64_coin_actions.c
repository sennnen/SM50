/* Compile the canonical coin callbacks as their own SH4 translation unit.
 * Pulling all of behavior_actions.c also imports its broad audio/cutscene
 * graph, which this calculator backend intentionally does not provide. */
#include <PR/ultratypes.h>

#include "sm64.h"
#include "behavior_data.h"
#include "engine/behavior_script.h"
#include "game/area.h"
#include "game/behavior_actions.h"
#include "game/interaction.h"
#include "game/mario.h"
#include "game/object_helpers.h"
#include "game/object_list_processor.h"
#include "game/spawn_object.h"
#include "game/spawn_sound.h"
#include "engine/math_util.h"
#include "engine/surface_collision.h"

#define o gCurrentObject
#include "game/behaviors/coin.inc.c"
