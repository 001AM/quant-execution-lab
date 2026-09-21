#include "quant_engine/matching_engine.hpp"

#include "test_support.hpp"

namespace {
using namespace quant_engine;

SubmitResult limit(MatchingEngine& engine, const OrderId id, const Side side,
                   const Price price, const Quantity quantity) {
    return engine.submit(OrderRequest{id, side, OrderType::Limit, quantity, price});
}

void rest(MatchingEngine& engine, const OrderId id, const Side side,
          const Price price, const Quantity quantity) {
    CHECK(limit(engine, id, side, price, quantity).accepted);
}

void quantity_decrease_keeps_sequence_and_fifo_priority() {
    MatchingEngine engine;
    rest(engine, 1, Side::Buy, 100, 100);
    rest(engine, 2, Side::Buy, 100, 100);
    rest(engine, 3, Side::Buy, 100, 100);
    const auto before = engine.get_order(1);
    const auto modified = engine.modify(1, ModifyRequest{std::nullopt, 50});
    CHECK(modified.modified);
    CHECK(modified.priority_preserved);
    CHECK(modified.report->sequence == before->sequence);
    CHECK(modified.report->remaining_quantity == 50);
    const auto trade = limit(engine, 10, Side::Sell, 100, 50);
    CHECK(trade.trades[0].buy_order_id == 1);
}

void quantity_increase_loses_fifo_priority() {
    MatchingEngine engine;
    rest(engine, 1, Side::Buy, 100, 100);
    rest(engine, 2, Side::Buy, 100, 100);
    rest(engine, 3, Side::Buy, 100, 100);
    const auto modified = engine.modify(1, ModifyRequest{std::nullopt, 200});
    CHECK(modified.modified);
    CHECK(!modified.priority_preserved);
    CHECK(modified.report->sequence == 4);
    const auto result = limit(engine, 10, Side::Sell, 100, 250);
    CHECK(result.trades.size() == 3);
    CHECK(result.trades[0].buy_order_id == 2);
    CHECK(result.trades[1].buy_order_id == 3);
    CHECK(result.trades[2].buy_order_id == 1);
}

void price_change_loses_priority_and_joins_new_level_tail() {
    MatchingEngine engine;
    rest(engine, 1, Side::Buy, 100, 100); // A
    rest(engine, 2, Side::Buy, 100, 100); // B
    rest(engine, 3, Side::Buy, 99, 100);  // C
    const auto modified = engine.modify(1, ModifyRequest{99, std::nullopt});
    CHECK(modified.modified);
    CHECK(!modified.priority_preserved);
    CHECK(modified.report->sequence == 4);
    CHECK(engine.cancel(2).cancelled);
    const auto result = limit(engine, 10, Side::Sell, 99, 150);
    CHECK(result.trades[0].buy_order_id == 3);
    CHECK(result.trades[1].buy_order_id == 1);
}

void priority_rule_sequence_numbers_are_monotonic() {
    MatchingEngine engine;
    rest(engine, 1, Side::Buy, 100, 100); // A seq 1
    rest(engine, 2, Side::Buy, 100, 100); // B seq 2
    rest(engine, 3, Side::Buy, 100, 100); // C seq 3
    const auto a = engine.modify(1, ModifyRequest{std::nullopt, 50});
    const auto b = engine.modify(2, ModifyRequest{std::nullopt, 200});
    const auto c = engine.modify(3, ModifyRequest{99, std::nullopt});
    CHECK(a.report->sequence == 1);
    CHECK(b.report->sequence == 4);
    CHECK(c.report->sequence == 5);
    CHECK(engine.book().snapshot().bids ==
          std::vector<LevelSnapshot>({{100, 250, 2}, {99, 100, 1}}));
}

void price_improvement_can_trigger_immediate_matching() {
    MatchingEngine engine;
    rest(engine, 1, Side::Buy, 100, 100);
    rest(engine, 2, Side::Sell, 102, 100);
    const auto modified = engine.modify(1, ModifyRequest{103, std::nullopt});
    CHECK(modified.modified);
    CHECK(modified.trades.size() == 1);
    CHECK(modified.trades[0].price == 102);
    CHECK(modified.trades[0].quantity == 100);
    CHECK(modified.report->status == OrderStatus::Filled);
    CHECK(engine.get_order(2)->status == OrderStatus::Filled);
    CHECK(engine.book().active_order_count() == 0);
}

void partially_filled_total_quantity_can_be_reduced() {
    MatchingEngine engine;
    rest(engine, 1, Side::Sell, 100, 40);
    const auto submitted = limit(engine, 2, Side::Buy, 100, 100);
    CHECK(submitted.report.executed_quantity == 40);
    const auto modified = engine.modify(2, ModifyRequest{std::nullopt, 70});
    CHECK(modified.modified);
    CHECK(modified.priority_preserved);
    CHECK(modified.report->original_quantity == 70);
    CHECK(modified.report->executed_quantity == 40);
    CHECK(modified.report->remaining_quantity == 30);
}

void quantity_below_executed_is_rejected_atomically() {
    MatchingEngine engine;
    rest(engine, 1, Side::Sell, 100, 60);
    static_cast<void>(limit(engine, 2, Side::Buy, 100, 100));
    const auto before = engine.get_order(2);
    const auto snapshot = engine.book().snapshot();
    const auto modified = engine.modify(2, ModifyRequest{std::nullopt, 50});
    CHECK(!modified.modified);
    CHECK(modified.reject_reason == RejectReason::QuantityBelowExecuted);
    CHECK(engine.get_order(2)->original_quantity == before->original_quantity);
    CHECK(engine.get_order(2)->remaining_quantity == before->remaining_quantity);
    CHECK(engine.book().snapshot() == snapshot);
}

void modifying_total_to_executed_cancels_remainder() {
    MatchingEngine engine;
    rest(engine, 1, Side::Sell, 100, 60);
    static_cast<void>(limit(engine, 2, Side::Buy, 100, 100));
    const auto modified = engine.modify(2, ModifyRequest{std::nullopt, 60});
    CHECK(modified.modified);
    CHECK(modified.priority_preserved);
    CHECK(modified.report->status == OrderStatus::Cancelled);
    CHECK(modified.report->executed_quantity == 60);
    CHECK(modified.report->remaining_quantity == 0);
    CHECK(engine.book().find_active_order(2) == nullptr);
}

void unknown_order_modification_is_rejected() {
    MatchingEngine engine;
    const auto result = engine.modify(999, ModifyRequest{100, 10});
    CHECK(!result.modified);
    CHECK(result.reject_reason == RejectReason::OrderNotFound);
}

void filled_order_modification_is_rejected() {
    MatchingEngine engine;
    rest(engine, 1, Side::Sell, 100, 10);
    static_cast<void>(limit(engine, 2, Side::Buy, 100, 10));
    const auto result = engine.modify(2, ModifyRequest{101, std::nullopt});
    CHECK(!result.modified);
    CHECK(result.reject_reason == RejectReason::OrderNotActive);
}

void cancelled_order_modification_is_rejected() {
    MatchingEngine engine;
    rest(engine, 1, Side::Buy, 100, 10);
    CHECK(engine.cancel(1).cancelled);
    const auto result = engine.modify(1, ModifyRequest{101, std::nullopt});
    CHECK(!result.modified);
    CHECK(result.reject_reason == RejectReason::OrderNotActive);
}

void meaningless_and_invalid_modifications_are_atomic() {
    MatchingEngine engine;
    rest(engine, 1, Side::Buy, 100, 10);
    const auto before = engine.get_order(1);
    const auto empty = engine.modify(1, ModifyRequest{});
    const auto same = engine.modify(1, ModifyRequest{100, 10});
    const auto invalid_price = engine.modify(1, ModifyRequest{0, std::nullopt});
    CHECK(empty.reject_reason == RejectReason::InvalidModification);
    CHECK(same.reject_reason == RejectReason::InvalidModification);
    CHECK(invalid_price.reject_reason == RejectReason::InvalidPrice);
    CHECK(engine.get_order(1)->price == before->price);
    CHECK(engine.get_order(1)->sequence == before->sequence);
    CHECK(engine.book().validate_invariants());
}

}  // namespace

int main() {
    quantity_decrease_keeps_sequence_and_fifo_priority();
    quantity_increase_loses_fifo_priority();
    price_change_loses_priority_and_joins_new_level_tail();
    priority_rule_sequence_numbers_are_monotonic();
    price_improvement_can_trigger_immediate_matching();
    partially_filled_total_quantity_can_be_reduced();
    quantity_below_executed_is_rejected_atomically();
    modifying_total_to_executed_cancels_remainder();
    unknown_order_modification_is_rejected();
    filled_order_modification_is_rejected();
    cancelled_order_modification_is_rejected();
    meaningless_and_invalid_modifications_are_atomic();
    return test_support::failures == 0 ? 0 : 1;
}
