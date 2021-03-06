/*
 * Oracle Linux DTrace.
 * Copyright (c) 2020, Oracle and/or its affiliates. All rights reserved.
 * Licensed under the Universal Permissive License v 1.0 as shown at
 * http://oss.oracle.com/licenses/upl.
 */

/*
 * ASSERTION: The printf action supports '%d' for unsigned integers.
 *
 * SECTION: Actions/printf()
 */

#pragma D option quiet

BEGIN
{
	printf("%d", (uint)-12345);
	exit(0);
}
