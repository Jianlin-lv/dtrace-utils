/*
 * Oracle Linux DTrace.
 * Copyright (c) 2006, 2020, Oracle and/or its affiliates. All rights reserved.
 * Licensed under the Universal Permissive License v 1.0 as shown at
 * http://oss.oracle.com/licenses/upl.
 */
/* @@xfail: dtv2 */

#pragma D option bufsize=1000
#pragma D option bufpolicy=ring
#pragma D option statusrate=10ms

fbt:::
{
	on = (timestamp / 1000000000) & 1;
}

fbt:::
/on/
{
	trace(strstr((char *)rand(), (char *)(rand() ^ vtimestamp)));
}

fbt:::entry
/on/
{
	trace(strstr((char *)arg0, (char *)arg1));
}

tick-1sec
/n++ == 10/
{
	exit(0);
}
