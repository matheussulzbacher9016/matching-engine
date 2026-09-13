#include "check.hpp"
#include "matching/types.hpp"
#include <iostream>
using namespace matching;
void numeric_tests() {
    CHECK(parse_price("10") == 1000);
    CHECK(parse_price("9.99") == 999);
    CHECK(parse_price("10.1") == 1010);
    CHECK(format_price(1010) == "10.1");
    CHECK(format_price(1001) == "10.01");
    CHECK(parse_qty("100") == 100);
    CHECK(parse_price("92233720368547758.07") == std::numeric_limits<Price>::max());
    for (const auto *s : {"", "0", "-1", "+1", "NaN", "1.001", "1.", ".1", "1,1", "1e2", "1.2.3",
                          "92233720368547758.08"}) {
        rejects([&] { (void)parse_price(s); });
    }
    rejects([] { (void)parse_qty("1.5"); });
    rejects([] { (void)parse_qty("9223372036854775808"); });
}

void extra_numeric_tests() {
    CHECK(parse_qty("9223372036854775807") == std::numeric_limits<Qty>::max());
    CHECK(parse_price("00010.50") == 1050);
    CHECK(parse_price("0.01") == 1);
    CHECK(format_price(std::numeric_limits<Price>::max()) == "92233720368547758.07");
    for (Price price = 1; price <= 100000; ++price) {
        CHECK(parse_price(format_price(price)) == price);
    }
    for (const auto *text : {"0", "-1", "+1", " 1", "1 ", "", "1e2", "1,5"}) {
        rejects([&] { (void)parse_qty(text); });
    }
    rejects([] { (void)format_price(0); });
    rejects([] { validate_side(static_cast<Side>(9)); });
}

int main() {
    try {
        numeric_tests();
        extra_numeric_tests();
        std::cout << "All tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}