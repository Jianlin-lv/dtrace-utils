/*
 * FILE:	dtrace_ecb.c
 * DESCRIPTION:	Dynamic Tracing: ECB functions
 *
 * Copyright (C) 2010 Oracle Corporation
 */

#include <linux/slab.h>

#include "dtrace.h"

dtrace_ecb_t	*dtrace_ecb_create_cache;

static dtrace_action_t *dtrace_ecb_aggregation_create(dtrace_ecb_t *ecb,
						      dtrace_actdesc_t *desc)
{
	dtrace_aggregation_t	*agg;
	size_t			size = sizeof (uint64_t);
	int			ntuple = desc->dtad_ntuple;
	dtrace_action_t		*act;
	dtrace_recdesc_t	*frec;
	dtrace_aggid_t		aggid;
	dtrace_state_t		*state = ecb->dte_state;

	agg = kzalloc(sizeof (dtrace_aggregation_t), GFP_KERNEL);
	agg->dtag_ecb = ecb;

	ASSERT(DTRACEACT_ISAGG(desc->dtad_kind));

	switch (desc->dtad_kind) {
	case DTRACEAGG_MIN:
		agg->dtag_initial = UINT64_MAX;
		agg->dtag_aggregate = dtrace_aggregate_min;
		break;

	case DTRACEAGG_MAX:
		agg->dtag_initial = UINT64_MIN;
		agg->dtag_aggregate = dtrace_aggregate_max;
		break;

	case DTRACEAGG_COUNT:
		agg->dtag_aggregate = dtrace_aggregate_count;
		break;

	case DTRACEAGG_QUANTIZE:
		agg->dtag_aggregate = dtrace_aggregate_quantize;
		size = (((sizeof (uint64_t) * NBBY) - 1) * 2 + 1) *
		       sizeof (uint64_t);
		break;

	case DTRACEAGG_LQUANTIZE: {
		uint16_t	step = DTRACE_LQUANTIZE_STEP(desc->dtad_arg);
		uint16_t	levels =
				DTRACE_LQUANTIZE_LEVELS(desc->dtad_arg);

		agg->dtag_initial = desc->dtad_arg;
		agg->dtag_aggregate = dtrace_aggregate_lquantize;

		if (step == 0 || levels == 0)
			goto err;

		size = levels * sizeof (uint64_t) + 3 * sizeof (uint64_t);
		break;
	}

	case DTRACEAGG_AVG:
		agg->dtag_aggregate = dtrace_aggregate_avg;
		size = sizeof (uint64_t) * 2;
		break;

	case DTRACEAGG_STDDEV:
		agg->dtag_aggregate = dtrace_aggregate_stddev;
		size = sizeof (uint64_t) * 4;
		break;

	case DTRACEAGG_SUM:
		agg->dtag_aggregate = dtrace_aggregate_sum;
		break;

	default:
		goto err;
	}

	agg->dtag_action.dta_rec.dtrd_size = size;

	if (ntuple == 0)
		goto err;

	for (act = ecb->dte_action_last; act != NULL; act = act->dta_prev) {
		if (DTRACEACT_ISAGG(act->dta_kind))
			break;

		if (--ntuple == 0) {
			agg->dtag_first = act;
			goto success;
		}
	}

	ASSERT(ntuple != 0);
err:
	kfree(agg);
	return NULL;

success:
	ASSERT(ecb->dte_action_last != NULL);
	act = ecb->dte_action_last;

	if (act->dta_kind == DTRACEACT_DIFEXPR) {
		ASSERT(act->dta_difo != NULL);

		if (act->dta_difo->dtdo_rtype.dtdt_size == 0)
			agg->dtag_hasarg = 1;
	}

	/*
	 * KVH FIXME: I am not too sure this is the best way to handle
	 * aggregate ids.  Essentially, a 1 byte allocation is performed,
	 * resulting in a unique virtual address that is converted into an
	 * integer value and used as id.  On Linux, I believe that this
	 * technique results in overhead due to the allocation.  I changed this
	 * to use kmalloc to aovid the vmalloc overhead (since vmalloc aligns
	 * all allocations on a page boundary).
	 */
	aggid = (dtrace_aggid_t)(uintptr_t)kmalloc(1, GFP_KERNEL);

	if (aggid - 1 >= state->dts_naggregations) {
		dtrace_aggregation_t	**oaggs = state->dts_aggregations;
		dtrace_aggregation_t	**aggs;
		int			naggs = state->dts_naggregations << 1;
		int			onaggs = state->dts_naggregations;

		ASSERT(aggid == state->dts_naggregations + 1);

		if (naggs == 0) {
			ASSERT(oaggs == NULL);

			naggs = 1;
		}

		aggs = kzalloc(naggs * sizeof (*aggs), GFP_KERNEL);
		if (oaggs != NULL) {
			memcpy(oaggs, aggs, onaggs * sizeof (*aggs));
			kfree(oaggs);
		}

		state->dts_aggregations = aggs;
		state->dts_naggregations = naggs;
	}

	ASSERT(state->dts_aggregations[aggid - 1] == NULL);
	state->dts_aggregations[(agg->dtag_id = aggid) - 1] = agg;

	frec = &agg->dtag_first->dta_rec;
	if (frec->dtrd_alignment < sizeof (dtrace_aggid_t))
		frec->dtrd_alignment = sizeof (dtrace_aggid_t);

	for (act = agg->dtag_first; act != NULL; act = act->dta_next) {
		ASSERT(!act->dta_intuple);

		act->dta_intuple = 1;
	}

	return &agg->dtag_action;
}

