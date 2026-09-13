#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace matching {

using Price = std::int64_t; // Cents: never binary floating point.
using Qty = std::int64_t;
using Id = std::uint64_t;
using Sequence = std::uint64_t;

enum class Side { Buy, Sell };
enum class Kind { Limit, PegBid, PegOffer };

inline void validate_side(Side side) {
    if (side != Side::Buy && side != Side::Sell) {
        throw std::invalid_argument("invalid side");
    }
}

inline const char *name(Side side) {
    return side == Side::Buy ? "buy" : "sell";
}

inline void positive(std::int64_t value) {
    if (value <= 0) {
        throw std::invalid_argument("value must be positive");
    }
}

inline std::int64_t digits(std::string_view text) {
    if (text.empty()) {
        throw std::invalid_argument("empty number");
    }
    std::int64_t value = 0;
    for (char character : text) {
        if (character < '0' || character > '9') {
            throw std::invalid_argument("invalid number");
        }
        const int digit = character - '0';
        if (value > (std::numeric_limits<std::int64_t>::max() - digit) / 10) {
            throw std::invalid_argument("number too large");
        }
        value = value * 10 + digit;
    }
    return value;
}

inline Qty parse_qty(std::string_view text) {
    const Qty qty = digits(text);
    positive(qty);
    return qty;
}

inline Price parse_price(std::string_view text) {
    const auto dot = text.find('.');
    const auto whole = digits(text.substr(0, dot));
    std::int64_t fraction = 0;

    if (dot != std::string_view::npos) {
        const auto decimal = text.substr(dot + 1);
        if (decimal.empty() || decimal.size() > 2) {
            throw std::invalid_argument("use one or two decimal places");
        }
        fraction = digits(decimal) * (decimal.size() == 1 ? 10 : 1);
    }
    if (whole > (std::numeric_limits<Price>::max() - fraction) / 100) {
        throw std::invalid_argument("price too large");
    }
    const Price price = whole * 100 + fraction;
    positive(price);
    return price;
}

inline std::string format_price(Price price) {
    positive(price);
    auto text = std::to_string(price / 100);
    const auto cents = price % 100;
    if (cents != 0) {
        text += '.';
        if (cents < 10) {
            text += '0';
        }
        text += std::to_string(cents);
        if (text.back() == '0') {
            text.pop_back();
        }
    }
    return text;
}

struct Order {
    Id id;
    Side side;
    Kind kind;
    std::optional<Price> price; // Empty means suspended peg.
    Qty remaining;
    Sequence sequence;         // FIFO priority.
    Sequence working_sequence; // Activation of the current working price/priority.
};

struct Trade {
    Price price;
    Qty qty;
    Id buy_id;
    Id sell_id;
};

struct Result {
    Id id = 0;        // Markets also receive IDs, but never rest in the book.
    Qty unfilled = 0; // Used only for the unfilled remainder of a market.
    std::vector<Trade> trades;
};

} // namespace matching