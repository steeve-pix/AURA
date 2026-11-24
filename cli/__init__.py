"""Python entry points and adapters for the AURA command-line interface."""

# Already-running CLI binaries launch the former cli.nn_session-style names.
# Resolve those names in backend/ while new code uses explicit cli.backend paths.
from pathlib import Path

__path__.append(str(Path(__file__).resolve().parent / "backend"))
