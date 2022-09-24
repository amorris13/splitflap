#pragma once

#include <map>

static std::map<String, String> airlinesMap = {
    {"QFA", "QF"},
    {"JST", "JQ"},
    {"ANZ", "NZ"}};

static String icaoToIataFlight(String icaoFlight)
{
    String icaoAirline = icaoFlight.substring(0, 3);

    auto iataAirline = airlinesMap.find(icaoAirline);
    if (iataAirline != airlinesMap.end())
    {
        icaoFlight.replace(icaoAirline, iataAirline->second);
    }

    return icaoFlight;
}
