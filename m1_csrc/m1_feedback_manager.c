/* See COPYING.txt for license details. */

/*
*
* m1_feedback_manager.c
*
* Core, hardware-agnostic feedback manager engine. See
* m1_feedback_manager.h for the public API and task/ISR contract, and
* documentation/M1_FEEDBACK_RECONCILIATION.md for the identifier-by-
* identifier mapping to the M1-FB-STD-001 v1.1 reference document.
*
* -----------------------------------------------------------------------
* Concurrency model
* -----------------------------------------------------------------------
* This file never calls the hardware adapter while holding the internal
* critical section. All bookkeeping (the fixed-size instance pool and the
* per-resource hold/transient/suspended-loop state) is mutated only while
* FB_ENTER_CRITICAL()/FB_EXIT_CRITICAL() is held; those sections do a
* bounded amount of array work and never touch hardware, so they are safe
* to call from any task and never block. fb_manager_tick() computes, under
* one such critical section, whether each resource's visible value changed
* ("pending"/"dirty"), then releases the section and only THEN calls the
* hardware adapter -- so an I2C write (which can take up to the driver's
* own timeout) never happens with interrupts/scheduling disabled and never
* blocks a caller of fb_request()/fb_release(). fb_cleanup_owner() and
* fb_reset_all() are the one documented exception: because they are the
* fault/cancel/shutdown safety path, they apply their result synchronously
* (see their comments) rather than waiting for the next tick.
*
* On target, FB_ENTER_CRITICAL/FB_EXIT_CRITICAL map to FreeRTOS
* taskENTER_CRITICAL/taskEXIT_CRITICAL, which nest safely within one task,
* so internal helper functions that already assume the lock is held may
* call fb_release() itself without deadlocking.
*
* M1 Project
*
*/

#include <string.h>
#include "m1_feedback_manager.h"
#include "m1_feedback_sequences.h"

#ifdef M1_FEEDBACK_HOST_TEST
#define FB_ENTER_CRITICAL()	((void)0)
#define FB_EXIT_CRITICAL()	((void)0)
#else
#include "FreeRTOS.h"
#include "task.h"
#define FB_ENTER_CRITICAL()	taskENTER_CRITICAL()
#define FB_EXIT_CRITICAL()	taskEXIT_CRITICAL()
#endif

/*************************** C O N F I G ****************************************/

#define FB_MAX_ACTIVE		8	/* concurrent running sequence instances     */
#define FB_MAX_HOLDS		4	/* concurrent persistent holds per resource  */
#define FB_LOOP_DEPTH		2	/* nested MSG_LOOP_BEGIN depth per sequence  */
#define FB_STEP_GUARD		64	/* max messages executed per instance/tick   */

/* Sentinel stored in a BL value meaning "track the live user brightness
 * setting" rather than a fixed percent (reference section 5.3 / 4.2). */
#define FB_VALUE_USER_BRIGHTNESS	0xFFFFu

/*************************** S T A T E *******************************************/

typedef struct
{
	uint16_t pc;
	uint32_t remaining;	/* bounded-loop iterations left; ignored if infinite */
	bool     infinite;
} fb_loop_frame_t;

typedef struct
{
	bool             in_use;
	const fb_msg_t  *prog;
	uint16_t         pc;
	uint32_t         delay_ms;
	fb_owner_t       owner;
	fb_priority_t    priority;
	fb_kind_t        kind;
	fb_resource_t    resource;
	fb_seq_id_t      tag;
	fb_loop_frame_t  loop_stack[FB_LOOP_DEPTH];
	uint8_t          loop_sp;
	/* True for exactly the first step_instance() call after this instance
	 * was allocated by recompute_baseline()'s suspended-loop resume path
	 * (the only place an instance can be allocated *during* the tick's own
	 * stepping sweep, from inside step_instance() itself via
	 * FB_MSG_LED_RESTORE/FB_MSG_BL_RESTORE or finish_instance()). Without
	 * this guard, a resumed instance could be stepped a second time in the
	 * very same fb_manager_tick() sweep -- at whatever index alloc_instance()
	 * happened to reuse -- and would consume that tick's elapsed_ms twice,
	 * silently shortening its first timed step. Skipping exactly one
	 * step_instance() call defers its real first step to the next tick,
	 * matching the elapsed_ms accounting of every other freshly-requested
	 * sequence. */
	bool             just_resumed;
} fb_instance_t;

