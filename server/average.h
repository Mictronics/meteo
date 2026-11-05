// Part of WebMeteo, a Vaisalla weather data visualization.
//
// Copyright (c) 2026 Michael Wolf <michael@mictronics.de>
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

#ifndef AVERAGE_H
#define AVERAGE_H

#define MOVING_AVG_LENGTH 30 /* Samples, one per second */

typedef struct
{
    double temperature;
    double humidity;
    double windspeed;
    double cross_wind;
    double head_wind;
    double wind_comp1; /* windspeed * sin(direction), for vector mean direction */
    double wind_comp2; /* windspeed * cos(direction) */
} t_sample;

typedef struct
{
    t_sample buf[MOVING_AVG_LENGTH];
    unsigned int count; /* valid samples, saturates at MOVING_AVG_LENGTH */
    unsigned int next;  /* slot written by the next add */
} t_average;

typedef struct
{
    double temperature;
    double humidity;
    double windspeed;
    double cross_wind;
    double head_wind;
    double direction; /* degrees [0, 360) */
} t_mean;

void average_add(t_average *avg, const t_sample *sample);

/* Mean over the samples collected so far; all zero if there are none. */
void average_get(const t_average *avg, t_mean *mean);

#endif /* AVERAGE_H */
