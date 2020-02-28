/*
 * Oracle Linux DTrace.
 * Copyright (c) 2020, Oracle and/or its affiliates. All rights reserved.
 * Licensed under the Universal Permissive License v 1.0 as shown at
 * http://oss.oracle.com/licenses/upl.
 */
/* @@xfail: dtv2 */

/*
 * ASSERTION: The 'timestamp' variable can be accessed and is not -1.
 *
 * SECTION: Variables/Built-in Variables/timestamp
 */

#pragma D option quiet

BEGIN {
	trace(timestamp);
	exit(timestamp != -1 ? 0 : 1);
}