typedef struct
{
	bool         dirty;
	uint16_t     value;	/* LED mask / BL percent-or-sentinel / SND note id */
	bool         is_off;	/* SND resource only: true => snd_off()            */
} fb_pending_t;

typedef struct
{
	bool          used[FB_MAX_HOLDS];
	fb_owner_t    owner[FB_MAX_HOLDS];
	fb_priority_t priority[FB_MAX_HOLDS];
	uint16_t      value[FB_MAX_HOLDS];

	int8_t        active;		/* index into g_inst, or -1                  */
	uint16_t      last_applied;	/* logical current value (unresolved sentinel ok) */

	bool          susp_valid;	/* LED only in practice: one suspended loop  */
	fb_owner_t    susp_owner;
	fb_priority_t susp_priority;
	const fb_msg_t *susp_prog;
	fb_seq_id_t   susp_tag;

	fb_pending_t  pending;
} fb_res_state_t;

static const fb_hw_adapter_t *g_hw;
static fb_instance_t   g_inst[FB_MAX_ACTIVE];
static fb_res_state_t  g_res[FB_RES_COUNT];
static bool            g_sound_enabled = true;
static uint8_t         g_volume_pct = 100;
static uint8_t         g_brightness_pct = 100;

/*************************** F O R W A R D   D E C L S **************************/

static void recompute_baseline(fb_resource_t r);
static void fb_release_locked(fb_owner_t owner, fb_resource_t r);
static void finish_instance(int8_t idx);
static void preempt_active_transient(fb_resource_t r);

/*************************** S M A L L   H E L P E R S ***************************/

static uint8_t resolve_bl(uint16_t value)
{
	if ( value == FB_VALUE_USER_BRIGHTNESS )
		return g_brightness_pct;
	return (uint8_t)(value > 100 ? 100 : value);
} // static uint8_t resolve_bl(uint16_t value)



static int8_t alloc_instance(void)
{
	uint8_t i;

	for (i = 0; i < FB_MAX_ACTIVE; i++)
	{
		if ( !g_inst[i].in_use )
			return (int8_t)i;
	} // for (i = 0; i < FB_MAX_ACTIVE; i++)
	return -1;
} // static int8_t alloc_instance(void)



/* Highest-priority (numerically lowest) owner currently controlling `r`,
 * considering both the active transient (if any) and the hold list. */
static fb_priority_t resource_blocking_priority(fb_resource_t r, fb_owner_t *owner_out)
{
	fb_priority_t best = FB_PRIO_NONE;
	fb_owner_t bo = FB_OWNER_NONE;
	uint8_t i;

	if ( g_res[r].active >= 0 )
	{
		best = g_inst[g_res[r].active].priority;
		bo = g_inst[g_res[r].active].owner;
	} // if ( g_res[r].active >= 0 )

	for (i = 0; i < FB_MAX_HOLDS; i++)
	{
		if ( !g_res[r].used[i] )
			continue;
		if ( g_res[r].priority[i] < best )
		{
			best = g_res[r].priority[i];
			bo = g_res[r].owner[i];
		} // if ( g_res[r].priority[i] < best )
	} // for (i = 0; i < FB_MAX_HOLDS; i++)

	if ( owner_out != NULL )
		*owner_out = bo;
	return best;
} // static fb_priority_t resource_blocking_priority(...)



static int8_t find_hold_slot_for_owner(fb_resource_t r, fb_owner_t owner)
{
	uint8_t i;
	for (i = 0; i < FB_MAX_HOLDS; i++)
	{
		if ( g_res[r].used[i] && g_res[r].owner[i] == owner )
			return (int8_t)i;
	} // for (i = 0; i < FB_MAX_HOLDS; i++)
	return -1;
} // static int8_t find_hold_slot_for_owner(...)



