#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "user_interface/emcomDistance.h"

static void checkFormat(double meters, const char *expected)
{
	char buf[16];

	assert(emcomFormatDistanceMeters(meters, buf, sizeof(buf)));
	assert(strcmp(buf, expected) == 0);
}

int main(void)
{
	char buf[16];

	assert(emcomCoordinateIsValid(0.0, 0.0));
	assert(emcomCoordinateIsValid(-90.0, -180.0));
	assert(emcomCoordinateIsValid(90.0, 180.0));
	assert(emcomCoordinateIsValid(NAN, 0.0) == false);
	assert(emcomCoordinateIsValid(0.0, INFINITY) == false);
	assert(emcomCoordinateIsValid(-90.001, 0.0) == false);
	assert(emcomCoordinateIsValid(90.001, 0.0) == false);
	assert(emcomCoordinateIsValid(0.0, -180.001) == false);
	assert(emcomCoordinateIsValid(0.0, 180.001) == false);

	checkFormat(0.0, "0m");
	checkFormat(850.0, "850m");
	checkFormat(999.4, "999m");
	checkFormat(999.6, "1.0km");
	checkFormat(1980.0, "2.0km");
	checkFormat(12390.0, "12.4km");
	checkFormat(99980.0, "100km");
	checkFormat(100800.0, "101km");
	assert(emcomFormatDistanceMeters(NAN, buf, sizeof(buf)) == false);
	assert(strcmp(buf, "--") == 0);
	assert(emcomFormatDistanceMeters(INFINITY, buf, sizeof(buf)) == false);
	assert(strcmp(buf, "--") == 0);
	assert(emcomFormatDistanceMeters(-1.0, buf, sizeof(buf)) == false);
	assert(strcmp(buf, "--") == 0);
	assert(emcomFormatDistanceMeters((double)UINT32_MAX, buf, sizeof(buf)) == false);
	assert(strcmp(buf, "--") == 0);

	puts("emcom distance: all passed");
	return 0;
}
