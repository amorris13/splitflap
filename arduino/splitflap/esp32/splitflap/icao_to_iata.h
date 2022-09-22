#pragma once

#include <string>
#include <map>

static std::map<std::string, std::string> airlinesMap = {
    {"QFA", "QF"},
    {"JST", "JQ"},
    {"ANZ", "NZ"}};

static std::string icaoToIataFlight(std::string icaoFlight)
{
    std::string icaoAirline = icaoFlight.substr(0, 3);

    auto iataAirline = airlinesMap.find(icaoAirline);
    if (iataAirline != airlinesMap.end())
    {
        return icaoFlight.replace(0, 3, iataAirline->second);
    }
    else
    {
        return icaoFlight;
    }
}
