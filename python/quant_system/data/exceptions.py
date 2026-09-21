"""Market-data exception hierarchy."""


class MarketDataError(Exception):
    """Base class for errors raised by the market-data layer."""


class MarketDataSchemaError(MarketDataError):
    """Raised when input cannot be mapped to the canonical schema."""


class MarketDataValidationError(MarketDataError):
    """Raised when data violates a financial or data-quality invariant."""