void dtrace_ecb_aggregation_destroy(dtrace_ecb_t *ecb, dtrace_action_t *act)
{
	dtrace_aggregation_t	*agg = (dtrace_aggregation_t *)act;
	dtrace_state_t		*state = ecb->dte_state;
	dtrace_aggid_t		aggid = agg->dtag_id;

	ASSERT(DTRACEACT_ISAGG(act->dta_kind));
	kfree(state->dts_aggid_arena);

	ASSERT(state->dts_aggregations[aggid - 1] == agg);
	state->dts_aggregations[aggid - 1] = NULL;

	kfree(agg);
}

static int dtrace_ecb_action_add(dtrace_ecb_t *ecb, dtrace_actdesc_t *desc)
{
	dtrace_action_t		*action, *last;
	dtrace_difo_t		*dp = desc->dtad_difo;
	uint32_t		size = 0, align = sizeof (uint8_t), mask;
	uint16_t		format = 0;
	dtrace_recdesc_t	*rec;
	dtrace_state_t		*state = ecb->dte_state;
	dtrace_optval_t		*opt = state->dts_options, nframes, strsize;
	uint64_t		arg = desc->dtad_arg;

	ASSERT(mutex_is_locked(&dtrace_lock));
	ASSERT(ecb->dte_action == NULL || ecb->dte_action->dta_refcnt == 1);

	if (DTRACEACT_ISAGG(desc->dtad_kind)) {
		dtrace_action_t	*act;

		for (act = ecb->dte_action; act != NULL; act = act->dta_next) {
			if (act->dta_kind == DTRACEACT_COMMIT)
				return -EINVAL;

			if (act->dta_kind == DTRACEACT_SPECULATE)
				return -EINVAL;
		}

		action = dtrace_ecb_aggregation_create(ecb, desc);
		if (action == NULL)
			return -EINVAL;
	} else {
		if (DTRACEACT_ISDESTRUCTIVE(desc->dtad_kind) ||
		    (desc->dtad_kind == DTRACEACT_DIFEXPR &&
		     dp != NULL && dp->dtdo_destructive))
			state->dts_destructive = 1;

		switch (desc->dtad_kind) {
		case DTRACEACT_PRINTF:
		case DTRACEACT_PRINTA:
		case DTRACEACT_SYSTEM:
		case DTRACEACT_FREOPEN:
			if ((void *)(uintptr_t)arg == NULL) {
				ASSERT(desc->dtad_kind == DTRACEACT_PRINTA);
	
				format = 0;
			} else {
				ASSERT((void *)(uintptr_t)arg != NULL);
#ifdef FIXME
				ASSERT(arg > KERNELBASE);
#endif

				format = dtrace_format_add(
						state, (char *)(uintptr_t)arg);
			}

		case DTRACEACT_LIBACT:
		case DTRACEACT_DIFEXPR:
			if (dp == NULL)
				return -EINVAL;

			if ((size = dp->dtdo_rtype.dtdt_size) != 0)
				break;

			if (dp->dtdo_rtype.dtdt_kind == DIF_TYPE_STRING) {
				if (!(dp->dtdo_rtype.dtdt_flags & DIF_TF_BYREF))
					return -EINVAL;

				size = opt[DTRACEOPT_STRSIZE];
			}

			break;

		case DTRACEACT_STACK:
			if ((nframes = arg) == 0) {
				nframes = opt[DTRACEOPT_STACKFRAMES];

				ASSERT(nframes > 0);

				arg = nframes;
			}

			size = nframes * sizeof (pc_t);
			break;

		case DTRACEACT_JSTACK:
			if ((strsize = DTRACE_USTACK_STRSIZE(arg)) == 0)
				strsize = opt[DTRACEOPT_JSTACKSTRSIZE];

			if ((nframes = DTRACE_USTACK_NFRAMES(arg)) == 0)
				nframes = opt[DTRACEOPT_JSTACKFRAMES];

			arg = DTRACE_USTACK_ARG(nframes, strsize);

		case DTRACEACT_USTACK:
			if (desc->dtad_kind != DTRACEACT_JSTACK &&
			    (nframes = DTRACE_USTACK_NFRAMES(arg)) == 0) {
				strsize = DTRACE_USTACK_STRSIZE(arg);
				nframes = opt[DTRACEOPT_USTACKFRAMES];

				ASSERT(nframes > 0);

				arg = DTRACE_USTACK_ARG(nframes, strsize);
			}

			size = (nframes + 1) * sizeof (uint64_t);
			size += DTRACE_USTACK_STRSIZE(arg);
			size = P2ROUNDUP(size, (uint32_t)(sizeof (uintptr_t)));

			break;

		case DTRACEACT_SYM:
		case DTRACEACT_MOD:
			if (dp == NULL || ((size = dp->dtdo_rtype.dtdt_size) !=
					   sizeof (uint64_t)) ||
			    (dp->dtdo_rtype.dtdt_flags & DIF_TF_BYREF))
				return -EINVAL;

			break;

		case DTRACEACT_USYM:
		case DTRACEACT_UMOD:
		case DTRACEACT_UADDR:
			if (dp == NULL ||
			    (dp->dtdo_rtype.dtdt_size != sizeof (uint64_t)) ||
			    (dp->dtdo_rtype.dtdt_flags & DIF_TF_BYREF))
				return -EINVAL;

			size = 2 * sizeof (uint64_t);

			break;

		case DTRACEACT_STOP:
		case DTRACEACT_BREAKPOINT:
		case DTRACEACT_PANIC:
			break;

		case DTRACEACT_CHILL:
		case DTRACEACT_DISCARD:
		case DTRACEACT_RAISE:
			if (dp == NULL)
				return -EINVAL;

			break;

		case DTRACEACT_EXIT:
			if (dp == NULL || (size = dp->dtdo_rtype.dtdt_size) !=
					   sizeof (int) ||
			    (dp->dtdo_rtype.dtdt_flags & DIF_TF_BYREF))
				return -EINVAL;

			break;

		case DTRACEACT_SPECULATE:
			if (ecb->dte_size > sizeof (dtrace_epid_t))
				return -EINVAL;

			if (dp == NULL)
				return -EINVAL;

			state->dts_speculates = 1;

			break;

		case DTRACEACT_COMMIT: {
			dtrace_action_t	*act = ecb->dte_action;

			for (; act != NULL; act = act->dta_next) {
				if (act->dta_kind == DTRACEACT_COMMIT)
					return -EINVAL;
			}

			if (dp == NULL)
				return -EINVAL;

			break;
		}

		default:
			return -EINVAL;
		}

		if (size != 0 || desc->dtad_kind == DTRACEACT_SPECULATE) {
			dtrace_action_t	*act = ecb->dte_action;

			for (; act != NULL; act = act->dta_next) {
				if (act->dta_kind == DTRACEACT_COMMIT)
					return -EINVAL;
			}
		}

		action = kzalloc(sizeof (dtrace_action_t), GFP_KERNEL);
		action->dta_rec.dtrd_size = size;
	}

	action->dta_refcnt = 1;
	rec = &action->dta_rec;
	size = rec->dtrd_size;

	for (mask = sizeof (uint64_t) - 1; size != 0 && mask > 0; mask >>= 1) {
		if (!(size & mask)) {
			align = mask + 1;

			break;
		}
	}

	action->dta_kind = desc->dtad_kind;

	if ((action->dta_difo = dp) != NULL)
		dtrace_difo_hold(dp);

	rec->dtrd_action = action->dta_kind;
	rec->dtrd_arg = arg;
	rec->dtrd_uarg = desc->dtad_uarg;
	rec->dtrd_alignment = (uint16_t)align;
	rec->dtrd_format = format;

	if ((last = ecb->dte_action_last) != NULL) {
		ASSERT(ecb->dte_action != NULL);

		action->dta_prev = last;
		last->dta_next = action;
	} else {
		ASSERT(ecb->dte_action == NULL);

		ecb->dte_action = action;
	}

	ecb->dte_action_last = action;

	return 0;
}

