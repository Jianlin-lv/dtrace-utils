/*
 * Oracle Linux DTrace.
 * Copyright (c) 2006, Oracle and/or its affiliates. All rights reserved.
 * Licensed under the Universal Permissive License v 1.0 as shown at
 * http://oss.oracle.com/licenses/upl.
 */
/* @@xfail: dtv2 */

/*
 * ASSERTION:
 *  lquantize() upper bound around must be an integer constant
 *
 * SECTION: Aggregations/Aggregations
 *
 */

BEGIN
{
	x = 'a';
	@a[1] = lquantize(timestamp, 100, 2147483657, 1);
}

