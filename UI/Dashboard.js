import React, { useState, useEffect } from "react";
import mqtt from "mqtt";
import {
  LineChart, Line, XAxis, YAxis, CartesianGrid, Tooltip, ResponsiveContainer
} from "recharts";

export default function Dashboard() {
  const [temperature, setTemperature] = useState("--");
  const [humidity, setHumidity] = useState("--");
  const [relayState, setRelayState] = useState("OFF");
  const [autoMode, setAutoMode] = useState("ON"); // default ON
  const [oledState, setOledState] = useState("ON"); // default ON
  const [targetTemp, setTargetTemp] = useState("");
  const [espStatus, setEspStatus] = useState("OFFLINE");

  const [temperatureHistory, setTemperatureHistory] = useState([]); // <-- store past readings
  const [client, setClient] = useState(null);

  useEffect(() => {
    const mqttClient = mqtt.connect("ws://broker.kerembilgicer.com:8083/mqtt");
    setClient(mqttClient);

    mqttClient.on("connect", () => {
      console.log("Connected to MQTT broker");

      // Subscribe to topics
      mqttClient.subscribe("esp32/sensor/temperature");
      mqttClient.subscribe("esp32/sensor/humidity");
      mqttClient.subscribe("esp32/target/temperature");
      mqttClient.subscribe("esp32/device/status");
      mqttClient.subscribe("esp32/device/availability");

      // Force defaults on load
      mqttClient.publish("esp32/control/commands", "AUTO_ON");
      mqttClient.publish("esp32/control/commands", "OLED_ON");

      // Request current status and target temperature
      mqttClient.publish("esp32/control/commands", "STATUS");
      mqttClient.publish("esp32/control/commands", "GET_TEMP");
    });

    mqttClient.on("message", (topic, message) => {
      const msg = message.toString();

      if (topic === "esp32/sensor/temperature") {
        try {
          const obj = JSON.parse(msg);
          if (obj.temperature !== undefined) {
            const tempValue = obj.temperature.toFixed(1);
            setTemperature(tempValue + " °C");

            // push into history
            const newEntry = {
              time: new Date().toLocaleTimeString(), // label
              value: parseFloat(tempValue),
            };
            setTemperatureHistory((prev) => {
              const updated = [...prev, newEntry];
              return updated.slice(-50); // keep last 50 points
            });
          }
        } catch {
          console.warn("Invalid temperature payload:", msg);
        }
      } else if (topic === "esp32/sensor/humidity") {
        try {
          const obj = JSON.parse(msg);
          if (obj.humidity !== undefined) {
            setHumidity(obj.humidity.toFixed(1) + " %");
          }
        } catch {
          console.warn("Invalid humidity payload:", msg);
        }
      } else if (topic === "esp32/target/temperature") {
        const val = parseFloat(msg);
        if (!isNaN(val)) setTargetTemp(val.toFixed(1));
      } else if (topic === "esp32/device/status") {
        try {
          const obj = JSON.parse(msg);
          if (obj.relay !== undefined) setRelayState(obj.relay === "on" ? "ON" : "OFF");
          if (obj.auto !== undefined) setAutoMode(obj.auto === "on" ? "ON" : "OFF");
          if (obj.oled !== undefined) setOledState(obj.oled === "on" ? "ON" : "OFF");
        } catch {
          console.warn("Invalid status payload:", msg);
        }
      } else if (topic === "esp32/device/availability") {
        setEspStatus(msg.toUpperCase());
      }
    });

    mqttClient.on("error", (err) => {
      console.error("MQTT error:", err);
    });

    return () => mqttClient.end();
  }, []);

  // Target temperature input
  const [inputTemp, setInputTemp] = useState("");
  const handleTargetChange = (e) => setInputTemp(e.target.value);
  const handleTargetSubmit = (e) => {
    e.preventDefault();
    if (client && inputTemp) {
      client.publish("esp32/control/commands", `SET_TEMP:${inputTemp}`);
      setInputTemp("");
    }
  };

  // Relay manual controls (only works if Auto Mode is OFF)
  const handleRelayOn = () => {
    if (autoMode === "OFF") {
      setRelayState("ON");
      client && client.publish("esp32/control/commands", "RELAY_ON");
    }
  };
  const handleRelayOff = () => {
    if (autoMode === "OFF") {
      setRelayState("OFF");
      client && client.publish("esp32/control/commands", "RELAY_OFF");
    }
  };

  // Auto Mode toggle
  const handleAutoOn = () => {
    setAutoMode("ON");
    client && client.publish("esp32/control/commands", "AUTO_ON");
  };
  const handleAutoOff = () => {
    setAutoMode("OFF");
    client && client.publish("esp32/control/commands", "AUTO_OFF");
  };

  // OLED controls
  const handleOledOn = () => {
    setOledState("ON");
    client && client.publish("esp32/control/commands", "OLED_ON");
  };
  const handleOledOff = () => {
    setOledState("OFF");
    client && client.publish("esp32/control/commands", "OLED_OFF");
  };

  return (
    <div className="dashboard">
      <div className="card">
        <h3>ESP32 Status</h3>
        <p style={{ color: espStatus === "ONLINE" ? "green" : "red", fontWeight: "bold" }}>
          {espStatus}
        </p>
      </div>

      <div className="card">
        <h3>Temperature</h3>
        <p>{temperature}</p>
      </div>

      <div className="card">
        <h3>Humidity</h3>
        <p>{humidity}</p>
      </div>

      <div className="card">
        <h3>Temperature History</h3>
        <ResponsiveContainer width="100%" height={300}>
          <LineChart data={temperatureHistory}>
            <CartesianGrid strokeDasharray="3 3" />
            <XAxis dataKey="time" />
            <YAxis domain={["auto", "auto"]} unit="°C" />
            <Tooltip />
            <Line type="monotone" dataKey="value" stroke="#2563eb" dot={false} />
          </LineChart>
        </ResponsiveContainer>
      </div>

      <div className="card">
        <h3>System</h3>
        <p>Relay: {relayState}</p>
        <p>Auto Mode: {autoMode}</p>
        <p>OLED: {oledState}</p>
        <div className="buttons">
          <button onClick={handleRelayOn} disabled={autoMode === "ON"}>Relay ON</button>
          <button onClick={handleRelayOff} disabled={autoMode === "ON"}>Relay OFF</button>
          <button onClick={handleAutoOn}>Auto ON</button>
          <button onClick={handleAutoOff}>Auto OFF</button>
          <button onClick={handleOledOn}>OLED ON</button>
          <button onClick={handleOledOff}>OLED OFF</button>
        </div>
      </div>

      <div className="card">
        <h3>Target Temperature</h3>
        <p>{targetTemp ? targetTemp + " °C" : "--"}</p>
        <form onSubmit={handleTargetSubmit}>
          <input
            type="number"
            value={inputTemp}
            onChange={handleTargetChange}
            placeholder="°C"
          />
          <button type="submit">Set</button>
        </form>
      </div>
    </div>
  );
}