static void dtrace_ecb_action_remove(dtrace_ecb_t *ecb)
{
	dtrace_action_t	*act = ecb->dte_action, *next;
	dtrace_vstate_t	*vstate = &ecb->dte_state->dts_vstate;
	dtrace_difo_t	*dp;
	uint16_t	format;

	if (act != NULL && act->dta_refcnt > 1) {
		ASSERT(act->dta_next == NULL || act->dta_next->dta_refcnt == 1);

		act->dta_refcnt--;
	} else {
		for (; act != NULL; act = next) {
			next = act->dta_next;
			ASSERT(next != NULL || act == ecb->dte_action_last);
			ASSERT(act->dta_refcnt == 1);

			if ((format = act->dta_rec.dtrd_format) != 0)
				dtrace_format_remove(ecb->dte_state, format);

			if ((dp = act->dta_difo) != NULL)
				dtrace_difo_release(dp, vstate);

			if (DTRACEACT_ISAGG(act->dta_kind))
				dtrace_ecb_aggregation_destroy(ecb, act);
			else
				kfree(act);
		}
	}

	ecb->dte_action = NULL;
	ecb->dte_action_last = NULL;
	ecb->dte_size = sizeof (dtrace_epid_t);
}

/*
 * Disable the ECB by removing it from its probe.
 */
