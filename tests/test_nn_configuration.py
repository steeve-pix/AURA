"""Configuration changes must reach training and CLI telemetry together."""
import contextlib
import io
from math import sqrt
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / "build-release")]

import torch
import aura_python
from cli.nn_session import TrainingEnvironment, TrainingSession
from python.aura_policy import (
    AuraPolicy, AuraValue, outputs_to_action, ACTION_TRANSFORMS, ACTION_SCHEMA_VERSION,
    CONTROLLED_ACTION_KEYS,
)
from python.aura_env import (
    ANGULAR_SPEED_SCALE, BODY_PART_ORDER, CHARACTER_HEIGHT, JOINT_ORDER,
    LINEAR_SPEED_SCALE, OBSERVATION_LAYOUT, OBSERVATION_SCHEMA_VERSION,
    OBSERVATION_SIZE, encode_observation,
)
from python.train_nn import TrainingConfig, train_batch, train_critic, collect_critic_rollouts, evaluate_policy, DISCOUNT_FACTOR, CRITIC_RETURN_SCALE
from python.control_aura import write_action


class ShortEnvironment:
    episode_limit = 777
    observation_size = 17

    def __init__(self):
        self.episode = 0
        self.actions = []

    def reset(self):
        self.episode += 1
        self.steps = 0
        return torch.zeros(self.observation_size), {}

    def step(self, action):
        self.actions.append(action)
        self.steps += 1
        done = self.steps == self.episode % 3 + 1
        return torch.zeros(self.observation_size), 1., done, False, dict(
            steps=self.steps, pelvis_height=3., pelvis_uprightness=1.,
            terminated=done, truncated=False, termination_reason="fallen")


def feature_slice(name):
    feature = next(group for group in OBSERVATION_LAYOUT if group["name"] == name)
    return slice(feature["start"], feature["end"])


def synthetic_observation():
    return {
        "joints": [
            {
                "name": name,
                "relative_orientation": (1.0, 0.0, 0.0, 0.0),
                "relative_angular_velocity": (0.0, 0.0, 0.0),
            }
            for name in reversed(JOINT_ORDER)
        ],
        "body_part_states": [
            {"name": name, "velocity": (0.0, 0.0, 0.0)}
            for name in reversed(BODY_PART_ORDER)
        ],
        "pelvis_position": (10.0, 4.0, 10.0),
        "pelvis_velocity": (1.0, 2.0, 3.0),
        "pelvis_orientation": (1.0, 0.0, 0.0, 0.0),
        "pelvis_angular_velocity": (0.0, 0.0, 0.0),
        "pelvis_up": (0.0, 1.0, 0.0),
        "center_of_mass_position": (11.0, 5.0, 12.0),
        "center_of_mass_velocity": (4.0, 6.0, 8.0),
        "left_foot_position": (9.5, 0.0, 10.2),
        "right_foot_position": (10.5, 0.0, 9.8),
        "left_foot_velocity": (4.0, 7.0, 9.0),
        "right_foot_velocity": (-2.0, 5.0, 1.0),
        "left_foot_grounded": True,
        "right_foot_grounded": False,
        "left_foot_contact_points": [],
        "right_foot_contact_points": [],
        "support_polygon": [],
        "center_of_mass_inside_support_polygon": False,
        "support_margin": 0.0,
    }


