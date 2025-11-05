from(bucket: "sensors")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r["_measurement"] == "cucumber")
  |> filter(fn: (r) => r["_field"] == "temp")
  |> filter(fn: (r) => r["host"] == "f199a2094793")
  |> keep(columns: ["_time", "_value", "sensor_id"])
  |> last()  // เอาเฉพาะค่าล่าสุดของแต่ละ sensor_id
  |> group(columns: ["sensor_id"])
  |> yield(name: "latest")