void dtrace_ecb_disable(dtrace_ecb_t *ecb)
{
	dtrace_ecb_t	*pecb, *prev = NULL;
	dtrace_probe_t	*probe = ecb->dte_probe;

	ASSERT(mutex_is_locked(&dtrace_lock));

	if (probe == NULL)
		return;

	for (pecb = probe->dtpr_ecb; pecb != NULL; pecb = pecb->dte_next) {
		if (pecb == ecb)
			break;

		prev = pecb;
	}

	ASSERT(pecb != NULL);

	if (prev == NULL)
		probe->dtpr_ecb = ecb->dte_next;
	else
		prev->dte_next = ecb->dte_next;

	if (ecb == probe->dtpr_ecb_last) {
		ASSERT(ecb->dte_next == NULL);
		probe->dtpr_ecb_last = prev;
	}

	/*
	 * The ECB has been disconnected from the probe; now sync to assure
	 * that all CPUs have seen the change before returning.
	 */
	dtrace_sync();

	if (probe->dtpr_ecb == NULL) {
		/*
		 * That was the last ECB on the probe; clear the predicate
		 * cache ID for the probe, disable it and sync one more time
		 * to assure that we'll never hit it again.
		 */
		dtrace_provider_t	*prov = probe->dtpr_provider;

		ASSERT(ecb->dte_next == NULL);
		ASSERT(probe->dtpr_ecb_last == NULL);

		probe->dtpr_predcache = DTRACE_CACHEIDNONE;
		prov->dtpv_pops.dtps_disable(prov->dtpv_arg,
		probe->dtpr_id, probe->dtpr_arg);

		dtrace_sync();
	} else {
		/*
		 * There is at least one ECB remaining on the probe.  If there
		 * is _exactly_ one, set the probe's predicate cache ID to be
		 * the predicate cache ID of the remaining ECB.
		 */
		ASSERT(probe->dtpr_ecb_last != NULL);
		ASSERT(probe->dtpr_predcache == DTRACE_CACHEIDNONE);

		if (probe->dtpr_ecb == probe->dtpr_ecb_last) {
			dtrace_predicate_t	*p =
						probe->dtpr_ecb->dte_predicate;

			ASSERT(probe->dtpr_ecb->dte_next == NULL);

			if (p != NULL)
				probe->dtpr_predcache = p->dtp_cacheid;
		}

		ecb->dte_next = NULL;
	}
}

