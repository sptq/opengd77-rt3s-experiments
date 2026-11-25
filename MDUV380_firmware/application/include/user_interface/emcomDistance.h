/*
 * Pure helpers for the EMCOM channel-distance display.
 */
#ifndef _OPENGD77_EMCOM_DISTANCE_H_
#define _OPENGD77_EMCOM_DISTANCE_H_

#include <stdbool.h>
#include <stdint.h>

bool emcomCoordinateIsValid(double latitude, double longitude);
bool emcomFormatDistanceMeters(double meters, char *buf, uint32_t size);

#endif
