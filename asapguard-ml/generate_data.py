import pandas as pd
import numpy as np
from datetime import datetime, timedelta
import random

random.seed(42)
np.random.seed(42)

def generate_kitchen_data(days=28, interval_seconds=10):
    records = []
    start_time = datetime(2026, 1, 1, 6, 0, 0)
    current_time = start_time

    total_seconds = days * 24 * 3600
    steps = total_seconds // interval_seconds

    for i in range(steps):
        hour = current_time.hour

        # Cooking intensity by time of day
        if 6 <= hour < 9:      # breakfast rush
            intensity = random.uniform(0.4, 0.7)
        elif 9 <= hour < 11:   # mid morning slow
            intensity = random.uniform(0.05, 0.2)
        elif 11 <= hour < 14:  # lunch peak
            intensity = random.uniform(0.6, 1.0)
        elif 14 <= hour < 17:  # afternoon slow
            intensity = random.uniform(0.05, 0.15)
        elif 17 <= hour < 21:  # dinner peak
            intensity = random.uniform(0.55, 0.95)
        elif 21 <= hour < 23:  # closing
            intensity = random.uniform(0.05, 0.3)
        else:                   # closed
            intensity = 0.0

        # Sensor readings — relative to MQ sensor output range
        mq135 = int(30 + intensity * 420 + np.random.normal(0, 12))
        mq6   = int(10 + intensity * 200 + np.random.normal(0, 8))
        temp  = round(27 + intensity * 12 + np.random.normal(0, 0.5), 1)
        humid = round(55 + intensity * 25 + np.random.normal(0, 2), 1)

        # Clamp to realistic prototype ranges
        mq135 = max(20, min(500, mq135))
        mq6   = max(5,  min(250, mq6))
        temp  = max(25, min(45, temp))
        humid = max(40, min(98, humid))

        # Prototype thresholds — relative sensor range, not DOSH ppm
        if mq135 < 100:
            alert_level = "Normal"
            fan_tier = 0
        elif mq135 < 200:
            alert_level = "Warning"
            fan_tier = 1
        elif mq135 < 350:
            alert_level = "Critical"
            fan_tier = 2
        else:
            alert_level = "Hazardous"
            fan_tier = 3

        records.append({
            "timestamp": current_time.strftime("%Y-%m-%d %H:%M:%S"),
            "hour": hour,
            "mq135": mq135,
            "mq6": mq6,
            "temperature": temp,
            "humidity": humid,
            "intensity": round(intensity, 2),
            "alert_level": alert_level,
            "fan_tier": fan_tier
        })

        current_time += timedelta(seconds=interval_seconds)

    df = pd.DataFrame(records)
    df["aqi_rise_rate"] = df["mq135"].diff().fillna(0)
    return df

print("Generating 28 days of kitchen sensor data...")
df = generate_kitchen_data(days=28)

print(f"Total records: {len(df)}")
print(f"\nClass distribution:")
print(df["alert_level"].value_counts())
print(f"\nSensor value ranges:")
print(df[["mq135","mq6","temperature","humidity"]].describe().round(2))

df.to_csv("kitchen_sensor_data.csv", index=False)
print("\nSaved to kitchen_sensor_data.csv")
