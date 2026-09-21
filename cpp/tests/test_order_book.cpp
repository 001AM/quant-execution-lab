#include "quant_engine/order_book.hpp"

#include "test_support.hpp"

#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace quant_engine;

void rest(OrderBook& book, const OrderId id, const Side side, const Price price,
          const Quantity quantity) {
    const auto trades = book.add_limit_order(id, side, price, quantity);
    CHECK(trades.empty());
}

void empty_book_has_no_quotes() {
    const OrderBook book;
    CHECK(!book.best_bid().has_value());
    CHECK(!book.best_ask().has_value());
    CHECK(!book.spread().has_value());
    CHECK(!book.mid_price().has_value());
    CHECK(book.active_order_count() == 0);
    CHECK(!book.is_crossed());
}

void add_bid_sets_best_bid() {
    OrderBook book;
    rest(book, 1, Side::Buy, 100, 25);
    CHECK(book.best_bid() == 100);
    CHECK(book.find_active_order(1) != nullptr);
}

void add_ask_sets_best_ask() {
    OrderBook book;
    rest(book, 1, Side::Sell, 105, 25);
    CHECK(book.best_ask() == 105);
    CHECK(book.find_active_order(1) != nullptr);
}

void multiple_prices_choose_correct_best_quotes() {
    OrderBook book;
    rest(book, 1, Side::Buy, 99, 10);
    rest(book, 2, Side::Buy, 101, 10);
    rest(book, 3, Side::Buy, 100, 10);
    rest(book, 4, Side::Sell, 105, 10);
    rest(book, 5, Side::Sell, 103, 10);
    rest(book, 6, Side::Sell, 104, 10);
    CHECK(book.best_bid() == 101);
    CHECK(book.best_ask() == 103);
}

void spread_and_half_tick_mid_are_reported() {
    OrderBook book;
    rest(book, 1, Side::Buy, 100, 10);
    rest(book, 2, Side::Sell, 103, 10);
    CHECK(book.spread() == 3);
    CHECK(book.mid_price() == 101.5);
}

void duplicate_active_id_is_rejected() {
    OrderBook book;
    rest(book, 1, Side::Buy, 100, 10);
    EXPECT_THROW(book.add_limit_order(1, Side::Sell, 105, 10),
                 std::invalid_argument);
    CHECK(book.active_order_count() == 1);
}

void non_crossing_limits_both_rest() {
    OrderBook book;
    rest(book, 1, Side::Sell, 105, 100);
    const auto trades = book.add_limit_order(2, Side::Buy, 100, 100);
    CHECK(trades.empty());
    CHECK(book.best_bid() == 100);
    CHECK(book.best_ask() == 105);
    CHECK(book.active_order_count() == 2);
}

void exact_limit_match_fills_both_orders() {
    OrderBook book;
    rest(book, 1, Side::Sell, 100, 100);
    const auto trades = book.add_limit_order(2, Side::Buy, 100, 100);
    CHECK(trades.size() == 1);
    CHECK(trades[0].price == 100);
    CHECK(trades[0].quantity == 100);
    CHECK(book.find_order(1)->status() == OrderStatus::Filled);
    CHECK(book.find_order(2)->status() == OrderStatus::Filled);
    CHECK(book.active_order_count() == 0);
}

void resting_order_sets_better_execution_price() {
    OrderBook book;
    rest(book, 1, Side::Sell, 95, 100);
    const auto trades = book.add_limit_order(2, Side::Buy, 100, 100);
    CHECK(trades.size() == 1);
    CHECK(trades[0].price == 95);
}

void partial_fill_remainder_rests() {
    OrderBook book;
    rest(book, 1, Side::Sell, 100, 40);
    const auto trades = book.add_limit_order(2, Side::Buy, 100, 100);
    CHECK(trades.size() == 1);
    const Order* incoming = book.find_active_order(2);
    CHECK(incoming != nullptr);
    CHECK(incoming->remaining_quantity() == 60);
    CHECK(incoming->status() == OrderStatus::PartiallyFilled);
    CHECK(book.best_bid() == 100);
    CHECK(!book.best_ask().has_value());
}