static int8_t find_free_hold_slot(fb_resource_t r)
{
	uint8_t i;
	for (i = 0; i < FB_MAX_HOLDS; i++)
	{
		if ( !g_res[r].used[i] )
			return (int8_t)i;
	} // for (i = 0; i < FB_MAX_HOLDS; i++)
	return -1;
} // static int8_t find_free_hold_slot(...)



static int8_t find_best_hold_slot(fb_resource_t r)
{
	int8_t best_slot = -1;
	fb_priority_t best = FB_PRIO_NONE;
	uint8_t i;

	for (i = 0; i < FB_MAX_HOLDS; i++)
	{
		if ( !g_res[r].used[i] )
			continue;
		if ( g_res[r].priority[i] < best )
		{
			best = g_res[r].priority[i];
			best_slot = (int8_t)i;
		} // if ( g_res[r].priority[i] < best )
	} // for (i = 0; i < FB_MAX_HOLDS; i++)
	return best_slot;
} // static int8_t find_best_hold_slot(...)



/* Extracts the literal (or FB_VALUE_USER_BRIGHTNESS-sentinel) value a HOLD
 * sequence sets, by scanning its short message array. Reference hold
 * sequences are always of the trivial form "SET(x), END" or
 * "HOLD_USER, END", so a small bounded scan is sufficient and safe even if
 * malformed input omits MSG_END (see FB_STEP_GUARD-sized bound). */
static uint16_t extract_hold_value(const fb_msg_t *prog, fb_resource_t r)
{
	uint8_t i;

	for (i = 0; i < FB_STEP_GUARD; i++)
	{
		fb_msg_t m = prog[i];
		if ( m.op == FB_MSG_END )
			break;
		if ( r == FB_RES_LED && m.op == FB_MSG_LED_SET )
			return m.arg;
		if ( r == FB_RES_BL )
		{
			if ( m.op == FB_MSG_BL_SET )
				return m.arg;
			if ( m.op == FB_MSG_BL_USER || m.op == FB_MSG_BL_HOLD_USER )
				return FB_VALUE_USER_BRIGHTNESS;
			if ( m.op == FB_MSG_BL_OFF )
				return 0;
		} // if ( r == FB_RES_BL )
	} // for (i = 0; i < FB_STEP_GUARD; i++)
	return 0;
} // static uint16_t extract_hold_value(...)



static void apply_value(fb_resource_t r, uint16_t value)
{
	g_res[r].last_applied = value;
	g_res[r].pending.dirty = true;
	g_res[r].pending.value = value;
	g_res[r].pending.is_off = false;
} // static void apply_value(fb_resource_t r, uint16_t value)



static void apply_snd_off(void)
{
	g_res[FB_RES_SND].last_applied = FB_VALUE_USER_BRIGHTNESS; /* "off" marker, unused otherwise for SND */
	g_res[FB_RES_SND].pending.dirty = true;
	g_res[FB_RES_SND].pending.is_off = true;
} // static void apply_snd_off(void)



/*************************** R E C O M P U T E ************************************/

/* Logical restoration (reference section 6, rule 8): recompute the correct
 * visible owner for `r` from scratch rather than restoring a saved
 * register value. Must be called with the critical section held. */
