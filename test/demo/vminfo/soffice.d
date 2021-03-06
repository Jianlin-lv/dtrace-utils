/*
 * Oracle Linux DTrace.
 * Copyright (c) 2005, Oracle and/or its affiliates. All rights reserved.
 * Licensed under the Universal Permissive License v 1.0 as shown at
 * http://oss.oracle.com/licenses/upl.
 */

vminfo:::maj_fault,
vminfo:::zfod,
vminfo:::as_fault
/execname == "soffice.bin" && start == 0/
{
	/*
	 * This is the first time that a vminfo probe has been hit; record
	 * our initial timestamp.
	 */
	start = timestamp;
}

vminfo:::maj_fault,
vminfo:::zfod,
vminfo:::as_fault
/execname == "soffice.bin"/
{
	/*
	 * Aggregate on the probename, and lquantize() the number of seconds
	 * since our initial timestamp.  (There are 1,000,000,000 nanoseconds
	 * in a second.)  We assume that the script will be terminated before
	 * 60 seconds elapses.
	 */
	@[probename] =
	    lquantize((timestamp - start) / 1000000000, 0, 60);
}
