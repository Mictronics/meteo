// Part of WebMeteo, a Vaisalla weather data visualization.
//
// Based on work by Srikanta Sing
// See https://qnaplus.com/implement-periodic-timer-linux/
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

#ifndef TIME_H
#define TIME_H
#include <stdlib.h>

typedef enum
{
    TIMER_SINGLE_SHOT = 0,
    TIMER_PERIODIC
} t_timer;

typedef void (*time_handler)(size_t timer_id, void *user_data);

int initialize_timer();
size_t start_timer(unsigned int interval, time_handler handler, t_timer type, void *user_data);
void stop_timer(size_t timer_id);
void finalize_timer();

#endif
