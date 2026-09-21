"""Exceptional risk configuration errors.

Normal order rejection is represented by ``RiskDecision``, not an exception.
"""


class RiskConfigurationError(ValueError):
    """Raised when exposure limits are invalid."""