static dtrace_ecb_t *dtrace_ecb_add(dtrace_state_t *state,
				    dtrace_probe_t *probe)
{
	dtrace_ecb_t	*ecb;
	dtrace_epid_t	epid;

	ASSERT(mutex_is_locked(&dtrace_lock));

	ecb = kzalloc(sizeof (dtrace_ecb_t), GFP_KERNEL);
	ecb->dte_predicate = NULL;
	ecb->dte_probe = probe;
	ecb->dte_size = ecb->dte_needed = sizeof (dtrace_epid_t);
	ecb->dte_alignment = sizeof (dtrace_epid_t);

	epid = state->dts_epid++;

	if (epid - 1 >= state->dts_necbs) {
		dtrace_ecb_t	**oecbs = state->dts_ecbs, **ecbs;
		int		necbs = state->dts_necbs << 1;

		ASSERT(epid == state->dts_necbs + 1);

		if (necbs == 0) {
			ASSERT(oecbs == NULL);

			necbs = 1;
		}

		ecbs = kcalloc(necbs, sizeof (*ecbs), GFP_KERNEL);


		if (oecbs != NULL)
			memcpy(oecbs, ecbs, state->dts_necbs * sizeof (*ecbs));

		dtrace_membar_producer();

		state->dts_ecbs = ecbs;

		if (oecbs != NULL) {
			if (state->dts_activity != DTRACE_ACTIVITY_INACTIVE)
				dtrace_sync();

			kfree(oecbs);
		}

		dtrace_membar_producer();

		state->dts_necbs = necbs;
	}

	ecb->dte_state = state;

	ASSERT(state->dts_ecbs[epid - 1] == NULL);

	dtrace_membar_producer();

	state->dts_ecbs[(ecb->dte_epid = epid) - 1] = ecb;

	return ecb;
}

static dtrace_ecb_t *dtrace_ecb_create(dtrace_state_t *state,
				       dtrace_probe_t *probe,
				       dtrace_enabling_t *enab)
{
	dtrace_ecb_t		*ecb;
	dtrace_predicate_t	*pred;
	dtrace_actdesc_t	*act;
	dtrace_provider_t	*prov;
	dtrace_ecbdesc_t	*desc = enab->dten_current;

	ASSERT(mutex_is_locked(&dtrace_lock));
	ASSERT(state != NULL);

	ecb = dtrace_ecb_add(state, probe);
	ecb->dte_uarg = desc->dted_uarg;

	if ((pred = desc->dted_pred.dtpdd_predicate) != NULL) {
		dtrace_predicate_hold(pred);
		ecb->dte_predicate = pred;
	}

	if (probe != NULL) {
		prov = probe->dtpr_provider;

		if (!(state->dts_cred.dcr_visible & DTRACE_CRV_ALLPROC) &&
		    (prov->dtpv_priv.dtpp_flags & DTRACE_PRIV_USER))
			ecb->dte_cond |= DTRACE_COND_OWNER;

		if (!(state->dts_cred.dcr_visible & DTRACE_CRV_KERNEL) &&
		    (prov->dtpv_priv.dtpp_flags & DTRACE_PRIV_KERNEL))
			ecb->dte_cond |= DTRACE_COND_USERMODE;
	}

	if (dtrace_ecb_create_cache != NULL) {
		dtrace_ecb_t	*cached = dtrace_ecb_create_cache;
		dtrace_action_t	*act = cached->dte_action;

		if (act != NULL) {
			ASSERT(act->dta_refcnt > 0);

			act->dta_refcnt++;
			ecb->dte_action = act;
			ecb->dte_action_last = cached->dte_action_last;
			ecb->dte_needed = cached->dte_needed;
			ecb->dte_size = cached->dte_size;
			ecb->dte_alignment = cached->dte_alignment;
		}

		return ecb;
	}

	for (act = desc->dted_action; act != NULL; act = act->dtad_next) {
		if ((enab->dten_error = dtrace_ecb_action_add(ecb, act)) != 0) {
			dtrace_ecb_destroy(ecb);
			return NULL;
		}
	}

	dtrace_ecb_resize(ecb);

	return (dtrace_ecb_create_cache = ecb);
}

