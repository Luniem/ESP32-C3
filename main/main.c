#include <stdio.h>
#include <inttypes.h>
#include "led_strip.h"
#include "portmacro.h"
#include "math.h"

#define SQUARE(x) ((x) * (x))

#define RNG_BASE 0x60026000
#define RNG_DATA_REG_OFFS 0xB0

volatile uint32_t *pRngDataReg = (volatile uint32_t *)(RNG_BASE | RNG_DATA_REG_OFFS);

inline uint32_t nextRand()
{
    return *pRngDataReg;
}

bool equalDistChi2(const uint32_t n[], uint32_t m, uint32_t n0, uint32_t chi2)
{
    uint32_t squaresum = 0;
    for (int i = 0; i < m; i += 1)
    {
        squaresum += SQUARE(n[i] - n0);
    }

    uint32_t x2 = squaresum / n0;
    return (x2 <= chi2);
}

void app_main(void)
{
    uint32_t *n = calloc(6, sizeof(uint32_t));
    if (n == NULL)
    {
        printf("No memory :( \n");
    }
    else
    {

        for (int i = 0; i < 10000000; i++)
        {
            n[(nextRand() & 0xF) % 6] += 1;
        }
    }

    for (int i = 0; i < 6; i += 1)
    {
        printf("%d : %" PRIu32 "\n", i + 1, n[i]);
    }

    bool ok = equalDistChi2(n, 6, 10000000 / 6, 9);

    if (ok)
    {
        printf("Is ok\n");
    }
    else
    {
        printf("Is not ok\n");
    }

    free(n);
    n = NULL;
}