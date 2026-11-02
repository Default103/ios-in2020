#include <stdio.h>
#include <stdlib.h>
#include "pm8150_keys_state.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
int main(void)
{
    struct in2020_key_state s = {0};
    CHECK(in2020_key_sample(&s, 1, 0) == -1);
    CHECK(in2020_key_sample(&s, 1, 0) == -1);
    CHECK(in2020_key_sample(&s, 1, 1) == -1);
    CHECK(in2020_key_sample(&s, 1, 0) == -1); /* bounce */
    CHECK(in2020_key_sample(&s, 1, 1) == -1);
    CHECK(in2020_key_sample(&s, 1, 1) == 1);
    for (int i = 0; i < 100; ++i) CHECK(in2020_key_sample(&s, 1, 1) == -1);
    CHECK(in2020_key_sample(&s, 1, 0) == -1);
    CHECK(in2020_key_sample(&s, 1, 0) == 0);
    CHECK(in2020_key_sample(&s, 1, 1) == -1);
    CHECK(in2020_key_sample(&s, 0, 1) == -1); /* failure breaks debounce */
    CHECK(in2020_key_sample(&s, 1, 1) == -1);
    CHECK(in2020_key_sample(&s, 1, 1) == 1);
    for (int i = 0; i < 4; ++i) CHECK(in2020_key_sample(&s, 0, 0) == -1);
    CHECK(in2020_key_sample(&s, 0, 0) == 0);
    for (int i = 0; i < 100; ++i) CHECK(in2020_key_sample(&s, 0, 0) == -1);
    CHECK(in2020_key_sample(&s, 1, 1) == -1);
    CHECK(in2020_key_sample(&s, 1, 1) == 1);
    puts("PM8150 debounce / held / release / read-failure fixtures PASS");
    return 0;
}