int dtrace_ecb_create_enable(dtrace_probe_t *probe, void *arg)
{
	dtrace_ecb_t		*ecb;
	dtrace_enabling_t	*enab = arg;
	dtrace_state_t		*state = enab->dten_vstate->dtvs_state;

	ASSERT(state != NULL);

	if (probe != NULL && probe->dtpr_gen < enab->dten_probegen)
		return DTRACE_MATCH_NEXT;

	if ((ecb = dtrace_ecb_create(state, probe, enab)) == NULL)
		return DTRACE_MATCH_DONE;

	if (dtrace_ecb_enable(ecb) < 0)
		return DTRACE_MATCH_FAIL;

	return DTRACE_MATCH_NEXT;
}

void dtrace_ecb_destroy(dtrace_ecb_t *ecb)
{
	dtrace_state_t		*state = ecb->dte_state;
	dtrace_vstate_t		*vstate = &state->dts_vstate;
	dtrace_predicate_t	*pred;
	dtrace_epid_t		epid = ecb->dte_epid;

	ASSERT(mutex_is_locked(&dtrace_lock));
	ASSERT(ecb->dte_next == NULL);
	ASSERT(ecb->dte_probe == NULL || ecb->dte_probe->dtpr_ecb != ecb);

	if ((pred = ecb->dte_predicate) != NULL)
		dtrace_predicate_release(pred, vstate);

	dtrace_ecb_action_remove(ecb);

	ASSERT(state->dts_ecbs[epid - 1] == ecb);
	state->dts_ecbs[epid - 1] = NULL;

	kfree(ecb);
}

void dtrace_ecb_resize(dtrace_ecb_t *ecb)
{
	/* INCOMPLETE */
}

int dtrace_ecb_enable(dtrace_ecb_t *ecb)
{
	dtrace_probe_t	*probe = ecb->dte_probe;

	ASSERT(mutex_is_locked(&cpu_lock));
	ASSERT(mutex_is_locked(&dtrace_lock));
	ASSERT(ecb->dte_next == NULL);

	if (probe == NULL)
		return 0;

	if (probe->dtpr_ecb == NULL) {
		dtrace_provider_t	*prov = probe->dtpr_provider;

		probe->dtpr_ecb = probe->dtpr_ecb_last = ecb;


		if (ecb->dte_predicate != NULL)
			probe->dtpr_predcache = ecb->dte_predicate->dtp_cacheid;

		return prov->dtpv_pops.dtps_enable(prov->dtpv_arg,
						   probe->dtpr_id,
						   probe->dtpr_arg);
	} else {
		ASSERT(probe->dtpr_ecb_last != NULL);

		probe->dtpr_ecb_last->dte_next = ecb;
		probe->dtpr_ecb_last = ecb;
		probe->dtpr_predcache = 0;

		dtrace_sync();

		return 0;
	}
}

dtrace_ecb_t *dtrace_epid2ecb(dtrace_state_t *state, dtrace_epid_t id)
{
	dtrace_ecb_t *ecb;

	ASSERT(mutex_is_locked(&dtrace_lock));

	if (id == 0 || id > state->dts_necbs)
		return NULL;

	ASSERT(state->dts_necbs > 0 && state->dts_ecbs != NULL);
	ecb = state->dts_ecbs[id - 1];
	ASSERT(ecb == NULL || ecb->dte_epid == id);

	return ecb;
}

dtrace_aggregation_t *dtrace_aggid2agg(dtrace_state_t *state,
				       dtrace_aggid_t id)
{
	dtrace_aggregation_t *agg;

	ASSERT(mutex_is_locked(&dtrace_lock));

	if (id == 0 || id > state->dts_naggregations)
		return (NULL);

	ASSERT(state->dts_naggregations > 0 && state->dts_aggregations != NULL);
	agg = state->dts_aggregations[id - 1];
	ASSERT(agg == NULL || agg->dtag_id == id);

	return agg;
}
