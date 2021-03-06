/*
 * Oracle Linux DTrace.
 * Copyright (c) 2006, Oracle and/or its affiliates. All rights reserved.
 * Licensed under the Universal Permissive License v 1.0 as shown at
 * http://oss.oracle.com/licenses/upl.
 */


/*
 * ASSERTION:
 *  An aggregation must call an aggregating function, not a probe
 *
 * SECTION: Aggregations/Aggregations
 *
 */

BEGIN
{

	@counts[0][2] = tick-1();
	exit(0);
}

