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


@dataclass(frozen=True)
class DisagreementStatistics:
    count: int
    rule_prediction_mae: float | None
    average_claimed_advantage: float | None
    rule_actual_exceeds_model_prediction: int
    model_prediction_exceeds_rule_actual: int


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
        self.records.append(record)
        return record

    def statistics(self) -> DisagreementStatistics:
        completed = [
            record
            for record in self.records
            if record.rule_actual_reward is not None
        ]

        if not completed:
            return DisagreementStatistics(
                count=0,
                rule_prediction_mae=None,
                average_claimed_advantage=None,
                rule_actual_exceeds_model_prediction=0,
                model_prediction_exceeds_rule_actual=0,
            )

        return DisagreementStatistics(
            count=len(completed),
            rule_prediction_mae=sum(
                record.rule_prediction_error() or 0.0
                for record in completed
            ) / len(completed),
            average_claimed_advantage=sum(
                record.model_claimed_advantage()
                for record in completed
            ) / len(completed),
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
        )

    @staticmethod
    def _target(action: dict) -> tuple[int, int] | None:
        target = action.get("target")

        if target is None:
            return None

        return target[0], target[1]