static void recompute_baseline(fb_resource_t r)
{
	int8_t slot;

	if ( r == FB_RES_LED && g_res[r].active < 0 && g_res[r].susp_valid )
	{
		fb_owner_t bo;
		fb_priority_t best = resource_blocking_priority(r, &bo);

		if ( g_res[r].susp_priority < best )
		{
			int8_t idx = alloc_instance();
			if ( idx >= 0 )
			{
				fb_instance_t *in = &g_inst[idx];
				memset(in, 0, sizeof(*in));
				in->in_use = true;
				in->prog = g_res[r].susp_prog;
				in->pc = 0;
				in->delay_ms = 0;
				in->owner = g_res[r].susp_owner;
				in->priority = g_res[r].susp_priority;
				in->kind = FB_KIND_LOOP;
				in->resource = r;
				in->tag = g_res[r].susp_tag;
				in->just_resumed = true;
				g_res[r].active = idx;
				g_res[r].susp_valid = false;
				return; /* the resumed instance drives the visible value as it steps */
			} // if ( idx >= 0 )
		} // if ( g_res[r].susp_priority < best )
	} // if (LED, idle, suspended loop pending)

	if ( g_res[r].active >= 0 )
		return; /* an active transient still drives the visible value directly */

	slot = find_best_hold_slot(r);
	if ( slot >= 0 )
	{
		apply_value(r, g_res[r].value[slot]);
	}
	else
	{
		if ( r == FB_RES_SND )
			apply_snd_off();
		else
			apply_value(r, 0); /* LED off / backlight off -- idle baseline */
	} // else
} // static void recompute_baseline(fb_resource_t r)



static void preempt_active_transient(fb_resource_t r)
{
	int8_t idx = g_res[r].active;
	fb_instance_t *in;

	if ( idx < 0 )
		return;

	in = &g_inst[idx];
	if ( r == FB_RES_LED && in->kind == FB_KIND_LOOP )
	{
		g_res[r].susp_valid = true;
		g_res[r].susp_owner = in->owner;
		g_res[r].susp_priority = in->priority;
		g_res[r].susp_prog = in->prog;
		g_res[r].susp_tag = in->tag;
	} // if (LED background loop -- suspend, don't drop)

	in->in_use = false;
	g_res[r].active = -1;
} // static void preempt_active_transient(fb_resource_t r)



static void finish_instance(int8_t idx)
{
	fb_instance_t *in = &g_inst[idx];
	fb_resource_t r = in->resource;

	in->in_use = false;
	if ( g_res[r].active == idx )
		g_res[r].active = -1;
	recompute_baseline(r);
} // static void finish_instance(int8_t idx)



/*************************** S E Q U E N C E   S T E P P I N G *******************/

