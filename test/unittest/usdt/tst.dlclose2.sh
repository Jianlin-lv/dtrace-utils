#!/bin/bash
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

#
# Copyright 2006 Oracle, Inc.  All rights reserved.
# Use is subject to license terms.
#
#ident	"%Z%%M%	%I%	%E% SMI"

PATH=/usr/bin:/usr/sbin:$PATH

if [ $# != 1 ]; then
	echo expected one argument: '<'dtrace-path'>'
	exit 2
fi

dtrace=$1
CC=/usr/bin/gcc
CFLAGS=

mkdir $tmpdir/usdt-dlclose2
cd $tmpdir/usdt-dlclose2

cat > Makefile <<EOF
all: main livelib.so deadlib.so

main: main.o prov.o
	\$(CC) -o main main.o -ldl

main.o: main.c
	\$(CC) -c main.c


livelib.so: livelib.o prov.o
	\$(CC) -shared -o livelib.so livelib.o prov.o -lc

livelib.o: livelib.c prov.h
	\$(CC) -c livelib.c

prov.o: livelib.o prov.d
	$dtrace -G -s prov.d livelib.o

prov.h: prov.d
	$dtrace -h -s prov.d


deadlib.so: deadlib.o
	\$(CC) -shared -o deadlib.so deadlib.o -lc

deadlib.o: deadlib.c
	\$(CC) -c deadlib.c

clean:
	rm -f main.o livelib.o prov.o prov.h deadlib.o

clobber: clean
	rm -f main livelib.so deadlib.so
EOF

cat > prov.d <<EOF
provider test_prov {
	probe go();
};
EOF

cat > livelib.c <<EOF
#include "prov.h"

void
go(void)
{
	TEST_PROV_GO();
}
EOF

cat > deadlib.c <<EOF
void
go(void)
{
}
EOF


cat > main.c <<EOF
#include <dlfcn.h>
#include <unistd.h>
#include <stdio.h>

int
main(int argc, char **argv)
{
	void *live, *dead;
	void *go;

	if ((live = dlopen("./livelib.so", RTLD_LAZY | RTLD_LOCAL)) == NULL) {
		printf("dlopen of livelib.so failed: %s\n", dlerror());
		return (1);
	}

	(void) dlclose(live);

	if ((dead = dlopen("./deadlib.so", RTLD_LAZY | RTLD_LOCAL)) == NULL) {
		printf("dlopen of deadlib.so failed: %s\n", dlerror());
		return (1);
	}

	if ((live = dlopen("./livelib.so", RTLD_LAZY | RTLD_LOCAL)) == NULL) {
		printf("dlopen of livelib.so failed: %s\n", dlerror());
		return (1);
	}

	if ((go = dlsym(live, "go")) == NULL) {
		printf("failed to lookup 'go' in livelib.so\n");
		return (1);
	}

	((void (*)(void))go)();

	return (0);
}
EOF

make > /dev/null
if [ $? -ne 0 ]; then
	echo "failed to build" >& 2
	exit 1
fi

script() {
	$dtrace -w -c ./main -Zqs /dev/stdin <<EOF
	test_prov*:::
	{
		printf("%s:%s:%s\n", probemod, probefunc, probename);
	}
EOF
}

script
status=$?

exit $status
