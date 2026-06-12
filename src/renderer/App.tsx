import { useEffect } from 'react';
import { useStore } from './stores/useStore';
import { api } from './utils/api';
import Header from './components/Header';
import DeviceGrid from './components/DeviceGrid';
import ActionPanel from './components/ActionPanel';
import LogPanel from './components/LogPanel';
import StarryBackground from './components/StarryBackground';

function App() {
  const { setDevices, addLog, clearLogs, setLoading } = useStore();

  useEffect(() => {
    // Initial device discovery
    const discoverDevices = async () => {
      setLoading(true);
      try {
        const devices = await api.devices.discover();
        setDevices(devices);
      } catch (error) {
        console.error('Failed to discover devices:', error);
      } finally {
        setLoading(false);
      }
    };

    discoverDevices();

    // Setup log listeners
    api.logs.onNewLog((log) => {
      addLog(log);
    });

    api.logs.onClearLogs(() => {
      clearLogs();
    });

    // Auto-refresh devices every 5 seconds
    const interval = setInterval(async () => {
      try {
        const devices = await api.devices.getAll();
        setDevices(devices);
      } catch (error) {
        console.error('Failed to refresh devices:', error);
      }
    }, 5000);

    // Load existing logs
    api.logs.getAll(100).then((logs) => {
      logs.forEach((log) => addLog(log));
    });

    return () => {
      clearInterval(interval);
    };
  }, [addLog, clearLogs, setDevices, setLoading]);

  return (
    <div className="relative min-h-screen bg-dark-bg text-white overflow-hidden">
      <StarryBackground />

      <div className="relative z-10">
        <Header />

        <main className="container mx-auto px-6 py-6 space-y-6">
          <DeviceGrid />

          <div className="grid grid-cols-1 lg:grid-cols-2 gap-6">
            <ActionPanel />
            <LogPanel />
          </div>
        </main>
      </div>
    </div>
  );
}

export default App;