static void step_instance(uint8_t idx, uint32_t elapsed_ms)
{
	fb_instance_t *in = &g_inst[idx];
	uint32_t carry;
	uint16_t guard = FB_STEP_GUARD;

	if ( !in->in_use )
		return;

	if ( in->just_resumed )
	{
		/* Defer this instance's real first step to the next tick -- see the
		 * field comment on fb_instance_t::just_resumed. */
		in->just_resumed = false;
		return;
	} // if (this instance was allocated earlier in this very tick's sweep)

	if ( in->delay_ms > elapsed_ms )
	{
		in->delay_ms -= elapsed_ms;
		return;
	} // if ( in->delay_ms > elapsed_ms )

	carry = elapsed_ms - in->delay_ms;
	in->delay_ms = 0;

	while ( guard-- )
	{
		fb_msg_t m = in->prog[in->pc];

		switch ( (fb_msg_op_t)m.op )
		{
			case FB_MSG_END:
				finish_instance((int8_t)idx);
				return;

			case FB_MSG_LED_SAVE:
			case FB_MSG_BL_SAVE:
				/* Kept for one-to-one fidelity with the reference notation.
				 * Logical recomputation (recompute_baseline) is used
				 * instead of a saved-register restore -- see
				 * m1_feedback_types.h. */
				in->pc++;
				break;

			case FB_MSG_LED_SET:
				apply_value(FB_RES_LED, m.arg & 0xFFu);
				in->pc++;
				break;

			case FB_MSG_LED_ADD:
				apply_value(FB_RES_LED, (uint8_t)(g_res[FB_RES_LED].last_applied | m.arg));
				in->pc++;
				break;

			case FB_MSG_LED_REMOVE:
				apply_value(FB_RES_LED, (uint8_t)(g_res[FB_RES_LED].last_applied & ~m.arg));
				in->pc++;
				break;

			case FB_MSG_LED_RESTORE:
				g_res[FB_RES_LED].active = -1; /* this instance no longer drives LED */
				recompute_baseline(FB_RES_LED);
				in->pc++;
				break;

			case FB_MSG_LED_RELEASE:
				fb_release_locked(in->owner, FB_RES_LED);
				in->pc++;
				break;

			case FB_MSG_BL_SET:
				apply_value(FB_RES_BL, m.arg > 100u ? 100u : m.arg);
				in->pc++;
				break;

			case FB_MSG_BL_USER:
			case FB_MSG_BL_HOLD_USER:
				apply_value(FB_RES_BL, FB_VALUE_USER_BRIGHTNESS);
				in->pc++;
				break;

			case FB_MSG_BL_OFF:
				apply_value(FB_RES_BL, 0);
				in->pc++;
				break;

			case FB_MSG_BL_RESTORE:
				g_res[FB_RES_BL].active = -1;
				recompute_baseline(FB_RES_BL);
				in->pc++;
				break;

			case FB_MSG_BL_RELEASE:
				fb_release_locked(in->owner, FB_RES_BL);
				in->pc++;
				break;

			case FB_MSG_SND_NOTE:
				if ( g_sound_enabled && g_volume_pct > 0u )
				{
					g_res[FB_RES_SND].pending.dirty = true;
					g_res[FB_RES_SND].pending.is_off = false;
					g_res[FB_RES_SND].pending.value = m.arg;
				} // if (sound enabled)
				/* Sound Off / zero volume silently suppresses the note
				 * (reference section 5.1); the matching MSG_SND_OFF still
				 * always executes below regardless. */
				in->pc++;
				break;

			case FB_MSG_SND_OFF:
				apply_snd_off(); /* unconditional -- reference section 5.1 */
				in->pc++;
				break;

			case FB_MSG_DELAY:
				in->pc++;
				if ( m.arg > carry )
				{
					in->delay_ms = m.arg - carry;
					return;
				}
				carry -= m.arg;
				break;

			case FB_MSG_LOOP_BEGIN:
				if ( in->loop_sp < FB_LOOP_DEPTH )
				{
					in->loop_stack[in->loop_sp].pc = (uint16_t)(in->pc + 1);
					in->loop_stack[in->loop_sp].infinite = (m.arg == 0u);
					in->loop_stack[in->loop_sp].remaining = m.arg;
					in->loop_sp++;
				} // if (room on the loop stack)
				in->pc++;
				break;

			case FB_MSG_LOOP_END:
				if ( in->loop_sp > 0u )
				{
					uint8_t top = (uint8_t)(in->loop_sp - 1u);
					if ( in->loop_stack[top].infinite )
					{
						in->pc = in->loop_stack[top].pc;
					}
					else if ( in->loop_stack[top].remaining > 1u )
					{
						in->loop_stack[top].remaining--;
						in->pc = in->loop_stack[top].pc;
					}
					else
					{
						in->loop_sp--;
						in->pc++;
					} // else (bounded loop complete)
				}
				else
				{
					/* Malformed: MSG_LOOP_END with no matching
					 * MSG_LOOP_BEGIN. Treated as a no-op so a bad sequence
					 * cannot desynchronize the program counter forever. */
					in->pc++;
				} // else (unmatched LOOP_END)
				break;

			default:
				/* Unknown/garbage opcode: skip defensively rather than
				 * looping forever on a corrupted sequence. */
				in->pc++;
				break;
		} // switch ( (fb_msg_op_t)m.op )
	} // while ( guard-- )

	/* Guard exhausted: an unclosed/malformed loop with no MSG_DELAY inside
	 * it would otherwise spin forever inside one tick. Stop making forward
	 * progress for this tick only -- execution resumes on the next tick,
	 * so this can never hang the feedback task (V-series "malformed loop"
	 * requirement). */
} // static void step_instance(uint8_t idx, uint32_t elapsed_ms)



/*************************** P U B L I C   A P I **********************************/

void fb_manager_init(const fb_hw_adapter_t *hw)
{
	uint8_t r;

	FB_ENTER_CRITICAL();
	memset(g_inst, 0, sizeof(g_inst));
	memset(g_res, 0, sizeof(g_res));
	for (r = 0; r < FB_RES_COUNT; r++)
		g_res[r].active = -1;
	g_hw = hw;
	g_sound_enabled = true;
	g_volume_pct = 100;
	g_brightness_pct = 100;
	FB_EXIT_CRITICAL();
} // void fb_manager_init(const fb_hw_adapter_t *hw)



