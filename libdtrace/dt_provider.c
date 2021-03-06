/*
 * Oracle Linux DTrace.
 * Copyright (c) 2006, 2019, Oracle and/or its affiliates. All rights reserved.
 * Licensed under the Universal Permissive License v 1.0 as shown at
 * http://oss.oracle.com/licenses/upl.
 */

#include <sys/types.h>
#include <sys/bitmap.h>

#include <assert.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <alloca.h>
#include <unistd.h>
#include <errno.h>
#include <port.h>

#include <dt_provider.h>
#include <dt_module.h>
#include <dt_string.h>
#include <dt_list.h>

static dt_provider_t *
dt_provider_insert(dtrace_hdl_t *dtp, dt_provider_t *pvp, uint_t h)
{
	dt_list_append(&dtp->dt_provlist, pvp);

	pvp->pv_next = dtp->dt_provs[h];
	dtp->dt_provs[h] = pvp;
	dtp->dt_nprovs++;

	return pvp;
}

dt_provider_t *
dt_provider_lookup(dtrace_hdl_t *dtp, const char *name)
{
	uint_t h = dt_strtab_hash(name, NULL) % dtp->dt_provbuckets;
	dt_provider_t *pvp;

	for (pvp = dtp->dt_provs[h]; pvp != NULL; pvp = pvp->pv_next) {
		if (strcmp(pvp->desc.dtvd_name, name) == 0)
			return pvp;
	}

	return NULL;
}

dt_provider_t *
dt_provider_create(dtrace_hdl_t *dtp, const char *name,
		   const dt_provimpl_t *impl, const dtrace_pattr_t *pattr)
{
	dt_provider_t *pvp;

	if ((pvp = dt_zalloc(dtp, sizeof (dt_provider_t))) == NULL)
		return (NULL);

	strlcpy(pvp->desc.dtvd_name, name, DTRACE_PROVNAMELEN);
	pvp->impl = impl;
	pvp->pv_probes = dt_idhash_create(pvp->desc.dtvd_name, NULL, 0, 0);
	pvp->pv_gen = dtp->dt_gen;
	pvp->pv_hdl = dtp;

	if (pvp->pv_probes == NULL) {
		dt_free(dtp, pvp);
		(void) dt_set_errno(dtp, EDT_NOMEM);
		return (NULL);
	}

	memcpy(&pvp->desc.dtvd_attr, pattr, sizeof(dtrace_pattr_t));

	return (dt_provider_insert(dtp, pvp,
	    dt_strtab_hash(name, NULL) % dtp->dt_provbuckets));
}

void
dt_provider_destroy(dtrace_hdl_t *dtp, dt_provider_t *pvp)
{
	dt_provider_t **pp;
	uint_t h;

	assert(pvp->pv_hdl == dtp);

	h = dt_strtab_hash(pvp->desc.dtvd_name, NULL) % dtp->dt_provbuckets;
	pp = &dtp->dt_provs[h];

	while (*pp != NULL && *pp != pvp)
		pp = &(*pp)->pv_next;

	assert(*pp != NULL && *pp == pvp);
	*pp = pvp->pv_next;

	dt_list_delete(&dtp->dt_provlist, pvp);
	dtp->dt_nprovs--;

	if (pvp->pv_probes != NULL)
		dt_idhash_destroy(pvp->pv_probes);

	dt_node_link_free(&pvp->pv_nodes);
	dt_free(dtp, pvp->pv_xrefs);
	dt_free(dtp, pvp);
}

int
dt_provider_xref(dtrace_hdl_t *dtp, dt_provider_t *pvp, id_t id)
{
	size_t oldsize = BT_SIZEOFMAP(pvp->pv_xrmax);
	size_t newsize = BT_SIZEOFMAP(dtp->dt_xlatorid);

	assert(id >= 0 && id < dtp->dt_xlatorid);

	if (newsize > oldsize) {
		ulong_t *xrefs = dt_zalloc(dtp, newsize);

		if (xrefs == NULL)
			return (-1);

		memcpy(xrefs, pvp->pv_xrefs, oldsize);
		dt_free(dtp, pvp->pv_xrefs);

		pvp->pv_xrefs = xrefs;
		pvp->pv_xrmax = dtp->dt_xlatorid;
	}

	BT_SET(pvp->pv_xrefs, id);
	return (0);
}