void fifo_matching_walks_orders_in_arrival_order() {
    OrderBook book;
    rest(book, 1, Side::Sell, 100, 30);
    rest(book, 2, Side::Sell, 100, 40);
    rest(book, 3, Side::Sell, 100, 50);
    const auto trades = book.add_limit_order(10, Side::Buy, 100, 100);
    CHECK(trades.size() == 3);
    CHECK(trades[0].sell_order_id == 1 && trades[0].quantity == 30);
    CHECK(trades[1].sell_order_id == 2 && trades[1].quantity == 40);
    CHECK(trades[2].sell_order_id == 3 && trades[2].quantity == 30);
    CHECK(book.find_active_order(3)->remaining_quantity() == 20);
}

void hand_verifiable_buy_walk_matches_price_then_time() {
    OrderBook book;
    rest(book, 1, Side::Sell, 100, 30);  // A
    rest(book, 2, Side::Sell, 100, 40);  // B
    rest(book, 3, Side::Sell, 101, 50);  // C
    rest(book, 4, Side::Sell, 102, 100); // D

    const auto trades = book.add_limit_order(10, Side::Buy, 102, 100); // X

    CHECK(trades.size() == 3);
    CHECK((trades[0] == Trade{1, 10, 1, 100, 30, 1}));
    CHECK((trades[1] == Trade{2, 10, 2, 100, 40, 2}));
    CHECK((trades[2] == Trade{3, 10, 3, 101, 30, 3}));
    CHECK(book.find_order(10)->status() == OrderStatus::Filled);
    CHECK(book.find_order(1)->status() == OrderStatus::Filled);
    CHECK(book.find_order(2)->status() == OrderStatus::Filled);
    CHECK(book.find_active_order(3)->status() == OrderStatus::PartiallyFilled);
    CHECK(book.find_active_order(3)->remaining_quantity() == 20);
    const auto snapshot = book.snapshot();
    CHECK(snapshot.asks ==
          std::vector<LevelSnapshot>({{101, 20, 1}, {102, 100, 1}}));
}

void hand_verifiable_sell_walk_is_symmetric() {
    OrderBook book;
    rest(book, 1, Side::Buy, 100, 30); // A
    rest(book, 2, Side::Buy, 100, 40); // B
    rest(book, 3, Side::Buy, 99, 50);  // C
    rest(book, 4, Side::Buy, 98, 100); // D

    const auto trades = book.add_limit_order(10, Side::Sell, 98, 100); // X

    CHECK(trades.size() == 3);
    CHECK((trades[0] == Trade{1, 1, 10, 100, 30, 1}));
    CHECK((trades[1] == Trade{2, 2, 10, 100, 40, 2}));
    CHECK((trades[2] == Trade{3, 3, 10, 99, 30, 3}));
    CHECK(book.find_active_order(3)->remaining_quantity() == 20);
    CHECK(book.snapshot().bids ==
          std::vector<LevelSnapshot>({{99, 20, 1}, {98, 100, 1}}));
}

void later_better_price_beats_earlier_time() {
    OrderBook book;
    rest(book, 1, Side::Sell, 101, 100);
    rest(book, 2, Side::Sell, 100, 100);
    const auto trades = book.add_limit_order(3, Side::Buy, 101, 100);
    CHECK(trades.size() == 1);
    CHECK(trades[0].sell_order_id == 2);
    CHECK(trades[0].price == 100);
    CHECK(book.find_active_order(1) != nullptr);
}

void earlier_time_wins_at_same_price() {
    OrderBook book;
    rest(book, 1, Side::Sell, 100, 100);
    rest(book, 2, Side::Sell, 100, 100);
    const auto trades = book.add_limit_order(3, Side::Buy, 100, 150);
    CHECK(trades.size() == 2);
    CHECK(trades[0].sell_order_id == 1 && trades[0].quantity == 100);
    CHECK(trades[1].sell_order_id == 2 && trades[1].quantity == 50);
}