void fb_manager_tick(uint32_t elapsed_ms)
{
	fb_pending_t snap[FB_RES_COUNT];
	uint8_t i;

	FB_ENTER_CRITICAL();
	for (i = 0; i < FB_MAX_ACTIVE; i++)
		step_instance(i, elapsed_ms);

	for (i = 0; i < FB_RES_COUNT; i++)
	{
		snap[i] = g_res[i].pending;
		g_res[i].pending.dirty = false;
	} // for (i = 0; i < FB_RES_COUNT; i++)
	FB_EXIT_CRITICAL();

	if ( g_hw == NULL )
		return;

	if ( snap[FB_RES_LED].dirty && g_hw->led_write != NULL )
		g_hw->led_write((uint8_t)snap[FB_RES_LED].value);

	if ( snap[FB_RES_BL].dirty && g_hw->bl_write != NULL )
		g_hw->bl_write(resolve_bl(snap[FB_RES_BL].value));

	if ( snap[FB_RES_SND].dirty )
	{
		if ( snap[FB_RES_SND].is_off )
		{
			if ( g_hw->snd_off != NULL )
				g_hw->snd_off();
		}
		else if ( g_hw->snd_note != NULL )
		{
			g_hw->snd_note((uint8_t)snap[FB_RES_SND].value);
		} // else if (note)
	} // if (SND resource changed)
} // void fb_manager_tick(uint32_t elapsed_ms)



fb_handle_t fb_request_raw(const fb_msg_t *prog, fb_resource_t resource,
                            fb_owner_t owner, fb_priority_t priority,
                            fb_kind_t kind, fb_seq_id_t tag)
{
	fb_handle_t result = FB_HANDLE_INVALID;
	fb_owner_t blocking_owner;
	fb_priority_t blocking;
	bool admit;

	if ( prog == NULL || resource >= FB_RES_COUNT )
		return FB_HANDLE_INVALID;

	FB_ENTER_CRITICAL();

	blocking = resource_blocking_priority(resource, &blocking_owner);

	if ( priority == FB_PRIO_ADMIN )
		admit = true;
	else if ( blocking == FB_PRIO_NONE )
		admit = true;
	else if ( priority < blocking )
		admit = true;
	else if ( priority == blocking && owner == blocking_owner )
		admit = true;
	else
		admit = false;

	if ( kind == FB_KIND_HOLD )
	{
		int8_t slot = find_hold_slot_for_owner(resource, owner);
		if ( slot < 0 )
			slot = find_free_hold_slot(resource);

		if ( slot >= 0 )
		{
			uint16_t value = extract_hold_value(prog, resource);
			g_res[resource].used[slot] = true;
			g_res[resource].owner[slot] = owner;
			g_res[resource].priority[slot] = priority;
			g_res[resource].value[slot] = value;
			result = (fb_handle_t)slot;
		} // if (a hold slot was available)

		if ( admit && g_res[resource].active >= 0 )
			preempt_active_transient(resource);

		recompute_baseline(resource);
	}
	else /* FB_KIND_ONESHOT or FB_KIND_LOOP */
	{
		if ( !admit )
		{
			if ( kind == FB_KIND_LOOP && resource == FB_RES_LED )
			{
				g_res[resource].susp_valid = true;
				g_res[resource].susp_owner = owner;
				g_res[resource].susp_priority = priority;
				g_res[resource].susp_prog = prog;
				g_res[resource].susp_tag = tag;
			} // if (LED background loop denied -- queue it instead of dropping)
			/* Otherwise: lower-priority one-shots are dropped and are not
			 * replayed later (reference rule 5). */
		}
		else
		{
			int8_t idx;

			if ( g_res[resource].active >= 0 )
				preempt_active_transient(resource);

			idx = alloc_instance();
			if ( idx >= 0 )
			{
				fb_instance_t *in = &g_inst[idx];
				memset(in, 0, sizeof(*in));
				in->in_use = true;
				in->prog = prog;
				in->pc = 0;
				in->delay_ms = 0;
				in->owner = owner;
				in->priority = priority;
				in->kind = kind;
				in->resource = resource;
				in->tag = tag;
				g_res[resource].active = idx;
				result = (fb_handle_t)idx;
			} // if (an instance slot was available)
		} // else (admitted)
	} // else (ONESHOT/LOOP)

	FB_EXIT_CRITICAL();
	return result;
} // fb_handle_t fb_request_raw(...)