class ConfigurationTests(unittest.TestCase):
    def test_value_network_has_two_tanh_hidden_layers_and_linear_scalar_output(self):
        critic = AuraValue(OBSERVATION_SIZE)
        layers = list(critic.network)
        self.assertEqual(
            [(layer.in_features, layer.out_features) for layer in layers if isinstance(layer, torch.nn.Linear)],
            [(OBSERVATION_SIZE, 64), (64, 64), (64, 1)],
        )
        self.assertEqual(
            [type(layer) for layer in layers],
            [torch.nn.Linear, torch.nn.Tanh, torch.nn.Linear, torch.nn.Tanh, torch.nn.Linear],
        )
        self.assertEqual(critic(torch.zeros(OBSERVATION_SIZE)).shape, (1,))
        values = critic(torch.zeros(3, OBSERVATION_SIZE))
        self.assertEqual(values.shape, (3, 1))
        self.assertTrue(torch.isfinite(values).all())
        values.sum().backward()
        self.assertTrue(all(parameter.grad is not None for parameter in critic.parameters()))

    def test_critic_update_uses_aligned_scaled_targets_and_leaves_actor_unchanged(self):
        torch.manual_seed(42)
        actor = AuraPolicy(OBSERVATION_SIZE)
        critic = AuraValue(OBSERVATION_SIZE)
        optimizer = torch.optim.Adam(critic.parameters(), lr=0.001)
        actor_before = [parameter.detach().clone() for parameter in actor.parameters()]
        critic_before = [parameter.detach().clone() for parameter in critic.parameters()]
        # An actor-connected input verifies critic learning detaches its dataset.
        connected = torch.ones(OBSERVATION_SIZE) * actor(torch.zeros(OBSERVATION_SIZE)).sum()
        episodes = [[torch.zeros(OBSERVATION_SIZE), connected], [torch.ones(OBSERVATION_SIZE)]]
        returns = [[199.5, 100.0], [0.0]]
        expected_states = torch.stack([state.detach() for episode in episodes for state in episode])
        expected_targets = torch.tensor([[1.995], [1.0], [0.0]])
        self.assertEqual(CRITIC_RETURN_SCALE, 100.0)
        with torch.no_grad():
            expected_loss = torch.nn.functional.mse_loss(critic(expected_states), expected_targets).item()
        with patch.object(critic, "forward", wraps=critic.forward) as forward, \
                patch("python.train_nn.torch.nn.functional.mse_loss", wraps=torch.nn.functional.mse_loss) as mse:
            loss = train_critic(critic, optimizer, episodes, returns)
        observed_states = forward.call_args.args[0]
        self.assertEqual(observed_states.shape, (3, OBSERVATION_SIZE))
        torch.testing.assert_close(observed_states, expected_states)
        self.assertFalse(observed_states.requires_grad)
        predictions, targets = mse.call_args.args
        self.assertEqual(predictions.shape, (3, 1))
        self.assertEqual(targets.shape, (3, 1))
        torch.testing.assert_close(targets, expected_targets)
        self.assertTrue(torch.isfinite(torch.tensor(loss)))
        self.assertAlmostEqual(loss, expected_loss)
        self.assertTrue(any(not torch.equal(parameter, old)
                            for parameter, old in zip(critic.parameters(), critic_before)))
        self.assertTrue(optimizer.state)
        for parameter, old in zip(actor.parameters(), actor_before):
            torch.testing.assert_close(parameter, old, rtol=0.0, atol=0.0)
            self.assertIsNone(parameter.grad)
        self.assertTrue(all(torch.isfinite(parameter.grad).all() for parameter in critic.parameters()))

    def test_critic_rejects_empty_misaligned_or_nonfinite_data_without_update(self):
        critic = AuraValue(OBSERVATION_SIZE)
        optimizer = torch.optim.Adam(critic.parameters())
        state = torch.zeros(OBSERVATION_SIZE)
        before = [parameter.detach().clone() for parameter in critic.parameters()]
        cases = [
            ([], []),
            ([[state]], []),
            ([[], [state]], [[], [1.0]]),
            ([[state, state]], [[1.0]]),
            ([[state]], [[1.0, 2.0]]),
            ([[state, torch.zeros(3)]], [[1.0, 2.0]]),
            ([[state]], [[float("nan")]]),
            ([[torch.full_like(state, float("inf"))]], [[1.0]]),
            ([[state]], [[[1.0, 2.0]]]),
        ]
        for states, returns in cases:
            with self.subTest(states=states, returns=returns), self.assertRaisesRegex(ValueError, "Critic|critic"):
                train_critic(critic, optimizer, states, returns)
            self.assertFalse(optimizer.state)
        for parameter, old in zip(critic.parameters(), before):
            torch.testing.assert_close(parameter, old, rtol=0.0, atol=0.0)

    def test_critic_rejects_broadcasting_prediction_shapes(self):
        critic = torch.nn.Linear(OBSERVATION_SIZE, 2)
        optimizer = torch.optim.Adam(critic.parameters())
        with self.assertRaisesRegex(ValueError, "shape"):
            train_critic(critic, optimizer, [[torch.zeros(OBSERVATION_SIZE)]], [[1.0]])
        self.assertFalse(optimizer.state)

    def test_critic_rollouts_are_graph_free_reproducible_and_use_independent_streams(self):
        test = self

        class CheckedPolicy(AuraPolicy):
            def sample_action(self, observation):
                test.assertFalse(torch.is_grad_enabled())
                action, log_probability = super().sample_action(observation)
                test.assertFalse(log_probability.requires_grad)
                return action, log_probability

        torch.manual_seed(42)
        actor = CheckedPolicy(ShortEnvironment.observation_size)
        before = [parameter.detach().clone() for parameter in actor.parameters()]
        rng_before = torch.get_rng_state().clone()
        environments = [ShortEnvironment() for _ in range(3)]
        datasets = [collect_critic_rollouts(env, actor, episodes=3, seed=seed)
                    for env, seed in zip(environments, (4201, 4201, 4202))]
        self.assertTrue(torch.equal(torch.get_rng_state(), rng_before))
        self.assertTrue(actor.training)
        self.assertEqual(environments[0].actions, environments[1].actions)
        self.assertNotEqual(environments[0].actions, environments[2].actions)
        self.assertEqual(datasets[0]["episode_scores"], [2, 3, 1])
        self.assertEqual(datasets[0]["returns_by_episode"], datasets[1]["returns_by_episode"])
        for states, returns, steps in zip(datasets[0]["states_by_episode"], datasets[0]["returns_by_episode"], [2, 3, 1]):
            self.assertEqual(len(states), steps)
            self.assertEqual(len(returns), steps)
            self.assertTrue(all(state.grad_fn is None and not state.requires_grad for state in states))
        self.assertAlmostEqual(datasets[0]["returns_by_episode"][0][0], 1.995)
        for parameter, old in zip(actor.parameters(), before):
            torch.testing.assert_close(parameter, old, rtol=0.0, atol=0.0)
            self.assertIsNone(parameter.grad)
        with self.assertRaisesRegex(ValueError, "positive"):
            collect_critic_rollouts(ShortEnvironment(), actor, episodes=0, seed=1)

    def test_critic_experiment_keeps_validation_out_of_optimizer_updates(self):
        from python import critic_experiment
        datasets, validation_snapshots = [], []

        def collect(*args, **kwargs):
            data = collect_critic_rollouts(*args, **kwargs)
            datasets.append(data)
            if len(datasets) == 2:
                validation_snapshots.extend(state.clone() for episode in data["states_by_episode"] for state in episode)
            return data

        with patch.object(critic_experiment, "collect_critic_rollouts", side_effect=collect), \
                patch.object(critic_experiment, "train_critic", wraps=train_critic) as train:
            result = critic_experiment.run_critic_experiment(
                training_episodes=3, validation_episodes=2, updates=20, env_factory=ShortEnvironment)
        self.assertEqual(train.call_count, 20)
        for call in train.call_args_list:
            self.assertIs(call.args[2], datasets[0]["states_by_episode"])
            self.assertIs(call.args[3], datasets[0]["returns_by_episode"])
        for state, old in zip((state for episode in datasets[1]["states_by_episode"] for state in episode), validation_snapshots):
            torch.testing.assert_close(state, old, rtol=0.0, atol=0.0)
        training_targets = torch.tensor([value for episode in datasets[0]["returns_by_episode"] for value in episode])/100.0
        validation_targets = torch.tensor([value for episode in datasets[1]["returns_by_episode"] for value in episode])/100.0
        expected_baseline = (validation_targets-training_targets.mean()).square().mean().item()
        self.assertAlmostEqual(result["constant_scaled_prediction"], training_targets.mean().item())
        self.assertAlmostEqual(result["constant_validation_mse"], expected_baseline)
        self.assertEqual([row["update"] for row in result["measurements"]], [0, 1, 5, 10, 20])
        self.assertTrue(all(torch.isfinite(torch.tensor([row["training_mse"], row["validation_mse"]])).all()
                            for row in result["measurements"]))
        self.assertEqual(result["actor_optimizer_updates"], 0)
        self.assertEqual(result["actor_max_parameter_change"], 0.0)
        self.assertTrue(result["actor_has_no_gradients"])
        self.assertGreater(result["critic_max_parameter_change"], 0.0)
        self.assertFalse(result["validation_used_for_optimization"])
        with self.assertRaisesRegex(ValueError, "independent"):
            critic_experiment.run_critic_experiment(training_seed=1, validation_seed=1)

    def test_critic_collection_cancels_without_actor_graphs_and_restores_rng(self):
        torch.manual_seed(42)
        actor = AuraPolicy(ShortEnvironment.observation_size)
        before = torch.get_rng_state().clone()
        calls = []

        def cancel():
            calls.append(True)
            if len(calls) == 3:
                raise KeyboardInterrupt

        with self.assertRaises(KeyboardInterrupt):
            collect_critic_rollouts(ShortEnvironment(), actor, 3, seed=4201, service=cancel)
        self.assertTrue(actor.training)
        self.assertTrue(torch.equal(torch.get_rng_state(), before))
        self.assertTrue(all(parameter.grad is None for parameter in actor.parameters()))

    def test_critic_mode_reads_actor_snapshot_and_saves_separate_unique_artifacts(self):
        import tempfile
        import json
        from cli import critic_session
        from python.critic_experiment import run_critic_experiment
        from python.model_checkpoint import save_checkpoint

        class FullObservationEnvironment(ShortEnvironment):
            observation_size = OBSERVATION_SIZE

        torch.manual_seed(42)
        actor = AuraPolicy(OBSERVATION_SIZE)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            actor_path = root/"actor.pt"
            save_checkpoint(actor, torch.optim.Adam(actor.parameters()), 0, actor_path)
            original = actor_path.read_bytes()
            artifacts = root/"critics"
            events = []

            def small_experiment(**kwargs):
                return run_critic_experiment(training_episodes=3, validation_episodes=2,
                                             env_factory=FullObservationEnvironment, **kwargs)

            with patch("cli.critic_session.run_critic_experiment", side_effect=small_experiment):
                worker = critic_session.CriticSession(lambda *event: events.append(event), artifacts_dir=artifacts)
                first = worker.run(actor_path)
                second = worker.run(actor_path)
            self.assertEqual(actor_path.read_bytes(), original)
            self.assertNotEqual(first["critic_path"], second["critic_path"])
            for report in (first, second):
                saved = torch.load(report["critic_path"], weights_only=True)
                self.assertEqual(saved["kind"], "aura_value")
                self.assertEqual(saved["model"]["network.4.weight"].shape, (1, 64))
                self.assertEqual(saved["updates_completed"], 20)
                self.assertEqual(saved["return_scale"], 100.0)
                self.assertEqual(saved["experiment"]["actor_max_parameter_change"], 0.0)
                self.assertEqual(json.loads(Path(report["report_path"]).read_text())["actor_optimizer_updates"], 0)
                self.assertEqual(report["actor_source"], str(actor_path))
                self.assertIsNone(report["actor_seed"])
                self.assertIsNotNone(report["actor_checkpoint_sha256"])
            self.assertEqual(sum(event[0]=="result" for event in events), 2)

    def test_training_batch_reports_advantage_support_without_updating_weights(self):
        torch.manual_seed(13)
        policy = AuraPolicy(ShortEnvironment.observation_size)
        optimizer = torch.optim.Adam(policy.parameters(), lr=0.001)
        before = [parameter.detach().clone() for parameter in policy.parameters()]

        result = train_batch(
            ShortEnvironment(), policy, optimizer, batch_size=3,
            update_parameters=False,
        )

        self.assertEqual(len(result["states_by_episode"]), 3)
        self.assertEqual(len(result["returns_by_episode"]), 3)
        for states, returns, steps in zip(result["states_by_episode"], result["returns_by_episode"], result["episode_scores"]):
            self.assertEqual(len(states), steps)
            self.assertEqual(len(returns), steps)
            self.assertTrue(all(not state.requires_grad for state in states))
        self.assertEqual(result["action_timestep_terms"], sum(result["episode_scores"]))
        self.assertEqual(result["peerless_terms"], 1)
        self.assertGreaterEqual(result["average_absolute_advantage"], 0.0)
        self.assertGreaterEqual(
            result["maximum_absolute_advantage"],
            result["average_absolute_advantage"],
        )
        self.assertGreaterEqual(result["peerless_absolute_advantage_percent"], 0.0)
        self.assertLessEqual(result["peerless_absolute_advantage_percent"], 100.0)
        self.assertTrue(torch.isfinite(torch.tensor(result["loss"])))
        self.assertFalse(result["parameters_updated"])
        self.assertEqual(result["max_parameter_change"], 0.0)
        self.assertFalse(optimizer.state)
        for parameter, old in zip(policy.parameters(), before):
            torch.testing.assert_close(parameter, old, rtol=0.0, atol=0.0)

    def test_saved_states_align_with_raw_discounted_returns_without_actor_update(self):
        class SnapshotEnvironment(ShortEnvironment):
            def __init__(self):
                super().__init__()
                self.observations = []

            def reset(self):
                self.episode += 1
                self.steps = 0
                self.state = torch.full((self.observation_size,), float(self.episode * 10))
                self.observations.append(self.state)
                return self.state, {}

            def step(self, action):
                reward = [1.0, 1.0, 0.0][self.steps]
                self.steps += 1
                self.state = torch.full_like(self.state, self.episode * 10 + self.steps)
                self.observations.append(self.state)
                return self.state, reward, self.steps == 3, False, {"steps": self.steps}

        torch.manual_seed(13)
        env = SnapshotEnvironment()
        policy = AuraPolicy(env.observation_size)
        optimizer = torch.optim.Adam(policy.parameters())
        before = [parameter.detach().clone() for parameter in policy.parameters()]
        result = train_batch(env, policy, optimizer, batch_size=2, update_parameters=False)

        # Mutating the source tensors after collection must not change saved states.
        for observation in env.observations:
            observation.fill_(-999.0)
        self.assertEqual(len(result["states_by_episode"]), 2)
        self.assertEqual(len(result["returns_by_episode"]), 2)
        for episode, (states, returns) in enumerate(zip(
            result["states_by_episode"], result["returns_by_episode"]
        ), start=1):
            self.assertEqual(len(states), len(returns))
            self.assertEqual(len(states), 3)
            for step, (state, target) in enumerate(zip(states, returns)):
                torch.testing.assert_close(state, torch.full_like(state, episode * 10 + step))
                self.assertFalse(state.requires_grad)
                self.assertAlmostEqual(target, [1.995, 1.0, 0.0][step])
        self.assertFalse(result["parameters_updated"])
        self.assertEqual(result["max_parameter_change"], 0.0)
        self.assertFalse(optimizer.state)
        for parameter, previous in zip(policy.parameters(), before):
            torch.testing.assert_close(parameter, previous, rtol=0.0, atol=0.0)

    def test_cli_reports_live_reward_and_completed_returns_without_actor_updates(self):
        torch.manual_seed(13)
        env = ShortEnvironment()
        policy = AuraPolicy(env.observation_size)
        optimizer = torch.optim.Adam(policy.parameters())
        before = [parameter.detach().clone() for parameter in policy.parameters()]
        with patch("cli.nn_session.create_training", return_value=(env, policy, optimizer)), \
                contextlib.redirect_stdout(io.StringIO()) as output:
            session = TrainingSession(events=True, config=TrainingConfig(batch_size=3))
            session.started()
            wrapped = TrainingEnvironment(env, session)
            for _ in range(3):
                state, _ = wrapped.reset()
                while True:
                    action, _ = policy.sample_action(state)
                    state, _, terminated, truncated, _ = wrapped.step(action)
                    if terminated or truncated:
                        break
        events = [line.split("\t") for line in output.getvalue().splitlines() if line.startswith("AURA_NN\treward\t")]
        finished = [event for event in events if event[-1] == "1"]
        self.assertEqual(len(finished), 3)
        for episode, event in enumerate(finished, start=1):
            steps = episode % 3 + 1
            self.assertEqual(event[2:5], ["1", str(episode), str(steps)])
            self.assertEqual(float(event[5]), 1.0)
            self.assertEqual(float(event[6]), float(steps))
        self.assertEqual(session.update, 0)
        self.assertFalse(optimizer.state)
        for parameter, old in zip(policy.parameters(), before):
            torch.testing.assert_close(parameter, old, rtol=0.0, atol=0.0)

    def test_cli_announces_actual_interface_without_training(self):
        with contextlib.redirect_stdout(io.StringIO()) as output:
            session = TrainingSession(events=True)
        events = output.getvalue().splitlines()
        self.assertIn("AURA_NN\tinterface\t205\t14\t2\t3", events)
        self.assertIn("AURA_NN\tmotors\t14\t15\tHead", events)
        self.assertNotIn("AURA_NN\taction\thead_target\t", output.getvalue())
        self.assertEqual(session.update, 0)
        self.assertFalse(session.optimizer.state)

    def test_observation_schema_and_deterministic_joint_body_order(self):
        observation = synthetic_observation()
        for index, name in enumerate(JOINT_ORDER, start=1):
            record = next(item for item in observation["joints"] if item["name"] == name)
            record["relative_orientation"] = (1.0, index / 100.0, 0.0, 0.0)
            record["relative_angular_velocity"] = (float(index), 0.0, -float(index))
        for index, name in enumerate(BODY_PART_ORDER, start=1):
            record = next(item for item in observation["body_part_states"] if item["name"] == name)
            record["velocity"] = (float(index), 2.0 * index, -float(index))

        encoded = encode_observation(observation)
        self.assertEqual(OBSERVATION_SCHEMA_VERSION, 2)
        self.assertEqual(encoded.numel(), OBSERVATION_SIZE)
        self.assertEqual(OBSERVATION_SIZE, sum(group["size"] for group in OBSERVATION_LAYOUT))
        self.assertEqual(OBSERVATION_LAYOUT[0]["start"], 0)
        self.assertEqual(OBSERVATION_LAYOUT[-1]["end"], OBSERVATION_SIZE)
        for previous, following in zip(OBSERVATION_LAYOUT, OBSERVATION_LAYOUT[1:]):
            self.assertEqual(previous["end"], following["start"])
        self.assertTrue(torch.isfinite(encoded).all())

        orientations = encoded[feature_slice("joint_relative_orientations")]
        angular = encoded[feature_slice("joint_relative_angular_velocities")]
        self.assertAlmostEqual(orientations[5].item(), 0.02 / sqrt(1.0 + 0.02**2))
        self.assertAlmostEqual(orientations[-3].item(), 0.15 / sqrt(1.0 + 0.15**2))
        self.assertAlmostEqual(angular[0].item(), 1.0 / ANGULAR_SPEED_SCALE)
        self.assertAlmostEqual(angular[-1].item(), -15.0 / ANGULAR_SPEED_SCALE, places=6)

        body_velocities = encoded[feature_slice("body_part_linear_velocities")]
        self.assertAlmostEqual(body_velocities[0].item(), 1.0 / LINEAR_SPEED_SCALE)
        self.assertAlmostEqual(body_velocities[(BODY_PART_ORDER.index("pelvis") * 3)].item(),
                               4.0 / LINEAR_SPEED_SCALE)
        self.assertAlmostEqual(body_velocities[-3].item(), 16.0 / LINEAR_SPEED_SCALE)

    def test_com_and_foot_relative_positions_and_velocities(self):
        observation = synthetic_observation()
        encoded = encode_observation(observation)

        torch.testing.assert_close(
            encoded[feature_slice("com_position_relative_to_pelvis")],
            torch.tensor([1.0, 1.0, 2.0]) / CHARACTER_HEIGHT,
        )
        torch.testing.assert_close(
            encoded[feature_slice("com_velocity_relative_to_pelvis")],
            torch.tensor([3.0, 4.0, 5.0]) / LINEAR_SPEED_SCALE,
        )
        torch.testing.assert_close(
            encoded[feature_slice("com_world_horizontal_velocity")],
            torch.tensor([4.0, 8.0]) / LINEAR_SPEED_SCALE,
        )
        torch.testing.assert_close(
            encoded[feature_slice("foot_positions_relative_to_pelvis")],
            torch.tensor([-0.5, -4.0, 0.2, 0.5, -4.0, -0.2]) / CHARACTER_HEIGHT,
        )
        torch.testing.assert_close(encoded[feature_slice("foot_grounded")], torch.tensor([1.0, 0.0]))
        torch.testing.assert_close(
            encoded[feature_slice("foot_velocities_relative_to_pelvis")],
            torch.tensor([3.0, 5.0, 6.0, -3.0, 3.0, -2.0]) / LINEAR_SPEED_SCALE,
        )

    def test_rotating_pelvis_frame_velocity_and_orientation(self):
        observation = synthetic_observation()
        half = sqrt(0.5)
        observation["pelvis_orientation"] = (half, 0.0, half, 0.0)
        observation["pelvis_position"] = (0.0, 0.0, 0.0)
        observation["pelvis_velocity"] = (0.0, 0.0, 0.0)
        observation["pelvis_angular_velocity"] = (0.0, 0.0, 2.0)
        observation["center_of_mass_position"] = (1.0, 0.0, 0.0)
        # Relative velocity is zero after accounting for omega × r in the body frame.
        observation["center_of_mass_velocity"] = (0.0, 2.0, 0.0)
        encoded = encode_observation(observation)
        torch.testing.assert_close(
            encoded[feature_slice("com_position_relative_to_pelvis")],
            torch.tensor([0.0, 0.0, 1.0]) / CHARACTER_HEIGHT,
            atol=1e-6, rtol=1e-6,
        )
        torch.testing.assert_close(
            encoded[feature_slice("com_velocity_relative_to_pelvis")],
            torch.zeros(3), atol=1e-6, rtol=1e-6,
        )

    def test_support_and_contact_geometry_are_fixed_size_and_masked(self):
        observation = synthetic_observation()
        observation["support_polygon"] = [(10.0, 11.0), (12.0, 11.0), (12.0, 13.0), (10.0, 13.0)]
        observation["center_of_mass_inside_support_polygon"] = True
        observation["support_margin"] = 1.0
        observation["left_foot_contact_points"] = [(9.0, 0.0, 10.0), (10.0, 0.0, 10.0)]
        observation["right_foot_contact_points"] = [(11.0, 0.0, 10.0)]
        encoded = encode_observation(observation)
        support = encoded[feature_slice("support_state")]
        polygon = encoded[feature_slice("support_polygon_geometry")]
        contacts = encoded[feature_slice("foot_contact_geometry")]
        torch.testing.assert_close(support[:2], torch.tensor([1.0, 1.0]))
        self.assertAlmostEqual(support[2].item(), 1.0 / CHARACTER_HEIGHT)
        torch.testing.assert_close(polygon[:2], torch.zeros(2), atol=1e-6, rtol=1e-6)
        torch.testing.assert_close(polygon[2:4], torch.tensor([1.0, 1.0]) / CHARACTER_HEIGHT)
        self.assertAlmostEqual(polygon[4].item(), 4.0 / CHARACTER_HEIGHT**2)
        self.assertEqual(polygon[-1].item(), 4.0 / 16.0)
        self.assertEqual(contacts[0].item(), 1.0)
        self.assertEqual(contacts[1].item(), 2.0 / 8.0)
        self.assertEqual(contacts[7].item(), 1.0)
        self.assertEqual(contacts[8].item(), 1.0 / 8.0)

        observation["support_polygon"] = []
        observation["center_of_mass_inside_support_polygon"] = False
        observation["support_margin"] = 0.0
        observation["left_foot_contact_points"] = []
        observation["right_foot_contact_points"] = [(0.0, 0.0, 0.0)] * 5
        empty_or_partial = encode_observation(observation)
        self.assertEqual(empty_or_partial.numel(), OBSERVATION_SIZE)
        torch.testing.assert_close(
            empty_or_partial[feature_slice("support_state")], torch.zeros(3)
        )
        self.assertEqual(empty_or_partial[feature_slice("support_polygon_geometry")][-1].item(), 0.0)
        self.assertEqual(empty_or_partial[feature_slice("foot_contact_geometry")][0].item(), 0.0)
        self.assertEqual(empty_or_partial[feature_slice("foot_contact_geometry")][7].item(), 1.0)
        self.assertEqual(empty_or_partial[feature_slice("foot_contact_geometry")][8].item(), 5.0 / 8.0)
        self.assertTrue(torch.isfinite(empty_or_partial).all())

    def test_encoder_rejects_nonfinite_float32_results(self):
        observation = synthetic_observation()
        observation["center_of_mass_velocity"] = (1e100, 0.0, 0.0)
        with self.assertRaisesRegex(ValueError, "finite in float32"):
            encode_observation(observation)

    def test_whole_body_policy_and_real_observation_before_and_after_contact(self):
        from python.aura_env import AuraEnv
        from python.train_nn import create_training
        env = AuraEnv()
        state, obs = env.reset()
        policy = AuraPolicy(env.observation_size)
        self.assertEqual(env.observation_size, OBSERVATION_SIZE)
        self.assertEqual(policy(state).numel(), len(CONTROLLED_ACTION_KEYS))
        action, _log_probability = policy.sample_action(state)
        self.assertEqual(list(action), list(CONTROLLED_ACTION_KEYS))
        training_env, training_policy, _optimizer = create_training(TrainingConfig(seed=7))
        training_state, _ = training_env.reset()
        self.assertEqual(training_policy(training_state).numel(), len(CONTROLLED_ACTION_KEYS))
        self.assertEqual(training_state.numel(), OBSERVATION_SIZE)
        self.assertFalse(obs["left_foot_grounded"] or obs["right_foot_grounded"])
        self.assertEqual(state.numel(), OBSERVATION_SIZE)
        self.assertFalse(state[feature_slice("support_state")][0].item())

        saw_contact = False
        for _ in range(120):
            env.simulation.step()
            obs = env.simulation.observation()
            state = encode_observation(obs)
            if obs["left_foot_grounded"] or obs["right_foot_grounded"]:
                saw_contact = True
                break
        self.assertTrue(saw_contact)
        self.assertEqual(state.numel(), OBSERVATION_SIZE)
        self.assertTrue(torch.isfinite(state).all())
        self.assertGreater(state[feature_slice("foot_contact_geometry")][0:1].item()
                           + state[feature_slice("foot_contact_geometry")][7:8].item(), 0.0)

    def test_evaluation_accepts_new_fixed_observation_size(self):
        class EvaluationEnvironment:
            def reset(self):
                self.steps = 0
                return torch.zeros(OBSERVATION_SIZE), {"done": False}

            def step(self, action):
                self.steps += 1
                return torch.zeros(OBSERVATION_SIZE), 0.0, False, False, {
                    "done": self.steps == 2,
                    "steps": self.steps,
                }

        policy = AuraPolicy(OBSERVATION_SIZE)
        self.assertEqual(evaluate_policy(EvaluationEnvironment(), policy), 2)

    def test_whole_body_action_order_and_neural_ranges(self):
        keys = (
            "left_hip_target", "right_hip_target", "left_knee_target", "right_knee_target",
            "left_ankle_target", "right_ankle_target", "waist_target", "neck_target",
            "left_shoulder_target", "right_shoulder_target", "left_elbow_target", "right_elbow_target",
            "left_wrist_target", "right_wrist_target",
        )
        self.assertEqual(tuple(ACTION_TRANSFORMS), keys)
        self.assertEqual(CONTROLLED_ACTION_KEYS, keys)
        self.assertEqual(len(aura_python.action_specs), 15)
        self.assertEqual(ACTION_SCHEMA_VERSION, 3)
        self.assertNotEqual(ACTION_SCHEMA_VERSION, aura_python.action_schema_version)
        controlled_specs = [spec for spec in aura_python.action_specs if spec["key"] in keys]
        self.assertEqual(tuple(spec["key"] for spec in controlled_specs), keys)
        self.assertNotIn("head_target", ACTION_TRANSFORMS)
        self.assertEqual(len(keys), 14)
        for index, spec in enumerate(controlled_specs):
            scale, nonnegative = ACTION_TRANSFORMS[spec["key"]]
            self.assertGreater(scale, 0)
            self.assertLessEqual(scale, spec["maximum"])
            if not nonnegative:
                self.assertGreaterEqual(-scale, spec["minimum"])
            for sign in (-1.0, 1.0):
                output = torch.zeros(len(keys))
                output[index] = sign
                action = outputs_to_action(output)
                expected = scale * (max(0.0, sign) if nonnegative else sign)
                self.assertAlmostEqual(action[spec["key"]], expected)
                self.assertTrue(all(value == 0 for key, value in action.items() if key != spec["key"]))
        bounded = outputs_to_action(torch.full((len(keys),), 10.0))
        for key, value in bounded.items():
            self.assertAlmostEqual(value, ACTION_TRANSFORMS[key][0])
        with self.assertRaisesRegex(ValueError, "finite"):
            outputs_to_action(torch.full((len(keys),), float("nan")))

    def test_all_actions_round_trip_clamp_partial_and_invalid_values(self):
        sim = aura_python.AuraSimulation()
        keys = [spec["key"] for spec in aura_python.action_specs]
        self.assertEqual(list(sim.action()), keys)
        for spec in aura_python.action_specs:
            sim.reset()
            for requested, limit in ((100.0, spec["maximum"]), (-100.0, spec["minimum"])):
                reported = sim.set_action({spec["key"]: requested})
                self.assertAlmostEqual(reported[spec["key"]], limit, places=6)
                self.assertEqual(sim.action(), reported)
                self.assertTrue(all(value == 0 for key, value in reported.items() if key != spec["key"]))
        sim.reset()
        sim.set_action({"left_shoulder_target": 0.2})
        sim.set_action({"right_shoulder_target": -0.3})
        self.assertAlmostEqual(sim.action()["left_shoulder_target"], 0.2)
        self.assertAlmostEqual(sim.action()["right_shoulder_target"], -0.3)
        previous = sim.action()
        for invalid in (float("nan"), float("inf"), -float("inf")):
            with self.assertRaisesRegex(ValueError, "finite"):
                sim.step_action({"head_target": invalid})
            self.assertEqual(sim.action(), previous)
            self.assertEqual(sim.observation()["steps"], 0)
        with self.assertRaisesRegex(ValueError, "Unknown action"):
            sim.set_action({"missing_joint_target": 0.1})
        with self.assertRaises((TypeError, RuntimeError, ValueError)):
            sim.set_action({"head_target": "not an angle"})
        # Legacy six-key dictionaries leave appended targets at their defaults.
        sim.reset()
        sim.set_action(dict.fromkeys(keys[:6], 0.1))
        self.assertTrue(all(sim.action()[key] == 0 for key in keys[6:]))

        state = encode_observation(sim.observation())
        policy = AuraPolicy(OBSERVATION_SIZE)
        with torch.inference_mode():
            output = policy(state)
        self.assertEqual(tuple(output.shape), (14,))
        self.assertTrue(torch.isfinite(output).all())
        # The C++ interface still accepts the head target, while the default
        # neural policy leaves that motor disabled and emits the other 14 actions.
        observation = sim.step_action(outputs_to_action(output))
        self.assertTrue(torch.isfinite(encode_observation(observation)).all())

    def test_motor_selection_survives_reset(self):
        sim = aura_python.AuraSimulation()
        states = sim.motor_states()
        self.assertEqual(sum(states.values()), 14)
        self.assertFalse(states["head"])
        self.assertTrue(states["neck"])
        sim.set_motor_enabled("neck", False)
        sim.set_motor_enabled("head", True)
        sim.reset()
        self.assertFalse(sim.motor_states()["neck"])
        self.assertTrue(sim.motor_states()["head"])
        with self.assertRaisesRegex(ValueError, "Unknown motor"):
            sim.set_motor_enabled("unknown", True)

    def test_action_credit_matches_manual_loss_and_gradient(self):
        class Policy(torch.nn.Module):
            def __init__(self):
                super().__init__()
                self.weight = torch.nn.Parameter(torch.tensor(1.0))

            def sample_action(self, state):
                return {}, self.weight * state

        class Environment:
            episode_limit = 10

            def __init__(self):
                self.episode = -1

            def reset(self):
                self.episode += 1
                self.step_index = 0
                return torch.tensor(1.0 if self.episode == 0 else 3.0), {}

            def step(self, action):
                rewards = ([1.0, 2.0], [3.0])[self.episode]
                reward = rewards[self.step_index]
                self.step_index += 1
                return torch.tensor(2.0), reward, self.step_index == len(rewards), False, {"steps": self.step_index}

        policy = Policy()
        optimizer = torch.optim.SGD(policy.parameters(), lr=0.1)
        result = train_batch(Environment(), policy, optimizer, 2)
        first = (1 + DISCOUNT_FACTOR * 2 - 3) / 10
        last = 2 / 10  # No other episode reached step two: baseline is zero.
        other = -first
        expected = -(first + 2 * last + 3 * other) / 2
        self.assertAlmostEqual(result["loss"], expected, places=6)
        self.assertAlmostEqual(policy.weight.grad.item(), expected, places=6)
        self.assertAlmostEqual(policy.weight.item(), 1 - 0.1 * expected, places=6)
        self.assertEqual(result["advantages"], [0.0, 0.0]) # Diagnostics differ from step advantages.

    def test_balance_returns_drive_advantages_and_weight_updates(self):
        class RewardEnvironment(ShortEnvironment):
            def __init__(self, scale):
                super().__init__()
                self.scale = scale

            def step(self, action):
                state, _, terminated, truncated, info = super().step(action)
                return state, self.scale * self.steps, terminated, truncated, info

        runs = []
        for scale in (0.25, -2.0):
            torch.manual_seed(42)
            env = RewardEnvironment(scale)
            policy = AuraPolicy(env.observation_size, (9,))
            optimizer = torch.optim.Adam(policy.parameters())
            result = train_batch(env, policy, optimizer, 5)
            self.assertEqual(result["episode_returns"],
                             [scale * steps * (steps + 1) / 2 for steps in result["episode_scores"]])
            returns = result["episode_returns"]
            for i, advantage in enumerate(result["advantages"]):
                others = returns[:i] + returns[i+1:]
                self.assertAlmostEqual(advantage, (returns[i] - sum(others)/len(others))/env.episode_limit)
            runs.append((result, [p.detach().clone() for p in policy.parameters()]))
        self.assertEqual(runs[0][0]["episode_scores"], runs[1][0]["episode_scores"])
        self.assertNotEqual(runs[0][0]["loss"], runs[1][0]["loss"])
        self.assertTrue(any(not torch.equal(first, second)
                            for first, second in zip(runs[0][1], runs[1][1])))

    def test_variable_batch_and_network(self):
        for size in (2, 5, 16):
            config = TrainingConfig(batch_size=size, seed=11, learning_rate=.002)

            def create(_config):
                torch.manual_seed(_config.seed)
                env = ShortEnvironment()
                policy = AuraPolicy(env.observation_size, (12, 9, 7), exploration_std=.4)
                return env, policy, torch.optim.Adam(policy.parameters(), lr=_config.learning_rate)

            plain_env, plain_policy, plain_optimizer = create(config)
            plain_result = train_batch(plain_env, plain_policy, plain_optimizer, size)
            with patch("cli.nn_session.create_training", create), contextlib.redirect_stdout(io.StringIO()) as output:
                session = TrainingSession(events=True, config=config)
                session.started()
                before = [p.detach().clone() for p in session.policy.parameters()]
                result = train_batch(TrainingEnvironment(session.env, session), session.policy, session.optimizer, size)
                session.updated(result, before)
            self.assertEqual(
                {key: value for key, value in result.items() if key != "states_by_episode"},
                {key: value for key, value in plain_result.items() if key != "states_by_episode"},
            )
            for actual_episode, expected_episode in zip(
                result["states_by_episode"], plain_result["states_by_episode"]
            ):
                for actual_state, expected_state in zip(actual_episode, expected_episode):
                    torch.testing.assert_close(actual_state, expected_state)
            self.assertEqual(plain_env.actions, session.env.actions)
            for actual, expected in zip(session.policy.parameters(), plain_policy.parameters()):
                self.assertTrue(torch.equal(actual, expected))
            scores = result["episode_scores"]
            self.assertEqual(len(scores), size)
            for i, advantage in enumerate(result["advantages"]):
                others = scores[:i] + scores[i+1:]
                self.assertAlmostEqual(advantage, (scores[i] - sum(others)/len(others))/plain_env.episode_limit)
            events = output.getvalue()
            self.assertIn(f"AURA_NN\tstart\t{size}\t777\t11\t", events)
            self.assertIn("AURA_NN\tlayer\t17\t12\tTanh", events)
            self.assertIn(f"AURA_NN\tlayer\t7\t{len(CONTROLLED_ACTION_KEYS)}\tTanh", events)
            self.assertIn("AURA_NN\tsampling\t0.4\t", events)
            self.assertIn(f"AURA_NN\tcredit\t{DISCOUNT_FACTOR}", events)
            self.assertEqual(events.count("AURA_NN\tepisode\t"), size)
            objective_events = [line.split("\t") for line in events.splitlines()
                                if line.startswith("AURA_NN\tobjective\t")]
            self.assertEqual([float(event[6]) for event in objective_events], result["episode_returns"])
            self.assertEqual(events.count("AURA_NN\tphase\toptimizing"), 1)
            self.assertEqual(events.count("AURA_NN\ttensor\t"), len(list(plain_policy.parameters())))
            update = next(line.split("\t") for line in events.splitlines() if line.startswith("AURA_NN\tupdate"))
            self.assertEqual(float(update[4]), sum(scores)/len(scores))

    def test_invalid_configuration(self):
        for size in (0, 1, -1, 2.5):
            with self.assertRaises(ValueError):
                TrainingConfig(batch_size=size)
        with self.assertRaises(ValueError):
            outputs_to_action(torch.zeros(3))

    def test_simulator_contract(self):
        from python.aura_env import AuraEnv
        env = AuraEnv()
        state, _ = env.reset()
        policy = AuraPolicy(env.observation_size)
        self.assertEqual(policy(state).numel(), len(CONTROLLED_ACTION_KEYS))
        self.assertEqual(env.observation_size, OBSERVATION_SIZE)
        self.assertEqual(env.episode_limit, env.simulation.episode_limit)
        self.assertEqual(list(policy.action_transforms), list(CONTROLLED_ACTION_KEYS))

    def test_controller_uses_exported_limits(self):
        import tempfile
        import json
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory)/"action.json"
            specs = [dict(spec, minimum=-.11, maximum=.22) for spec in aura_python.action_specs]
            with patch("aura_python.action_specs", specs), patch("python.control_aura.ACTION_FILE", target):
                values = write_action(1., -1., 1., -1., 1., -1.)
            self.assertEqual(values, (.22, -.11, .22, -.11, .22, -.11) + (0.0,) * (len(specs) - 6))
            self.assertEqual(list(json.loads(target.read_text())), [spec["key"] for spec in specs])


if __name__ == "__main__":
    unittest.main()
