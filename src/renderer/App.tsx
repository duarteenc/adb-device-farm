import { useEffect } from 'react';
import { useStore } from './stores/useStore';
import { api } from './utils/api';
import MinimalScreenWall from './components/MinimalScreenWall';

function App() {
  const { setDevices, addLog, clearLogs, setScrcpyAvailable, setActiveLiveViews } = useStore();

  useEffect(() => {
    // Initial device discovery
    const discoverDevices = async () => {
      try {
        const devices = await api.devices.discover();
        setDevices(devices);
      } catch (error) {
        console.error('Failed to discover devices:', error);
      }
    };

    discoverDevices();

    // Check scrcpy availability
    api.scrcpy.isAvailable().then((available) => {
      setScrcpyAvailable(available);
      if (!available) {
        console.warn('scrcpy is not available. Install from: https://github.com/Genymobile/scrcpy');
      }
    });

    // Setup log listeners
    api.logs.onNewLog((log) => {
      addLog(log);
    });

    api.logs.onClearLogs(() => {
      clearLogs();
    });

    // Auto-refresh devices and active live views every 5 seconds
    const interval = setInterval(async () => {
      try {
        const devices = await api.devices.getAll();
        setDevices(devices);

        // Update active live views
        const activeViews = await api.scrcpy.getActiveDevices();
        setActiveLiveViews(activeViews);
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
  }, [addLog, clearLogs, setDevices, setScrcpyAvailable, setActiveLiveViews]);

  return (
    <div className="bg-black text-white h-screen overflow-hidden">
      <MinimalScreenWall />
    </div>
  );
}

export default App;