static void fb_release_locked(fb_owner_t owner, fb_resource_t r)
{
	uint8_t i;

	for (i = 0; i < FB_MAX_HOLDS; i++)
	{
		if ( g_res[r].used[i] && g_res[r].owner[i] == owner )
			g_res[r].used[i] = false;
	} // for (i = 0; i < FB_MAX_HOLDS; i++)

	if ( g_res[r].active >= 0 && g_inst[g_res[r].active].owner == owner )
	{
		g_inst[g_res[r].active].in_use = false;
		g_res[r].active = -1;
	} // if (this owner's transient is the active one)

	if ( g_res[r].susp_valid && g_res[r].susp_owner == owner )
		g_res[r].susp_valid = false;

	recompute_baseline(r);
} // static void fb_release_locked(fb_owner_t owner, fb_resource_t r)



void fb_release(fb_owner_t owner, fb_resource_t resource)
{
	if ( resource >= FB_RES_COUNT )
		return;
	FB_ENTER_CRITICAL();
	fb_release_locked(owner, resource);
	FB_EXIT_CRITICAL();
} // void fb_release(fb_owner_t owner, fb_resource_t resource)



void fb_stop_led_loop(void)
{
	FB_ENTER_CRITICAL();
	if ( g_res[FB_RES_LED].active >= 0 && g_inst[g_res[FB_RES_LED].active].kind == FB_KIND_LOOP )
	{
		g_inst[g_res[FB_RES_LED].active].in_use = false;
		g_res[FB_RES_LED].active = -1;
		recompute_baseline(FB_RES_LED); /* recomputes correct owner -- never a blind "force off" */
	} // if (a continuous LED loop is currently visible)
	FB_EXIT_CRITICAL();
} // void fb_stop_led_loop(void)



void fb_cleanup_owner(fb_owner_t owner)
{
	fb_resource_t r;

	/* This is the fault/cancel/back-home/timeout/supervisor cleanup path
	 * (reference section 13). Unlike fb_request()/fb_release(), it applies
	 * its result synchronously instead of waiting for the next tick,
	 * because a fault handler needs a guaranteed-silent, guaranteed-
	 * released outcome now, not up to one tick period later. It is
	 * idempotent: calling it twice for an owner holding nothing is a
	 * harmless no-op (V12). */
	FB_ENTER_CRITICAL();
	for (r = 0; r < FB_RES_COUNT; r++)
		fb_release_locked(owner, r);
	FB_EXIT_CRITICAL();

	if ( g_hw == NULL )
		return;

	/* MSG_SND_OFF must always execute, unconditionally. */
	if ( g_hw->snd_off != NULL )
		g_hw->snd_off();
	if ( g_hw->led_write != NULL )
		g_hw->led_write((uint8_t)g_res[FB_RES_LED].last_applied);
	if ( g_hw->bl_write != NULL )
		g_hw->bl_write(resolve_bl(g_res[FB_RES_BL].last_applied));
} // void fb_cleanup_owner(fb_owner_t owner)