void incoming_remainder_retains_original_sequence() {
    OrderBook book;
    rest(book, 1, Side::Sell, 100, 30);
    static_cast<void>(book.add_limit_order(2, Side::Buy, 100, 100));
    rest(book, 3, Side::Buy, 100, 10);
    CHECK(book.find_active_order(2)->sequence() < book.find_active_order(3)->sequence());
    CHECK(book.snapshot().bids[0].total_quantity == 80);
    CHECK(book.snapshot().bids[0].order_count == 2);
}

void cancel_bid_removes_order_and_level() {
    OrderBook book;
    rest(book, 1, Side::Buy, 100, 10);
    CHECK(book.cancel_order(1));
    CHECK(!book.best_bid().has_value());
    CHECK(book.find_active_order(1) == nullptr);
    CHECK(book.find_order(1)->status() == OrderStatus::Cancelled);
}

void cancel_ask_preserves_other_fifo_orders() {
    OrderBook book;
    rest(book, 1, Side::Sell, 100, 10);
    rest(book, 2, Side::Sell, 100, 20);
    CHECK(book.cancel_order(1));
    CHECK((book.snapshot().asks[0] == LevelSnapshot{100, 20, 1}));
    const auto trades = book.add_limit_order(3, Side::Buy, 100, 20);
    CHECK(trades[0].sell_order_id == 2);
}

