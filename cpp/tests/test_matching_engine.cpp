#include "quant_engine/matching_engine.hpp"

#include "test_support.hpp"

#include <cmath>
#include <cstdint>
#include <utility>

namespace {
using namespace quant_engine;

SubmitResult limit(MatchingEngine& engine, const OrderId id, const Side side,
                   const Price price, const Quantity quantity) {
    return engine.submit(OrderRequest{id, side, OrderType::Limit, quantity, price});
}

void submission_validation_returns_explicit_reasons() {
    MatchingEngine engine;
    CHECK(engine.submit(OrderRequest{1, Side::Buy, OrderType::Limit, 0, 100})
              .reject_reason == RejectReason::InvalidQuantity);
    CHECK(engine.submit(OrderRequest{2, Side::Buy, OrderType::Limit, 10, std::nullopt})
              .reject_reason == RejectReason::MissingLimitPrice);
    CHECK(engine.submit(OrderRequest{3, Side::Buy, OrderType::Market, 10, 100})
              .reject_reason == RejectReason::UnexpectedMarketPrice);
    CHECK(engine.submit(OrderRequest{4, Side::Buy, static_cast<OrderType>(99), 10,
                                     std::nullopt})
              .reject_reason == RejectReason::InvalidOrderType);
    CHECK(engine.submit(OrderRequest{5, static_cast<Side>(99), OrderType::Market, 10,
                                     std::nullopt})
              .reject_reason == RejectReason::InvalidSide);
}

void duplicate_ids_are_rejected_without_overwrite() {
    MatchingEngine engine;
    CHECK(limit(engine, 1, Side::Buy, 100, 10).accepted);
    const auto duplicate = limit(engine, 1, Side::Sell, 101, 20);
    CHECK(!duplicate.accepted);
    CHECK(duplicate.reject_reason == RejectReason::DuplicateOrderId);
    CHECK(engine.get_order(1)->side == Side::Buy);
    CHECK(engine.get_order(1)->original_quantity == 10);
}

void execution_report_contains_fill_quantities_and_last_fill() {
    MatchingEngine engine;
    CHECK(limit(engine, 1, Side::Sell, 100, 30).accepted);
    CHECK(limit(engine, 2, Side::Sell, 102, 70).accepted);
    const auto result =
        engine.submit(OrderRequest{10, Side::Buy, OrderType::Market, 100, std::nullopt});
    CHECK(result.report.original_quantity == 100);
    CHECK(result.report.executed_quantity == 100);
    CHECK(result.report.remaining_quantity == 0);
    CHECK(result.report.last_execution_price == 102);
    CHECK(result.report.last_execution_quantity == 70);
    CHECK(result.report.average_execution_price.has_value());
    CHECK(std::abs(*result.report.average_execution_price - 101.4) < 1e-12);
}

void trade_history_is_append_only_and_ordered() {
    MatchingEngine engine;
    CHECK(limit(engine, 1, Side::Sell, 100, 10).accepted);
    CHECK(limit(engine, 2, Side::Sell, 101, 10).accepted);
    static_cast<void>(engine.submit(
        OrderRequest{3, Side::Buy, OrderType::Market, 20, std::nullopt}));
    CHECK(engine.trades().size() == 2);
    CHECK(engine.trades()[0].trade_id == 1);
    CHECK(engine.trades()[1].trade_id == 2);
    CHECK(engine.trades()[0].price == 100);
    CHECK(engine.trades()[1].price == 101);
}

void historical_lookup_returns_value_views_not_mutable_orders() {
    MatchingEngine engine;
    CHECK(limit(engine, 1, Side::Sell, 100, 10).accepted);
    static_cast<void>(limit(engine, 2, Side::Buy, 100, 10));
    const auto view = engine.get_order(1);
    CHECK(view.has_value());
    CHECK(view->status == OrderStatus::Filled);
    CHECK(view->executed_quantity == 10);
    CHECK(view->remaining_quantity == 0);
    CHECK(engine.book().find_active_order(1) == nullptr);
}

void rejected_order_id_is_terminal_and_cannot_be_reused() {
    MatchingEngine engine;
    const auto rejected =
        engine.submit(OrderRequest{1, Side::Buy, OrderType::Limit, 10, std::nullopt});
    CHECK(!rejected.accepted);
    const auto reused = limit(engine, 1, Side::Buy, 100, 10);
    CHECK(!reused.accepted);
    CHECK(reused.reject_reason == RejectReason::DuplicateOrderId);
    CHECK(engine.get_order(1)->status == OrderStatus::Rejected);
}

struct StressResult {
    std::size_t trade_count;
    BookSnapshot snapshot;
    std::size_t cancels;
    std::size_t modifications;
};

StressResult run_stress_sequence() {
    MatchingEngine engine;
    std::size_t cancels = 0;
    std::size_t modifications = 0;
    for (OrderId id = 1; id <= 10'000; ++id) {
        const Side side = id % 2 == 0 ? Side::Buy : Side::Sell;
        if (id % 10 == 0) {
            const auto result = engine.submit(
                OrderRequest{id, side, OrderType::Market, 5 + id % 17, std::nullopt});
            CHECK(result.accepted);
        } else {
            const Price price = side == Side::Buy
                                    ? static_cast<Price>(99 + id % 6)
                                    : static_cast<Price>(101 + id % 6);
            const auto result = limit(engine, id, side, price, 10 + id % 23);
            CHECK(result.accepted);
        }

        if (id > 20 && id % 17 == 0) {
            const OrderId target = id - 7;
            const auto view = engine.get_order(target);
            if (view.has_value() &&
                (view->status == OrderStatus::Active ||
                 view->status == OrderStatus::PartiallyFilled)) {
                const auto result = engine.cancel(target);
                CHECK(result.cancelled);
                ++cancels;
            }
        }
        if (id > 30 && id % 23 == 0) {
            const OrderId target = id - 11;
            const auto view = engine.get_order(target);
            if (view.has_value() && view->type == OrderType::Limit &&
                (view->status == OrderStatus::Active ||
                 view->status == OrderStatus::PartiallyFilled)) {
                const auto result = engine.modify(
                    target,
                    ModifyRequest{std::nullopt, view->original_quantity + 1});
                CHECK(result.modified);
                ++modifications;
            }
        }
        if (id % 100 == 0) {
            CHECK(engine.book().validate_invariants());
        }
    }
    CHECK(engine.book().validate_invariants());
    CHECK(cancels > 0);
    CHECK(modifications > 0);
    return StressResult{engine.trades().size(), engine.book().snapshot(), cancels,
                        modifications};
}

void ten_thousand_order_sequence_preserves_invariants_and_determinism() {
    const StressResult first = run_stress_sequence();
    const StressResult second = run_stress_sequence();
    CHECK(first.trade_count == second.trade_count);
    CHECK(first.snapshot == second.snapshot);
    CHECK(first.cancels == second.cancels);
    CHECK(first.modifications == second.modifications);
}

}  // namespace

int main() {
    submission_validation_returns_explicit_reasons();
    duplicate_ids_are_rejected_without_overwrite();
    execution_report_contains_fill_quantities_and_last_fill();
    trade_history_is_append_only_and_ordered();
    historical_lookup_returns_value_views_not_mutable_orders();
    rejected_order_id_is_terminal_and_cannot_be_reused();
    ten_thousand_order_sequence_preserves_invariants_and_determinism();
    return test_support::failures == 0 ? 0 : 1;
}
