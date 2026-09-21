#include "quant_engine/matching_engine.hpp"

#include "test_support.hpp"

#include <cmath>

namespace {
using namespace quant_engine;

SubmitResult limit(MatchingEngine& engine, const OrderId id, const Side side,
                   const Price price, const Quantity quantity) {
    return engine.submit(OrderRequest{id, side, OrderType::Limit, quantity, price});
}

SubmitResult market(MatchingEngine& engine, const OrderId id, const Side side,
                    const Quantity quantity) {
    return engine.submit(OrderRequest{id, side, OrderType::Market, quantity, std::nullopt});
}

void rest(MatchingEngine& engine, const OrderId id, const Side side,
          const Price price, const Quantity quantity) {
    const auto result = limit(engine, id, side, price, quantity);
    CHECK(result.accepted);
    CHECK(result.trades.empty());
}

void market_buy_consumes_one_level() {
    MatchingEngine engine;
    rest(engine, 1, Side::Sell, 100, 50);
    const auto result = market(engine, 2, Side::Buy, 40);
    CHECK(result.accepted);
    CHECK(result.trades.size() == 1);
    CHECK(result.trades[0].price == 100 && result.trades[0].quantity == 40);
    CHECK(result.report.status == OrderStatus::Filled);
    CHECK(engine.book().snapshot().asks[0].total_quantity == 10);
}

void market_sell_consumes_one_level() {
    MatchingEngine engine;
    rest(engine, 1, Side::Buy, 100, 50);
    const auto result = market(engine, 2, Side::Sell, 40);
    CHECK(result.trades.size() == 1);
    CHECK(result.trades[0].price == 100 && result.trades[0].quantity == 40);
    CHECK(result.report.status == OrderStatus::Filled);
}

void full_market_buy_walks_price_then_fifo() {
    MatchingEngine engine;
    rest(engine, 1, Side::Sell, 100, 30);
    rest(engine, 2, Side::Sell, 100, 40);
    rest(engine, 3, Side::Sell, 101, 50);
    rest(engine, 4, Side::Sell, 102, 100);

    const auto result = market(engine, 10, Side::Buy, 150);

    CHECK(result.trades.size() == 4);
    CHECK((result.trades[0] == Trade{1, 10, 1, 100, 30, 1}));
    CHECK((result.trades[1] == Trade{2, 10, 2, 100, 40, 2}));
    CHECK((result.trades[2] == Trade{3, 10, 3, 101, 50, 3}));
    CHECK((result.trades[3] == Trade{4, 10, 4, 102, 30, 4}));
    CHECK(result.report.status == OrderStatus::Filled);
    CHECK(result.report.executed_quantity == 150);
    CHECK(engine.book().snapshot().asks ==
          std::vector<LevelSnapshot>({{102, 70, 1}}));
}

void market_sell_walks_multiple_levels() {
    MatchingEngine engine;
    rest(engine, 1, Side::Buy, 100, 50);
    rest(engine, 2, Side::Buy, 99, 100);
    rest(engine, 3, Side::Buy, 98, 200);
    const auto result = market(engine, 10, Side::Sell, 120);
    CHECK(result.trades.size() == 2);
    CHECK(result.trades[0].price == 100 && result.trades[0].quantity == 50);
    CHECK(result.trades[1].price == 99 && result.trades[1].quantity == 70);
    CHECK((engine.book().snapshot().bids[0] == LevelSnapshot{99, 30, 1}));
}

void insufficient_market_liquidity_cancels_remainder() {
    MatchingEngine engine;
    rest(engine, 1, Side::Sell, 100, 30);
    rest(engine, 2, Side::Sell, 101, 20);
    const auto result = market(engine, 10, Side::Buy, 100);
    CHECK(result.trades.size() == 2);
    CHECK(result.report.status == OrderStatus::Cancelled);
    CHECK(result.report.executed_quantity == 50);
    CHECK(result.report.remaining_quantity == 50);
    CHECK(engine.book().active_order_count() == 0);
    CHECK(engine.book().snapshot().asks.empty());
}

void empty_book_market_order_is_cancelled_and_never_rests() {
    MatchingEngine engine;
    const auto result = market(engine, 1, Side::Buy, 100);
    CHECK(result.accepted);
    CHECK(result.trades.empty());
    CHECK(result.report.status == OrderStatus::Cancelled);
    CHECK(result.report.executed_quantity == 0);
    CHECK(result.report.remaining_quantity == 100);
    CHECK(engine.book().active_order_count() == 0);
    CHECK(engine.book().snapshot().bids.empty());
}

void market_order_average_price_is_quantity_weighted() {
    MatchingEngine engine;
    rest(engine, 1, Side::Sell, 100, 30);
    rest(engine, 2, Side::Sell, 101, 40);
    rest(engine, 3, Side::Sell, 102, 30);
    const auto result = market(engine, 10, Side::Buy, 100);
    CHECK(result.report.average_execution_price.has_value());
    CHECK(std::abs(*result.report.average_execution_price - 101.0) < 1e-12);
    CHECK(result.report.last_execution_price == 102);
    CHECK(result.report.last_execution_quantity == 30);
}

void limit_buy_walks_only_protected_prices_then_rests() {
    MatchingEngine engine;
    rest(engine, 1, Side::Sell, 100, 50);
    rest(engine, 2, Side::Sell, 101, 50);
    rest(engine, 3, Side::Sell, 102, 50);
    const auto result = limit(engine, 10, Side::Buy, 101, 130);
    CHECK(result.trades.size() == 2);
    CHECK(result.trades[0].price == 100 && result.trades[0].quantity == 50);
    CHECK(result.trades[1].price == 101 && result.trades[1].quantity == 50);
    CHECK(result.report.status == OrderStatus::PartiallyFilled);
    CHECK(result.report.remaining_quantity == 30);
    CHECK(engine.book().best_bid() == 101);
    CHECK(engine.book().best_ask() == 102);
}

void limit_sell_walks_only_protected_prices_then_rests() {
    MatchingEngine engine;
    rest(engine, 1, Side::Buy, 105, 50);
    rest(engine, 2, Side::Buy, 104, 50);
    rest(engine, 3, Side::Buy, 103, 50);
    const auto result = limit(engine, 10, Side::Sell, 104, 120);
    CHECK(result.trades.size() == 2);
    CHECK(result.trades[0].price == 105);
    CHECK(result.trades[1].price == 104);
    CHECK(result.report.remaining_quantity == 20);
    CHECK(engine.book().best_bid() == 103);
    CHECK(engine.book().best_ask() == 104);
}

void resting_price_sets_market_execution_price() {
    MatchingEngine engine;
    rest(engine, 1, Side::Sell, 95, 10);
    const auto result = market(engine, 2, Side::Buy, 10);
    CHECK(result.trades[0].price == 95);
}

}  // namespace

int main() {
    market_buy_consumes_one_level();
    market_sell_consumes_one_level();
    full_market_buy_walks_price_then_fifo();
    market_sell_walks_multiple_levels();
    insufficient_market_liquidity_cancels_remainder();
    empty_book_market_order_is_cancelled_and_never_rests();
    market_order_average_price_is_quantity_weighted();
    limit_buy_walks_only_protected_prices_then_rests();
    limit_sell_walks_only_protected_prices_then_rests();
    resting_price_sets_market_execution_price();
    return test_support::failures == 0 ? 0 : 1;
}
