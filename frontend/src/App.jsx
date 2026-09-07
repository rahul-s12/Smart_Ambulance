import { useEffect, useState } from "react";
import "./App.css";
import {
  LineChart,
  Line,
  XAxis,
  YAxis,
  CartesianGrid,
  Tooltip,
  ResponsiveContainer,
} from "recharts";

function App() {
  const [vitals, setVitals] = useState(null);
  const [error, setError] = useState(null);
  const [lastUpdated, setLastUpdated] = useState(null);
  const [history, setHistory] = useState([]);

  const chartData = [...history]
  .reverse()
  .map((reading) => ({
    time: new Date(reading.receivedTimestamp).toLocaleTimeString(),
    heartRate: reading.heartRate,
  }));

  useEffect(() => {
    const fetchVitals = async () => {
      try {
        const [latestResponse, historyResponse] = await Promise.all([
          fetch("http://127.0.0.1:8000/patients/P001/latest"),
          fetch("http://127.0.0.1:8000/patients/P001/history"),
        ]);

        if (!latestResponse.ok) {
          throw new Error("Failed to fetch latest patient data");
        }

        if (!historyResponse.ok) {
          throw new Error("Failed to fetch patient history");
        }

        const latestData = await latestResponse.json();
        const historyData = await historyResponse.json();

        setVitals(latestData);

        if (Array.isArray(historyData)) {
          setHistory(historyData);
        }

        setLastUpdated(new Date());
        setError(null);

      } catch (error) {
        console.error(error);
        setError(error.message);
      }
    };

    // Fetch immediately
    fetchVitals();

    // Then fetch every second
    const interval = setInterval(fetchVitals, 1000);

    // Stop polling when the component is removed
    return () => clearInterval(interval);
  }, []);

  return (
    <div className="dashboard">

      <header className="header">
        <div>
          <h1>Smart Ambulance</h1>
          <p>Real-time Patient Monitoring System</p>
        </div>

        <div className="connection">
          <span className="status-dot"></span>

          <div>
            <strong>System Online</strong>

            {lastUpdated && (
              <small>
                Last update: {lastUpdated.toLocaleTimeString()}
              </small>
            )}
          </div>
        </div>
      </header>


      <section className="patient-info">

        <div>
          <span>Patient ID</span>
          <strong>{vitals?.patientId || "Loading..."}</strong>
        </div>

        <div>
          <span>Ambulance ID</span>
          <strong>{vitals?.ambulanceId || "Loading..."}</strong>
        </div>

        <div>
          <span>Status</span>
          <strong className="normal">Normal</strong>
        </div>

      </section>


      <section className="vitals">

        <div className="vital-card">
          <div className="vital-icon">❤️</div>
          <div>
            <span>Heart Rate</span>
            <strong>{vitals?.heartRate ?? "--"} <small>bpm</small></strong>
          </div>
        </div>


        <div className="vital-card">
          <div className="vital-icon">🫁</div>
          <div>
            <span>SpO₂</span>
            <strong>{vitals?.spo2 ?? "--"} <small>%</small></strong>
          </div>
        </div>


        <div className="vital-card">
          <div className="vital-icon">🌡️</div>
          <div>
            <span>Temperature</span>
            <strong>{vitals?.temperature ?? "--"} <small>°C</small></strong>
          </div>
        </div>

      </section>


      <section className="history-card">

        <div className="section-title">
          <div>
            <h2>Patient Vital History</h2>
            <p>Recent readings</p>
          </div>
        </div>

        <div className="chart-container">

          <ResponsiveContainer width="100%" height={300}>

            <LineChart data={chartData}>

              <CartesianGrid strokeDasharray="3 3" />

              <XAxis dataKey="time" />

              <YAxis />

              <Tooltip />

              <Line
                type="monotone"
                dataKey="heartRate"
                stroke="#dc2626"
                strokeWidth={3}
                dot={false}
              />

            </LineChart>

          </ResponsiveContainer>

        </div>

      </section>


      <footer>
        Smart Ambulance · Patient Monitoring System
      </footer>

    </div>
  );
}

export default App;