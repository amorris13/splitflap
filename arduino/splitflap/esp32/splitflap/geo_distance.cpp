/*
 * Great-circle distance computational forumlas
 *
 * https://en.wikipedia.org/wiki/Great-circle_distance
 */

#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <cmath>
#include "geo_distance.h"

#ifndef M_PI
#define M_PI 3.1415926535897932384626433832795
#endif

using namespace std;

static const double earth_radius_km = 6371.0;

double deg2rad(double deg)
{
    return (deg * M_PI / 180.0);
}

// Returns great circle distance in km.
double great_circle_distance(double latitude1, double longitude1, double latitude2,
                             double longitude2)
{
    double lat1 = deg2rad(latitude1);
    double lon1 = deg2rad(longitude1);
    double lat2 = deg2rad(latitude2);
    double lon2 = deg2rad(longitude2);

    double d_lat = abs(lat1 - lat2);
    double d_lon = abs(lon1 - lon2);

    double a = pow(sin(d_lat / 2), 2) + cos(lat1) * cos(lat2) * pow(sin(d_lon / 2), 2);

    // double d_sigma = 2 * atan2(sqrt(a), sqrt(1 - a));
    double d_sigma = 2 * asin(sqrt(a));

    return earth_radius_km * d_sigma;
}
