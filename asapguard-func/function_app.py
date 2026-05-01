import azure.functions as func
import logging
import json
import joblib
import numpy as np
from datetime import datetime, timezone
from azure.data.tables import TableServiceClient
import os

app = func.FunctionApp()

# Load ML model once at startup
MODEL_PATH = os.path.join(os.path.dirname(__file__), "asapguard_rf_model.pkl")
rf_model = joblib.load(MODEL_PATH)

ALERT_MAP = {0: "Normal", 1: "Warning", 2: "Critical", 3: "Hazardous"}

@app.function_name(name="IngestSensorData")
@app.event_hub_message_trigger(
    arg_name="event",
    event_hub_name="asapguard-hub",
    connection="IoTHubConnectionString"
)
def ingest_sensor_data(event: func.EventHubEvent):
    logging.info("Message received from IoT Hub")
    payload = json.loads(event.get_body().decode("utf-8"))
    conn_str = os.environ["AzureWebJobsStorage"]
    table_service = TableServiceClient.from_connection_string(conn_str)

    # Write raw sensor reading
    sensor_client = table_service.get_table_client("SensorReadings")
    timestamp = datetime.now(timezone.utc).strftime("%Y%m%d%H%M%S%f")
    entity = {
        "PartitionKey": payload.get("device_id", "asapguard-01"),
        "RowKey": timestamp,
        "mq135": payload.get("mq135", 0),
        "mq6": payload.get("mq6", 0),
        "temperature": payload.get("temperature", 0.0),
        "humidity": payload.get("humidity", 0.0),
        "timestamp": timestamp
    }
    sensor_client.create_entity(entity=entity)

    # Get last 6 readings for rolling features
    rows = list(sensor_client.query_entities(
        f"PartitionKey eq '{payload.get('device_id', 'asapguard-01')}'"
    ))
    rows.sort(key=lambda x: x["RowKey"], reverse=True)
    last6 = rows[:6]

    mq135_vals = [r["mq135"] for r in last6]
    mq6_vals   = [r["mq6"] for r in last6]
    temp_vals  = [r["temperature"] for r in last6]

    mq135_rolling_mean = float(np.mean(mq135_vals))
    mq135_rolling_std  = float(np.std(mq135_vals)) if len(mq135_vals) > 1 else 0.0
    mq6_rolling_mean   = float(np.mean(mq6_vals))
    temp_rolling_mean  = float(np.mean(temp_vals))

    # AQI rise rate
    if len(mq135_vals) >= 2:
        aqi_rise_rate = float(mq135_vals[0] - mq135_vals[1])
    else:
        aqi_rise_rate = 0.0

    hour = datetime.now(timezone.utc).hour

    # ML inference
    features = np.array([[
        payload.get("mq6", 0),
        payload.get("temperature", 0.0),
        payload.get("humidity", 0.0),
        hour,
        aqi_rise_rate,
        mq135_rolling_mean,
        mq135_rolling_std,
        mq6_rolling_mean,
        temp_rolling_mean
    ]])

    fan_tier = int(rf_model.predict(features)[0])
    alert_level = ALERT_MAP[fan_tier]

    # Write prediction
    pred_client = table_service.get_table_client("Predictions")
    pred_entity = {
        "PartitionKey": payload.get("device_id", "asapguard-01"),
        "RowKey": timestamp,
        "fan_tier": fan_tier,
        "alert_level": alert_level,
        "predicted_aqi": mq135_rolling_mean,
        "aqi_rise_rate": aqi_rise_rate,
        "timestamp": timestamp
    }
    pred_client.create_entity(entity=pred_entity)
    logging.info(f"Prediction: fan_tier={fan_tier}, alert={alert_level}")


@app.function_name(name="GetLatestReadings")
@app.route(route="readings", auth_level=func.AuthLevel.ANONYMOUS)
def get_latest_readings(req: func.HttpRequest) -> func.HttpResponse:
    logging.info("Dashboard requested latest readings")
    try:
        conn_str = os.environ["AzureWebJobsStorage"]
        table_service = TableServiceClient.from_connection_string(conn_str)

        sensor_client = table_service.get_table_client("SensorReadings")
        sensor_rows = list(sensor_client.list_entities())
        sensor_rows.sort(key=lambda x: x["RowKey"], reverse=True)
        latest = sensor_rows[0] if sensor_rows else {}

        pred_client = table_service.get_table_client("Predictions")
        pred_rows = list(pred_client.list_entities())
        pred_rows.sort(key=lambda x: x["RowKey"], reverse=True)
        latest_pred = pred_rows[0] if pred_rows else {}

        response = {
            "latest_reading": {
                "device_id": latest.get("PartitionKey", ""),
                "timestamp": latest.get("timestamp", ""),
                "mq135": latest.get("mq135", 0),
                "mq6": latest.get("mq6", 0),
                "temperature": latest.get("temperature", 0.0),
                "humidity": latest.get("humidity", 0.0),
            },
            "latest_prediction": {
                "predicted_aqi": latest_pred.get("predicted_aqi", 0),
                "fan_tier": latest_pred.get("fan_tier", 0),
                "alert_level": latest_pred.get("alert_level", "Normal"),
                "aqi_rise_rate": latest_pred.get("aqi_rise_rate", 0),
                "timestamp": latest_pred.get("timestamp", "")
            },
            "history": [
                {
                    "timestamp": row.get("timestamp", ""),
                    "mq135": row.get("mq135", 0),
                    "mq6": row.get("mq6", 0),
                    "temperature": row.get("temperature", 0.0),
                    "humidity": row.get("humidity", 0.0),
                }
                for row in sensor_rows[:30]
            ]
        }

        return func.HttpResponse(
            json.dumps(response),
            mimetype="application/json",
            status_code=200
        )

    except Exception as e:
        logging.error(f"Error: {str(e)}")
        return func.HttpResponse(
            json.dumps({"error": str(e)}),
            mimetype="application/json",
            status_code=500
        )
