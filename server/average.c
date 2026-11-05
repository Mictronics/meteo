// Part of WebMeteo, a Vaisalla weather data visualization.
//
// Copyright (c) 2021 Michael Wolf <michael@mictronics.de>
//
// This file is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// any later version.
//
// This file is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include <math.h>
#include <string.h>
#include "average.h"

#define RAD_TO_DEG (180.0 / 3.14159265358979323846)

void average_add(t_average *avg, const t_sample *sample)
{
    avg->buf[avg->next] = *sample;
    avg->next = (avg->next + 1) % MOVING_AVG_LENGTH;
    if (avg->count < MOVING_AVG_LENGTH)
        avg->count++;
}

void average_get(const t_average *avg, t_mean *mean)
{
    t_sample sum = {0};

    memset(mean, 0, sizeof(*mean));
    if (avg->count == 0)
        return;

    for (unsigned int i = 0; i < avg->count; i++)
    {
        const t_sample *s = &avg->buf[i];
        sum.temperature += s->temperature;
        sum.humidity += s->humidity;
        sum.windspeed += s->windspeed;
        sum.cross_wind += s->cross_wind;
        sum.head_wind += s->head_wind;
        sum.wind_comp1 += s->wind_comp1;
        sum.wind_comp2 += s->wind_comp2;
    }

    mean->temperature = sum.temperature / avg->count;
    mean->humidity = sum.humidity / avg->count;
    mean->windspeed = sum.windspeed / avg->count;
    mean->cross_wind = sum.cross_wind / avg->count;
    mean->head_wind = sum.head_wind / avg->count;

    mean->direction = atan2(sum.wind_comp1, sum.wind_comp2) * RAD_TO_DEG;
    if (mean->direction < 0.0)
        mean->direction += 360.0;
}
