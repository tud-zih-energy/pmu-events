#include <pmu-events/_impl/pmu-events.h>
#include <pmu-events/pmu-events.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * catch2 for poor people
 */
#define TEST_CASE(name) test_name = name;

#define REQUIRE(term)                                                                              \
    if (!(term))                                                                                   \
    {                                                                                              \
        fprintf(stderr, "Test failed: %s\n", test_name);                                           \
        fprintf(stderr, "Failing expression: %s\n", #term);                                        \
        return -1;                                                                                 \
    }

int main(void)
{
    char* test_name;
    TEST_CASE("parse_range works for single digits");
    {
        struct range slice;
        REQUIRE(parse_range("9", &slice) != -1);

        REQUIRE((slice.start == 9) && (slice.end == 9));
    }

    TEST_CASE("parse_range works for ranges");
    {
        struct range slice;
        REQUIRE(parse_range("9-23", &slice) != -1);

        REQUIRE((slice.start == 9) && (slice.end == 23));
    }

    TEST_CASE("parse_range fails for garbage");
    {
        struct range slice;
        REQUIRE(parse_range("dasfklahsgkj", &slice) == -1);
    }

    TEST_CASE("parse_range fails for extraneous characters");
    {
        struct range slice;
        REQUIRE(parse_range("9-23hg", &slice) == -1);
    }

    TEST_CASE("parse_range fails for unterminated range");
    {
        struct range slice;
        REQUIRE(parse_range("9-", &slice) == -1);
    }

    TEST_CASE("parse_range fails for empty string");
    {
        struct range slice;
        REQUIRE(parse_range("", &slice) == -1);
    }

    TEST_CASE("parse_range_list works for single range");
    {
        struct range_list list;
        REQUIRE(parse_range_list("1", &list) != -1);
        free_range_list(&list);
    }

    TEST_CASE("parse_range_list works for multiple ranges");
    {
        struct range_list list;
        REQUIRE(parse_range_list("1,7-9", &list) != -1);
        free_range_list(&list);
    }

    TEST_CASE("parse_range_list fails for extra commas");
    {
        struct range_list list;
        REQUIRE(parse_range_list("1,7-9,", &list) == -1);
        REQUIRE(parse_range_list("1,,7-9", &list) == -1);
    }
    TEST_CASE("parse_config_def fails for unsupported attr field");
    {
        struct config_def def;
        REQUIRE(parse_config_def("config3:1,7-9", &def) == -1);
    }

    TEST_CASE("apply_range_list_to_val works");
    {
        struct range_list list;
        REQUIRE(parse_range_list("1,3,5,7,9", &list) != -1);

        unsigned long long val = 0;

        apply_range_list_to_val(&val, UINT64_MAX, &list);

        REQUIRE(val == 0b1010101010);

        free_range_list(&list);

        REQUIRE(parse_range_list("0-3,8-11", &list) != -1);

        val = 0;

        apply_range_list_to_val(&val, UINT64_MAX, &list);

        REQUIRE(val == 0b111100001111);

        free_range_list(&list);
    }

    TEST_CASE("apply_config_def_to_attr works");
    {
        struct config_def def;
        REQUIRE(parse_config_def("config1:1,3,5,7,9", &def) != -1);

        struct perf_event_attr attr;
        memset(&attr, 0, sizeof(attr));
        apply_config_def_to_attr(&attr, UINT64_MAX, &def);

        REQUIRE(attr.config1 == 0b1010101010);

        free_config_def(&def);

        REQUIRE(parse_config_def("config:0-3,8-11", &def) != -1);

        memset(&attr, 0, sizeof(attr));
        apply_config_def_to_attr(&attr, UINT64_MAX, &def);

        REQUIRE(attr.config == 0b111100001111);

        free_config_def(&def);
    }

    TEST_CASE("get_format_file_content works")
    {
        struct pmus pmus;
        get_pmus(&pmus);
        REQUIRE(pmus.num_classes != 0);
        REQUIRE(pmus.classes[0].num_instances != 0);
        char* str = get_format_file_content("event", &pmus.classes[0].instances[0]);
        REQUIRE(str != NULL);

        struct config_def def;
        REQUIRE(parse_config_def(str, &def) != -1);

        free(str);
        free_config_def(&def);

        free_pmus(&pmus);
    }

    TEST_CASE("get_format_file_content fails for fake file")
    {
        struct pmus pmus;
        get_pmus(&pmus);
        REQUIRE(pmus.num_classes != 0);
        REQUIRE(pmus.classes[0].num_instances != 0);

        char* str = get_format_file_content("foobarfoobar", &pmus.classes[0].instances[0]);
        REQUIRE(str == NULL);

        free_pmus(&pmus);
    }

    TEST_CASE("read_perf_type works")
    {
        struct pmus pmus;
        get_pmus(&pmus);
        REQUIRE(pmus.num_classes != 0);
        REQUIRE(pmus.classes[0].num_instances != 0);

        REQUIRE(read_perf_type(&pmus.classes[0].instances[0]) != -1);

        free_pmus(&pmus);
    }
}