void cancel_unknown_returns_false() {
    OrderBook book;
    CHECK(!book.cancel_order(999'999));
}

void cancel_partially_filled_order_preserves_execution_history() {
    OrderBook book;
    rest(book, 1, Side::Sell, 100, 40);
    static_cast<void>(book.add_limit_order(2, Side::Buy, 100, 100));
    CHECK(book.cancel_order(2));
    const Order* cancelled = book.find_order(2);
    CHECK(cancelled->status() == OrderStatus::Cancelled);
    CHECK(cancelled->original_quantity() == 100);
    CHECK(cancelled->filled_quantity() == 40);
    CHECK(cancelled->remaining_quantity() == 60);
}

void filled_orders_leave_active_lookup() {
    OrderBook book;
    rest(book, 1, Side::Sell, 100, 10);
    static_cast<void>(book.add_limit_order(2, Side::Buy, 100, 10));
    CHECK(book.find_active_order(1) == nullptr);
    CHECK(book.find_active_order(2) == nullptr);
    CHECK(book.find_order(1) != nullptr);
    CHECK(book.find_order(2) != nullptr);
}

void filled_price_level_is_removed() {
    OrderBook book;
    rest(book, 1, Side::Sell, 100, 10);
    rest(book, 2, Side::Sell, 101, 10);
    static_cast<void>(book.add_limit_order(3, Side::Buy, 100, 10));
    CHECK(book.best_ask() == 101);
    CHECK(book.snapshot().asks.size() == 1);
}

void snapshot_has_sorted_levels_quantities_and_counts() {
    OrderBook book;
    rest(book, 1, Side::Buy, 100, 10);
    rest(book, 2, Side::Buy, 101, 20);
    rest(book, 3, Side::Buy, 101, 30);
    rest(book, 4, Side::Sell, 103, 40);
    rest(book, 5, Side::Sell, 102, 50);
    const BookSnapshot expected{
        .bids = {{101, 50, 2}, {100, 10, 1}},
        .asks = {{102, 50, 1}, {103, 40, 1}},
    };
    CHECK(book.snapshot() == expected);
}

void matching_never_leaves_crossed_resting_book() {
    OrderBook book;
    rest(book, 1, Side::Sell, 100, 50);
    static_cast<void>(book.add_limit_order(2, Side::Buy, 105, 100));
    CHECK(!book.is_crossed());
    CHECK(book.best_bid() == 105);
    CHECK(!book.best_ask().has_value());
}

void identical_input_sequence_is_deterministic() {
    auto run = []() {
        OrderBook book;
        rest(book, 1, Side::Sell, 100, 30);
        rest(book, 2, Side::Sell, 100, 40);
        rest(book, 3, Side::Sell, 101, 50);
        static_cast<void>(book.add_limit_order(10, Side::Buy, 101, 80));
        return std::pair{book.trades(), book.snapshot()};
    };
    const auto first = run();
    const auto second = run();
    CHECK(first.first == second.first);
    CHECK(first.second == second.second);
}

void invalid_orders_are_rejected_without_book_mutation() {
    OrderBook book;
    EXPECT_THROW(book.add_limit_order(1, Side::Buy, 100, 0),
                 std::invalid_argument);
    EXPECT_THROW(book.add_limit_order(2, Side::Buy, 0, 10),
                 std::invalid_argument);
    EXPECT_THROW(book.add_limit_order(3, static_cast<Side>(99), 100, 10),
                 std::invalid_argument);
    CHECK(book.active_order_count() == 0);
}

void trade_ids_and_sequences_are_monotonic() {
    OrderBook book;
    rest(book, 1, Side::Sell, 100, 10);
    rest(book, 2, Side::Sell, 101, 10);
    const auto trades = book.add_limit_order(3, Side::Buy, 101, 20);
    CHECK(trades[0].trade_id == 1 && trades[0].sequence == 1);
    CHECK(trades[1].trade_id == 2 && trades[1].sequence == 2);
}

void completed_order_ids_cannot_be_reused() {
    OrderBook book;
    rest(book, 1, Side::Buy, 100, 10);
    CHECK(book.cancel_order(1));
    EXPECT_THROW(book.add_limit_order(1, Side::Sell, 101, 10),
                 std::invalid_argument);
}

void matching_stops_beyond_incoming_limit() {
    OrderBook book;
    rest(book, 1, Side::Sell, 100, 10);
    rest(book, 2, Side::Sell, 101, 10);
    const auto trades = book.add_limit_order(3, Side::Buy, 100, 20);
    CHECK(trades.size() == 1);
    CHECK(book.best_ask() == 101);
    CHECK(book.best_bid() == 100);
}

void debugging_display_reflects_book_not_internal_state() {
    OrderBook book;
    rest(book, 1, Side::Buy, 100, 10);
    rest(book, 2, Side::Sell, 102, 20);
    const std::string display = book.to_string();
    CHECK(display.find("ASK") != std::string::npos);
    CHECK(display.find("102 | 20") != std::string::npos);
    CHECK(display.find("100 | 10") != std::string::npos);
}

}  // namespace

int main() {
    empty_book_has_no_quotes();
    add_bid_sets_best_bid();
    add_ask_sets_best_ask();
    multiple_prices_choose_correct_best_quotes();
    spread_and_half_tick_mid_are_reported();
    duplicate_active_id_is_rejected();
    non_crossing_limits_both_rest();
    exact_limit_match_fills_both_orders();
    resting_order_sets_better_execution_price();
    partial_fill_remainder_rests();
    fifo_matching_walks_orders_in_arrival_order();
    hand_verifiable_buy_walk_matches_price_then_time();
    hand_verifiable_sell_walk_is_symmetric();
    later_better_price_beats_earlier_time();
    earlier_time_wins_at_same_price();
    incoming_remainder_retains_original_sequence();
    cancel_bid_removes_order_and_level();
    cancel_ask_preserves_other_fifo_orders();
    cancel_unknown_returns_false();
    cancel_partially_filled_order_preserves_execution_history();
    filled_orders_leave_active_lookup();
    filled_price_level_is_removed();
    snapshot_has_sorted_levels_quantities_and_counts();
    matching_never_leaves_crossed_resting_book();
    identical_input_sequence_is_deterministic();
    invalid_orders_are_rejected_without_book_mutation();
    trade_ids_and_sequences_are_monotonic();
    completed_order_ids_cannot_be_reused();
    matching_stops_beyond_incoming_limit();
    debugging_display_reflects_book_not_internal_state();
    return test_support::failures == 0 ? 0 : 1;
}
