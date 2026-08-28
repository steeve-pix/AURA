from dataclasses import dataclass, field


@dataclass
class DisagreementRecord:
    step: int

    rule_goal: str
    rule_action: str
    rule_target: tuple[int, int] | None
    rule_predicted_value: float

    model_goal: str
    model_action: str
    model_target: tuple[int, int] | None
    model_predicted_value: float

    rule_actual_reward: float | None = None
    rule_counterfactual_reward: float | None = None
    model_counterfactual_reward: float | None = None

    def rule_prediction_error(self) -> float | None:
        if self.rule_actual_reward is None:
            return None

        return abs(
            self.rule_predicted_value
            - self.rule_actual_reward
        )

    def model_claimed_advantage(self) -> float:
        return (
            self.model_predicted_value
            - self.rule_predicted_value
        )

    def rule_actual_vs_model_prediction(self) -> float | None:
        if self.rule_actual_reward is None:
            return None

        return (
            self.rule_actual_reward
            - self.model_predicted_value
        )

    def actual_model_advantage(self) -> float | None:
        if (
                self.rule_counterfactual_reward is None
                or self.model_counterfactual_reward is None
        ):
            return None

        return (
            self.model_counterfactual_reward
            - self.rule_counterfactual_reward
        )


@dataclass(frozen=True)
class DisagreementStatistics:
    count: int
    rule_prediction_mae: float | None
    average_claimed_advantage: float | None
    rule_actual_exceeds_model_prediction: int
    model_prediction_exceeds_rule_actual: int
    counterfactual_count: int = 0
    model_actual_wins: int = 0
    rule_actual_wins: int = 0
    actual_ties: int = 0
    average_actual_model_advantage: float | None = None
    rule_counterfactual_prediction_mae: float | None = None
    model_counterfactual_prediction_mae: float | None = None


@dataclass
class DisagreementAnalysis:
    records: list[DisagreementRecord] = field(default_factory=list)

    def begin(
            self,
            *,
            step: int,
            rule_goal: str,
            rule_action: dict,
            rule_predicted_value: float,
            model_goal: str,
            model_action: dict,
            model_predicted_value: float,
    ) -> DisagreementRecord:
        return DisagreementRecord(
            step=step,
            rule_goal=rule_goal,
            rule_action=rule_action["action"],
            rule_target=self._target(rule_action),
            rule_predicted_value=rule_predicted_value,
            model_goal=model_goal,
            model_action=model_action["action"],
            model_target=self._target(model_action),
            model_predicted_value=model_predicted_value,
        )

    def complete(
            self,
            record: DisagreementRecord,
            *,
            rule_actual_reward: float,
    ) -> DisagreementRecord:
        record.rule_actual_reward = rule_actual_reward
        if record not in self.records:
            self.records.append(record)
        return record

    def complete_counterfactual(
            self,
            record: DisagreementRecord,
            *,
            rule_actual_reward: float,
            model_actual_reward: float,
    ) -> DisagreementRecord:
        record.rule_counterfactual_reward = rule_actual_reward
        record.model_counterfactual_reward = model_actual_reward

        if record not in self.records:
            self.records.append(record)

        return record

    def statistics(self) -> DisagreementStatistics:
        completed = [
            record
            for record in self.records
            if record.rule_actual_reward is not None
        ]

        counterfactual = [
            record
            for record in self.records
            if record.actual_model_advantage() is not None
        ]

        counterfactual_count = len(counterfactual)

        if not completed and not counterfactual:
            return DisagreementStatistics(0, None, None, 0, 0)

        return DisagreementStatistics(
            count=len(completed),
            rule_prediction_mae=(None if not completed else sum(
                record.rule_prediction_error() or 0.0
                for record in completed
            ) / len(completed)),
            average_claimed_advantage=(None if not completed else sum(
                record.model_claimed_advantage()
                for record in completed
            ) / len(completed)),
            rule_actual_exceeds_model_prediction=sum(
                1
                for record in completed
                if (record.rule_actual_vs_model_prediction() or 0.0) > 0.0
            ),
            model_prediction_exceeds_rule_actual=sum(
                1
                for record in completed
                if (record.rule_actual_vs_model_prediction() or 0.0) < 0.0
            ),
            counterfactual_count=counterfactual_count,
            model_actual_wins=sum(
                1 for record in counterfactual
                if (record.actual_model_advantage() or 0.0) > 0.0
            ),
            rule_actual_wins=sum(
                1 for record in counterfactual
                if (record.actual_model_advantage() or 0.0) < 0.0
            ),
            actual_ties=sum(
                1 for record in counterfactual
                if (record.actual_model_advantage() or 0.0) == 0.0
            ),
            average_actual_model_advantage=(
                None if not counterfactual else sum(
                    record.actual_model_advantage() or 0.0
                    for record in counterfactual
                ) / counterfactual_count
            ),
            rule_counterfactual_prediction_mae=(
                None if not counterfactual else sum(
                    abs(
                        record.rule_predicted_value
                        - (record.rule_counterfactual_reward or 0.0)
                    )
                    for record in counterfactual
                ) / counterfactual_count
            ),
            model_counterfactual_prediction_mae=(
                None if not counterfactual else sum(
                    abs(
                        record.model_predicted_value
                        - (record.model_counterfactual_reward or 0.0)
                    )
                    for record in counterfactual
                ) / counterfactual_count
            ),
        )

    @staticmethod
    def _target(action: dict) -> tuple[int, int] | None:
        target = action.get("target")

        if target is None:
            return None

        return target[0], target[1]
