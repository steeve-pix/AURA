"""Stable learning modules and compatibility for archived module commands."""
from pathlib import Path

# Preserve `python -m python.critic_experiment` and similar archived commands
# without keeping duplicate wrapper files in the runtime directory.
_package_directory = Path(__file__).resolve().parent
__path__.extend(str(_package_directory / group)
                for group in ("experiments", "diagnostics", "historical"))
