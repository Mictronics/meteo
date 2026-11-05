// Self-check for the moving average. Build and run with `make test`.

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "average.h"

#define NEAR(a, b) (fabs((a) - (b)) < 1e-9)

static t_sample sample(double temp, double speed, double dir_deg)
{
    double r = dir_deg * 3.14159265358979323846 / 180.0;
    t_sample s = {temp, 50.0, speed, 0.0, 0.0, speed * sin(r), speed * cos(r)};
    return s;
}

int main(void)
{
    t_average avg = {0};
    t_mean m;

    /* No samples: all zero */
    average_get(&avg, &m);
    assert(m.temperature == 0.0 && m.direction == 0.0);

    /* Partial buffer averages over the samples present, not over 30 */
    t_sample a = sample(10.0, 4.0, 90.0);
    t_sample b = sample(20.0, 4.0, 90.0);
    average_add(&avg, &a);
    average_add(&avg, &b);
    average_get(&avg, &m);
    assert(NEAR(m.temperature, 15.0));
    assert(NEAR(m.humidity, 50.0));
    assert(NEAR(m.windspeed, 4.0));
    assert(NEAR(m.direction, 90.0));

    /* Repeated reads give the same result (no state accumulates) */
    average_get(&avg, &m);
    assert(NEAR(m.temperature, 15.0));

    /* Full buffer slides: after 30 more samples of 0 the old ones are gone */
    t_sample z = sample(0.0, 0.0, 0.0);
    for (int i = 0; i < MOVING_AVG_LENGTH; i++)
        average_add(&avg, &z);
    average_get(&avg, &m);
    assert(NEAR(m.temperature, 0.0));

    /* Direction wraps around north: 359 and 1 average to 0 */
    t_average w = {0};
    t_sample n1 = sample(0.0, 5.0, 359.0);
    t_sample n2 = sample(0.0, 5.0, 1.0);
    average_add(&w, &n1);
    average_add(&w, &n2);
    average_get(&w, &m);
    assert(m.direction < 1e-6 || m.direction > 360.0 - 1e-6);

    /* Direction stays in [0, 360) */
    t_average s = {0};
    t_sample west = sample(0.0, 5.0, 270.0);
    average_add(&s, &west);
    average_get(&s, &m);
    assert(NEAR(m.direction, 270.0));

    puts("average: ok");
    return 0;
}
