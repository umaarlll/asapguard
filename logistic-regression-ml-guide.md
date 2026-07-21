# Logistic Regression ML Guide for AsapGuard

## Purpose

Logistic Regression can extend AsapGuard with an advisory probability model that answers:

> Will the system reach Stage 1 or Stage 2 within the next five minutes?

The model must not replace the existing rule-based controller or the immediate MQ-6 Stage 2 override. Until it has been validated across multiple independent sessions, its output should be displayed in Grafana as an additional forecast only.

## Recommended architecture

```text
Sensors -> ESP32 rule controller -> relays/fans
              |
              +-> telemetry -> Logistic Regression risk score -> Grafana/Telegram
```

Authoritative control remains:

```text
MQ-6 trigger       -> immediate Stage 2
Existing rules     -> Fan 1/Fan 2 control
Logistic Regression -> advisory probability
```

## Prediction target

Create a binary target for each telemetry row:

```text
alert_within_5min = 1  if Stage 1 or Stage 2 occurs within the next five minutes
alert_within_5min = 0  otherwise
```

With the current sample interval of approximately three seconds, five minutes is roughly 100 future samples. Generate this target independently inside each data-collection session so one session cannot label another.

If the future fan stage is used to create the target, the model is learning to anticipate the existing controller. This is suitable for demonstrating a supervised ML workflow, but it does not establish an independent safety model.

## Input features

Recommended initial features:

```text
temperatureC
humidityFiltered
humidityDelta
humiditySlopePerMin
mq135Filtered
mq135Delta
mq135SlopePerMin
```

Do not train with these fields because they contain or closely reproduce the existing controller's answer:

```text
predictionState
predictionReason
predictedFanStage
fanStageCommand
fan1On
fan2On
mq135Projected5Min
humidityProjected5Min
```

Keep `mq6Triggered` outside the model as a direct safety override. Do not allow a low ML probability to suppress an MQ-6 or rule-based alert.

## Data collection

Collect multiple independent sessions covering:

- Stable clean-air baselines.
- Natural changes in occupied and unoccupied rooms.
- Humidity stimuli and recovery.
- Smoke/VOC stimuli and recovery.
- MQ-6 tests or safe digital simulations.
- Different rooms, days, baseline levels, and airflow conditions.

Add a `sessionId` to every recording. Split training and testing by complete session, not by random telemetry row. Adjacent three-second readings are highly correlated; randomly placing neighboring rows in both training and testing would produce unrealistically optimistic results.

When using one long chronological dataset, use time-ordered validation such as scikit-learn's [`TimeSeriesSplit`](https://scikit-learn.org/stable/modules/generated/sklearn.model_selection.TimeSeriesSplit.html).

## Training example

```python
from sklearn.pipeline import make_pipeline
from sklearn.preprocessing import StandardScaler
from sklearn.linear_model import LogisticRegression

FEATURES = [
    "temperatureC",
    "humidityFiltered",
    "humidityDelta",
    "humiditySlopePerMin",
    "mq135Filtered",
    "mq135Delta",
    "mq135SlopePerMin",
]

X_train = train_data[FEATURES]
y_train = train_data["alert_within_5min"]

model = make_pipeline(
    StandardScaler(),
    LogisticRegression(
        class_weight="balanced",
        max_iter=1000,
    ),
)

model.fit(X_train, y_train)

risk_probability = model.predict_proba(X_test)[:, 1]
```

`StandardScaler` prevents fields with larger numerical ranges from dominating the optimization. `class_weight="balanced"` can help when alert samples are much rarer than normal samples.

Official references:

- [scikit-learn LogisticRegression](https://scikit-learn.org/stable/modules/generated/sklearn.linear_model.LogisticRegression.html)
- [scikit-learn StandardScaler](https://scikit-learn.org/stable/modules/generated/sklearn.preprocessing.StandardScaler.html)
- [scikit-learn precision-recall metrics](https://scikit-learn.org/stable/api/sklearn.metrics.html)

## Evaluation

Do not rely on accuracy alone. A model that always predicts normal may have high accuracy when alerts are rare.

Report at least:

- Alert recall: how many real upcoming alerts were detected.
- Precision: how many model warnings were correct.
- False-alert rate.
- Precision-recall curve.
- Confusion matrix.
- Performance on completely unseen sessions.

Select the probability threshold from the validation results. For example, `0.5` is not automatically the best threshold for a safety-oriented warning system.

## Backend deployment

The safest first deployment is shadow mode in a private backend or offline notebook. Add advisory output fields such as:

```text
mlRiskProbability
mlPredictedAlert
mlModelVersion
```

Grafana can display `mlRiskProbability` as a percentage gauge and compare it with `predictedFanStage`, `fanStageCommand`, and the existing rule-based ETA.

Because the Azure Storage path batches telemetry roughly once per minute, backend ML scoring is delayed and must not control the fans.

## ESP32 deployment

After validation, Logistic Regression is small enough to run directly on the ESP32. Export the trained scaler means/scales plus the model coefficients and intercept.

```cpp
float logit = intercept;

for (int i = 0; i < FEATURE_COUNT; i++) {
  float standardized = (features[i] - means[i]) / scales[i];
  logit += weights[i] * standardized;
}

float probability = 1.0f / (1.0f + expf(-logit));
```

The ESP32 calculation must use the exact same feature order, scaler values, and model version as Python.

Recommended rollout:

1. Train and validate offline.
2. Run in shadow mode without changing fan commands.
3. Compare ML predictions with rule outcomes across unseen sessions.
4. Display the probability in Grafana.
5. Consider edge deployment only after documenting recall and false-alert performance.

## Estimating seconds to a threshold

Binary Logistic Regression predicts probability, not an exact number of seconds. Continue using the API's existing slope-based fields:

```text
mq135Stage1EtaSeconds
mq135Stage2EtaSeconds
humidityStage1EtaSeconds
nextThresholdEtaSeconds
nextThresholdTarget
```

An ML alternative is to train separate binary classifiers for several horizons, such as 30, 60, 120, and 300 seconds. The earliest horizon whose probability crosses its validated warning threshold becomes an approximate ML time band, not an exact ETA.

## Safety boundary

AsapGuard is a classroom prototype. Logistic Regression output must never cancel or downgrade:

- An active-low MQ-6 gas trigger.
- A direct Stage 1 or Stage 2 rule.
- A manual safety intervention.

Local ESP32 rules and physical shutdown procedures remain the authoritative protection path.
