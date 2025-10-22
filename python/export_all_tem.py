import influxdb_client
import pandas as pd
from datetime import datetime

# InfluxDB configuration
url = "http://172.16.46.14:8086"
token = "my-token-1234567890abcdef"
org = "myorg"
bucket = "sensors"

# Initialize client
client = influxdb_client.InfluxDBClient(url=url, token=token, org=org)
query_api = client.query_api()

# Flux query
query = '''
from(bucket: "sensors")
  |> range(start: 0)
  |> filter(fn: (r) => r["_measurement"] == "cucumber")
  |> filter(fn: (r) => r["_field"] == "temp")
  |> filter(fn: (r) => r["host"] == "f199a2094793")
  |> aggregateWindow(every: 5m, fn: mean, createEmpty: false)
  |> yield(name: "mean")
'''

# Execute query and convert to DataFrame
result = query_api.query_data_frame(query=query, org=org)

# Save to CSV
result.to_csv("./sensors_temp_data.csv", index=False)

# Close client
client.close()