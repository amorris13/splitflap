/* Simple public domain implementation of the standard CRC32 checksum.
 * Outputs the checksum for each file given as a command line argument.
 * Invalid file names and files that cause errors are silently skipped.
 * The program reads from stdin if it is called with no arguments. */
#pragma once

double great_circle_distance(double latitude1, double longitude1, double latitude2,
                             double longitude2);