void fb_reset_all(void)
{
	/* SEQ_RESET_ALL: supervisor fallback when owner-specific cleanup is
	 * not possible. Wipes every hold/transient/suspended-loop registration
	 * on every resource. Persistent system power state (charging, battery,
	 * emulation, USB, ...) is NOT remembered here -- by design, the
	 * relevant subsystem is expected to re-request it from current
	 * hardware state on the next manager update, exactly as reference
	 * section 13 describes. */
	FB_ENTER_CRITICAL();
	memset(g_inst, 0, sizeof(g_inst));
	memset(g_res, 0, sizeof(g_res));
	g_res[FB_RES_LED].active = -1;
	g_res[FB_RES_BL].active = -1;
	g_res[FB_RES_SND].active = -1;
	FB_EXIT_CRITICAL();

	if ( g_hw == NULL )
		return;

	if ( g_hw->led_write != NULL )
		g_hw->led_write(FB_LED_OFF);
	if ( g_hw->bl_write != NULL )
		g_hw->bl_write(0);
	if ( g_hw->snd_off != NULL )
		g_hw->snd_off();
} // void fb_reset_all(void)



fb_handle_t fb_request(fb_seq_id_t seq_id, fb_owner_t owner, fb_priority_t priority)
{
	const fb_msg_t *prog;
	fb_resource_t resource;
	fb_kind_t kind;

	if ( seq_id == SEQ_RESET_ALL )
	{
		fb_reset_all();
		return 0;
	} // if (SEQ_RESET_ALL)

	if ( !fb_catalogue_lookup(seq_id, &prog, &resource, &kind) )
		return FB_HANDLE_INVALID;

	return fb_request_raw(prog, resource, owner, priority, kind, seq_id);
} // fb_handle_t fb_request(fb_seq_id_t seq_id, fb_owner_t owner, fb_priority_t priority)



/*************************** S E T T I N G S ***************************************/

void fb_set_sound_enabled(bool enabled)
{
	FB_ENTER_CRITICAL();
	g_sound_enabled = enabled;
	FB_EXIT_CRITICAL();
} // void fb_set_sound_enabled(bool enabled)

bool fb_get_sound_enabled(void)
{
	return g_sound_enabled;
} // bool fb_get_sound_enabled(void)

void fb_set_alert_volume_pct(uint8_t pct)
{
	FB_ENTER_CRITICAL();
	g_volume_pct = pct > 100u ? 100u : pct;
	FB_EXIT_CRITICAL();
} // void fb_set_alert_volume_pct(uint8_t pct)

uint8_t fb_get_alert_volume_pct(void)
{
	return g_volume_pct;
} // uint8_t fb_get_alert_volume_pct(void)

void fb_set_brightness_pct(uint8_t pct)
{
	FB_ENTER_CRITICAL();
	g_brightness_pct = pct > 100u ? 100u : pct;
	if ( g_res[FB_RES_BL].last_applied == FB_VALUE_USER_BRIGHTNESS )
	{
		/* A live user-brightness-sourced output must reflect a settings
		 * change immediately (reference section 5.3), not just on the
		 * next state transition. */
		g_res[FB_RES_BL].pending.dirty = true;
		g_res[FB_RES_BL].pending.value = FB_VALUE_USER_BRIGHTNESS;
	} // if (currently showing the user-brightness-tracking value)
	FB_EXIT_CRITICAL();
} // void fb_set_brightness_pct(uint8_t pct)

uint8_t fb_get_brightness_pct(void)
{
	return g_brightness_pct;
} // uint8_t fb_get_brightness_pct(void)



/*************************** I N T R O S P E C T I O N *****************************/

uint8_t fb_debug_visible_led(void)
{
	return (uint8_t)g_res[FB_RES_LED].last_applied;
} // uint8_t fb_debug_visible_led(void)

uint8_t fb_debug_visible_bl(void)
{
	return resolve_bl(g_res[FB_RES_BL].last_applied);
} // uint8_t fb_debug_visible_bl(void)

bool fb_debug_snd_idle(void)
{
	return g_res[FB_RES_SND].active < 0;
} // bool fb_debug_snd_idle(void)

bool fb_debug_led_loop_active(void)
{
	return g_res[FB_RES_LED].active >= 0 &&
	       g_inst[g_res[FB_RES_LED].active].kind == FB_KIND_LOOP;
} // bool fb_debug_led_loop_active(void)

bool fb_debug_led_loop_suspended(void)
{
	return g_res[FB_RES_LED].susp_valid;
} // bool fb_debug_led_loop_suspended(void)
