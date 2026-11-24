# Python learning code

The runtime modules remain at stable paths so the environment, trainer, and
archived experiment fingerprints keep their existing contracts.

| Module | Role |
| --- | --- |
| `aura_env.py` | Observation encoding, single-agent environment, reward/termination versions |
| `arena_env.py` | Historical shared arena and checkpoint environment selection |
| `independent_env.py`, `worker_config.py` | Default spawned collectors, seeds, cancellation and resource budgets |
| `model_templates.py` | Validated untrained actor/critic designs |
| `experiment_library.py`, `experiment_rewards.py` | Bounded experiment registry, provenance, comparisons and explicit reward alternatives |
| `aura_policy.py` | Actor and value-network definitions and action transforms |
| `balance_quality.py`, `balance_metrics.py` | Active physical quality and evaluation measurements |
| `model_checkpoint.py` | Saving, loading, validation, and compatibility |
| `train_nn.py` | Production actor–critic updates and evaluation |
| `control_aura.py` | Live viewer action bridge |

`balance_quality_v2.py`, `balance_reward_v2.py`, and `failure_tail.py` preserve
earlier reward semantics and diagnostics. Their presence does not select them
for new training. `ppo_experiment.py` and `training_strategy_experiment.py` stay
at their original paths because archived matched-comparison manifests verify
their exact contents. They are experimental, not the production trainer.

## Investigations

- `experiments/`: actor/critic comparisons, gradient studies, bootstrap analyses,
  reward-repair pilots, critic-fit studies, and research report generators.
- `diagnostics/`: balance and termination traces, reward-component measurements,
  incentive audits, and a bounded integration smoke test.
- `historical/`: the older balance-parameter search and session.

Run tools as modules from the repository root, with the compiled extension and
PyTorch available. For example, these commands only display their options:

```sh
PYTHONPATH=build-release:. python3 -m python.experiments.critic_experiment --help
PYTHONPATH=build-release:. python3 -m python.diagnostics.reward_component_audit --help
PYTHONPATH=build-release:. python3 -m cli.backend.nn_session --help
```

Consult a tool's arguments before running an experiment: investigation scripts
can collect episodes, fit networks, and write new experiment artifacts. Merely
moving them does not make their older environment or reward a current default.
Saved report/trace contents retain their original paths and hashes as provenance;
current source and documentation use the new module locations.

The package initializer keeps older `python -m python.<tool>` module commands
resolvable in these investigation folders. This is a compatibility lookup, not
a second copy of each implementation. Direct filesystem paths in old commands
must use the relocated file; the examples above use explicit module paths.

Current worker/experiment usage and compatibility rules are documented in [the CLI guide](../cli/README.md#model-designs-and-experiments). Independent environment contract 6 excludes worker count from neural compatibility; historical coupled collectors require explicit migration.
