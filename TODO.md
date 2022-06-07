adjust relative humidity when using a temperature offset!

tAdjusted = tRaw + tOffset;
humAdjusted = calculateRelHumidity(tAdjusted, calculateAbsHumidity(tRaw, humRaw)) + humOffset;

```
// rel to abs
// https://carnotcycle.wordpress.com/2012/08/04/how-to-convert-relative-humidity-to-absolute-humidity/
// abs to rel
// https://www.easycalculation.com/weather/learn-relative-humidity-from-absolute.php

/**
 * calculates absolute humidity from temperature and relative humidity
 */
function calculateAbsHumidity(t, rh) {
    return 6.112 * Math.pow(Math.E,(17.67 * t)/(t + 243.5)) * rh * 2.1674;
}

/**
 * calculates relative humidity from temperature and absolute humidity
 */
function calculateRelHumidity(t, ah) {
    const ps = ( 0.61078 * 7.501 ) * Math.pow(Math.E, ((17.2694*t) / (238.3+t)));
    const mv = (18 / (0.0623665 * (273.15 + t))) * ps;
    return (ah / mv) * 100;
}
```
