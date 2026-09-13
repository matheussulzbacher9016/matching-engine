#include "check.hpp"
#include "matching/engine.hpp"
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
void matching_tests() {
    Engine e;
    const auto b = e.limit(Side::Buy, 1000, 100).id;
    const auto a1 = e.limit(Side::Sell, 2000, 100).id;
    const auto a2 = e.limit(Side::Sell, 2000, 200).id;
    auto r = e.market(Side::Buy, 150);
    CHECK(r.trades.size() == 2);
    CHECK(r.trades[0].sell_id == a1 && r.trades[0].qty == 100);
    CHECK(r.trades[1].sell_id == a2 && r.trades[1].qty == 50);
    CHECK(!e.find(a1) && e.find(a2)->remaining == 150);
    r = e.market(Side::Buy, 200);
    CHECK(r.trades.size() == 1 && r.trades[0].qty == 150 && r.unfilled == 50);
    r = e.market(Side::Sell, 200);
    CHECK(r.trades[0].buy_id == b && r.unfilled == 100);
    CHECK(e.snapshot(Side::Buy).empty() && e.snapshot(Side::Sell).empty());
    CHECK(e.market(Side::Buy, 10).unfilled == 10);
    Engine levels;
    const auto expensive = levels.limit(Side::Sell, 1100, 7).id;
    const auto cheap = levels.limit(Side::Sell, 1000, 3).id;
    r = levels.limit(Side::Buy, 1050, 5);
    CHECK(r.trades.size() == 1 && r.trades[0].sell_id == cheap && r.trades[0].price == 1000);
    CHECK(levels.find(r.id)->remaining == 2 && levels.find(expensive)->remaining == 7);
    Engine sell;
    const auto high = sell.limit(Side::Buy, 1200, 4).id;
    sell.limit(Side::Buy, 1100, 4);
    r = sell.limit(Side::Sell, 1000, 7);
    CHECK(r.trades.size() == 2 && r.trades[0].buy_id == high && r.trades[1].price == 1100);
    rejects([&] { (void)sell.limit(Side::Buy, 0, 1); });
    rejects([&] { (void)sell.market(Side::Buy, -1); });
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

void extra_matching_tests() {
    for (Side side : {Side::Buy, Side::Sell}) {
        Engine engine;
        const Side opposite = side == Side::Buy ? Side::Sell : Side::Buy;
        const auto first = engine.limit(side, 1000, 2).id;
        const auto second = engine.limit(side, 1000, 3).id;
        const auto result = engine.market(opposite, 4);
        CHECK(result.trades.size() == 2);
        CHECK(result.trades[0].qty == 2 && result.trades[1].qty == 2);
        CHECK((side == Side::Buy ? result.trades[0].buy_id : result.trades[0].sell_id) == first);
        CHECK((side == Side::Buy ? result.trades[1].buy_id : result.trades[1].sell_id) == second);
        CHECK(!engine.find(first));
        CHECK(engine.find(second).has_value());
        CHECK(engine.find(second)->remaining == 1);

        auto copy = engine.snapshot(side);
        copy[0].remaining = 99;
        CHECK(engine.find(second)->remaining == 1);
    }

    Engine invalid;
    rejects([&] { (void)invalid.limit(static_cast<Side>(2), 100, 1); });
    rejects([&] { (void)invalid.market(Side::Buy, 0); });
    rejects([&] { (void)invalid.limit(Side::Sell, 100, -1); });
    CHECK(invalid.limit(Side::Buy, 100, 1).id == 1);

    Engine levels;
    levels.limit(Side::Sell, 1100, 3);
    levels.limit(Side::Sell, 1000, 2);
    levels.limit(Side::Sell, 1200, 5);
    const auto swept = levels.market(Side::Buy, 7);
    CHECK(swept.trades.size() == 3);
    CHECK(swept.trades[0].price == 1000 && swept.trades[0].qty == 2);
    CHECK(swept.trades[1].price == 1100 && swept.trades[1].qty == 3);
    CHECK(swept.trades[2].price == 1200 && swept.trades[2].qty == 2);
    CHECK(swept.unfilled == 0);
}

int main() {
    try {
        numeric_tests();
        extra_numeric_tests();
        matching_tests();
        extra_matching_tests();
        std::cout << "All tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}