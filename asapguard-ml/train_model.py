import pandas as pd
import numpy as np
from sklearn.ensemble import RandomForestClassifier
from sklearn.model_selection import train_test_split, cross_val_score
from sklearn.metrics import classification_report, confusion_matrix
import joblib
import json

print("Loading data...")
df = pd.read_csv("kitchen_sensor_data.csv")

# Add rolling features — patterns over time
df["mq135_rolling_mean"] = df["mq135"].rolling(6).mean().fillna(df["mq135"])
df["mq135_rolling_std"]  = df["mq135"].rolling(6).std().fillna(0)
df["mq6_rolling_mean"]   = df["mq6"].rolling(6).mean().fillna(df["mq6"])
df["temp_rolling_mean"]  = df["temperature"].rolling(6).mean().fillna(df["temperature"])

# Remove mq135 raw value — force model to learn from rate + pattern
FEATURES = [
    "mq6",                  # gas level
    "temperature",          # cooking heat
    "humidity",             # moisture
    "hour",                 # time of day
    "aqi_rise_rate",        # how fast AQI is rising
    "mq135_rolling_mean",   # trend over last 60 seconds
    "mq135_rolling_std",    # volatility
    "mq6_rolling_mean",     # gas trend
    "temp_rolling_mean",    # heat trend
]
TARGET = "fan_tier"

X = df[FEATURES]
y = df[TARGET]

X_train, X_test, y_train, y_test = train_test_split(
    X, y, test_size=0.2, random_state=42, stratify=y
)

print(f"Training samples: {len(X_train)}")
print(f"Test samples:     {len(X_test)}")
print(f"Features used:    {FEATURES}")

print("\nTraining Random Forest...")
rf = RandomForestClassifier(
    n_estimators=100,
    max_depth=15,
    min_samples_split=10,
    random_state=42,
    n_jobs=-1,
    class_weight="balanced"
)
rf.fit(X_train, y_train)

print("\nEvaluating...")
y_pred = rf.predict(X_test)

print("\nClassification Report:")
print(classification_report(y_test, y_pred,
    target_names=["Tier 0 Normal", "Tier 1 Warning", "Tier 2 Critical", "Tier 3 Hazardous"]))

print("Confusion Matrix:")
print(confusion_matrix(y_test, y_pred))

print("\nFeature Importances:")
for feat, imp in sorted(zip(FEATURES, rf.feature_importances_), key=lambda x: -x[1]):
    print(f"  {feat}: {imp:.4f}")

# Cross validation
print("\nCross Validation (5-fold):")
cv_scores = cross_val_score(rf, X, y, cv=5, scoring="f1_macro", n_jobs=-1)
print(f"  F1 scores: {cv_scores.round(4)}")
print(f"  Mean F1:   {cv_scores.mean():.4f} (+/- {cv_scores.std():.4f})")

joblib.dump(rf, "asapguard_rf_model.pkl")
print("\nModel saved to asapguard_rf_model.pkl")

metadata = {
    "features": FEATURES,
    "target": TARGET,
    "classes": {"0": "Normal", "1": "Warning", "2": "Critical", "3": "Hazardous"},
    "thresholds_note": "Relative to MQ sensor output range. Prototype only. Not DOSH ppm.",
    "cv_mean_f1": float(cv_scores.mean()),
    "cv_std_f1": float(cv_scores.std())
}
with open("model_metadata.json", "w") as f:
    json.dump(metadata, f, indent=2)
print("Metadata saved to model_metadata.json")
