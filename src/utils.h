#ifndef _UTILS_H
#define _UTILS_H

#include <cmath>

/**
 * calculates absolute humidity from temperature and relative humidity
 */
float calculateAbsHumidity(float t, float rh) {
    return (6.112f * expf((17.67f * t)/(t + 243.5f)) * rh * 2.1674f)/(t + 273.15f);
}

/**
 * calculates relative humidity from temperature and absolute humidity
 */
float calculateRelHumidity(float t, float ah) {
    float ps = ( 0.61078f * 7.501f ) * expf((17.2694f*t) / (238.3f+t));
    float mv = (18.0f / (0.0623665f * (273.15f + t))) * ps;
    return (ah / mv) * 100.0f;
}

#endif