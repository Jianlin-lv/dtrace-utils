/*
 * Oracle Linux DTrace.
 * Copyright (c) 2006, 2020, Oracle and/or its affiliates. All rights reserved.
 * Licensed under the Universal Permissive License v 1.0 as shown at
 * http://oss.oracle.com/licenses/upl.
 */

/*
 * ASSERTION: sizeof returns the size in bytes of any D expression or data
 * type.
 *
 * SECTION: Structs and Unions/Member Sizes and Offsets
 */
#pragma D option quiet

char new_char;
short new_short;
int new_int;
long new_long;
long long new_long_long;
int8_t new_int8;
int16_t new_int16;
int32_t new_int32;
int64_t new_int64;
intptr_t new_intptr;
uint8_t new_uint8;
uint16_t new_uint16;
uint32_t new_uint32;
uint64_t new_uint64;
uintptr_t new_uintptr;

/*
float new_float;
double new_double;
long double new_long_double;

string new_string;
*/

struct record {
	char ch;
	int in;
} new_struct;

struct {
	char ch;
	int in;
} anon_struct;

union record {
     char ch;
     int in;
} new_union;

union {
     char ch;
     int in;
} anon_union;

enum colors {
	RED,
	GREEN,
	BLUE
} new_enum;


int *pointer;

BEGIN
{
	printf("sizeof (new_char): %d\n", sizeof (new_char));
	printf("sizeof (new_short): %d\n", sizeof (new_short));
	printf("sizeof (new_int): %d\n", sizeof (new_int));
	printf("sizeof (new_long): %d\n", sizeof (new_long));
	printf("sizeof (new_long_long): %d\n", sizeof (new_long_long));
	printf("sizeof (new_int8): %d\n", sizeof (new_int8));
	printf("sizeof (new_int16): %d\n", sizeof (new_int16));
	printf("sizeof (new_int32): %d\n", sizeof (new_int32));
	printf("sizeof (new_int64): %d\n", sizeof (new_int64));
	printf("sizeof (pointer): %d\n", sizeof (pointer));
	printf("sizeof (intptr_t): %d\n", sizeof (intptr_t));
	printf("sizeof (new_struct): %d\n", sizeof (new_struct));
	printf("sizeof (anon_struct): %d\n", sizeof (anon_struct));
	printf("sizeof (new_union): %d\n", sizeof (new_union));
	printf("sizeof (anon_union): %d\n", sizeof (anon_union));
	printf("sizeof (new_enum): %d\n", sizeof (new_enum));
	exit(0);
}

END
/(1 != sizeof (new_char)) || (2 != sizeof (new_short)) ||
    (4 != sizeof (new_int)) ||
    ((4 != sizeof (new_long)) && (8 != sizeof (new_long))) ||
    (8 != sizeof (new_long_long)) ||
    (1 != sizeof (new_int8)) || (2 != sizeof (new_int16)) ||
    (4 != sizeof (new_int32)) || (8 != sizeof (new_int64)) ||
    (sizeof (pointer) != sizeof (new_intptr)) || (8 != sizeof (new_struct)) ||
    (4 != sizeof (new_union)) || (4 != sizeof (new_enum))/
{
	exit(1);
}
