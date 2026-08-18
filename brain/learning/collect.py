import argparse
import subprocess
from pathlib import Path

DEFAULT_COLLECTION_SEEDS = tuple(range(2001, 2013))


def body_command(
    executable: Path,
    brain_directory: Path,
    seed: int,
    max_steps: int,
) -> list[str]:
    return [
        str(executable),
        str(brain_directory),
        "--seed",
        str(seed),
        "--max-steps",
        str(max_steps),
        "--challenge-scenario",
    ]


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Collect challenge experiences from multiple maze seeds.",
    )
    parser.add_argument(
        "--body",
        type=Path,
        default=Path("cmake-build-debug/body/aura_body"),
    )
    parser.add_argument(
        "--brain-directory",
        type=Path,
        default=Path.cwd(),
    )
    parser.add_argument(
        "--seeds",
        type=int,
        nargs="+",
        default=DEFAULT_COLLECTION_SEEDS,
    )
    parser.add_argument(
        "--max-steps",
        type=int,
        default=600,
    )
    args = parser.parse_args()

    if args.max_steps <= 0:
        parser.error("Maximum steps must be positive.")

    for seed in args.seeds:
        print(f"Collecting seed {seed}...")
        subprocess.run(
            body_command(
                args.body,
                args.brain_directory,
                seed,
                args.max_steps,
            ),
            check=True,
        )


if __name__ == "__main__":
    main()
