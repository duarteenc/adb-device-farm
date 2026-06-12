import { useState } from 'react';
import { useStore } from '../stores/useStore';
import { api } from '../utils/api';

const Header = () => {
  const { devices, setDevices, setLoading } = useStore();
  const [showConnectModal, setShowConnectModal] = useState(false);
  const [ipFrom, setIpFrom] = useState('192.168.100.1');
  const [ipTo, setIpTo] = useState('192.168.100.255');
  const [port, setPort] = useState('5555');
  const [scanning, setScanning] = useState(false);
  const [scanProgress, setScanProgress] = useState({ current: 0, total: 0 });

  const handleRefresh = async () => {
    setLoading(true);
    try {
      const newDevices = await api.devices.discover();
      setDevices(newDevices);
    } catch (error) {
      console.error('Failed to refresh devices:', error);
    } finally {
      setLoading(false);
    }
  };

  const parseIP = (ip: string): number[] => {
    return ip.split('.').map(Number);
  };

  const generateIPRange = (from: string, to: string): string[] => {
    const fromParts = parseIP(from);
    const toParts = parseIP(to);
    const ips: string[] = [];

    // Simple range generation for the last octet
    const baseIP = fromParts.slice(0, 3).join('.');
    const startLast = fromParts[3];
    const endLast = toParts[3];

    for (let i = startLast; i <= endLast; i++) {
      ips.push(`${baseIP}.${i}`);
    }

    return ips;
  };

  const handleConnect = async () => {
    if (!ipFrom || !ipTo) return;

    // Close modal immediately
    setShowConnectModal(false);

    setScanning(true);
    setLoading(true);

    try {
      const ips = generateIPRange(ipFrom, ipTo);
      setScanProgress({ current: 0, total: ips.length });

      // Scan IPs in batches of 5 for better performance
      const batchSize = 5;
      for (let i = 0; i < ips.length; i += batchSize) {
        const batch = ips.slice(i, i + batchSize);

        await Promise.all(
          batch.map(async (ip) => {
            try {
              await api.devices.connect(ip, parseInt(port));
              setScanProgress(prev => ({ ...prev, current: prev.current + 1 }));
            } catch {
              // Silent fail for unreachable IPs
              setScanProgress(prev => ({ ...prev, current: prev.current + 1 }));
            }
          })
        );

        // Small delay between batches to avoid overwhelming the system
        await new Promise(resolve => setTimeout(resolve, 100));
      }

      // Refresh devices after scanning
      await handleRefresh();
    } catch (error) {
      console.error('Failed to scan IP range:', error);
    } finally {
      setScanning(false);
      setLoading(false);
      setScanProgress({ current: 0, total: 0 });
    }
  };

  const onlineCount = devices.filter((d) => d.status === 'online').length;
  const totalCount = devices.length;

  return (
    <>
      <header className="bg-dark-surface border-b border-dark-border shadow-lg">
        <div className="container mx-auto px-6 py-4">
          <div className="flex items-center justify-between">
            <div>
              <h1 className="text-3xl font-bold text-neon-red drop-shadow-neon-md">
                ADB Device Farm
              </h1>
              <p className="text-gray-400 text-sm mt-1">
                Professional Android Device Management
              </p>
            </div>

            <div className="flex items-center space-x-6">
              <div className="text-right">
                <div className="text-sm text-gray-400">Connected Devices</div>
                <div className="text-2xl font-bold text-neon-red">
                  {onlineCount} <span className="text-gray-500">/ {totalCount}</span>
                </div>
                {scanning && (
                  <div className="text-xs text-yellow-400 mt-1">
                    Scanning... {scanProgress.current}/{scanProgress.total}
                  </div>
                )}
              </div>

              <div className="flex space-x-3">
                <button
                  onClick={handleRefresh}
                  disabled={scanning}
                  className="px-4 py-2 bg-dark-elevated border border-neon-red text-neon-red rounded-lg hover:bg-neon-red hover:text-white transition-all duration-200 shadow-neon-sm hover:shadow-neon-md disabled:opacity-50 disabled:cursor-not-allowed"
                >
                  Refresh
                </button>

                <button
                  onClick={() => setShowConnectModal(true)}
                  disabled={scanning}
                  className="px-4 py-2 bg-neon-red text-white rounded-lg hover:bg-neon-red-light transition-all duration-200 shadow-neon-md hover:shadow-neon-lg disabled:opacity-50 disabled:cursor-not-allowed"
                >
                  {scanning ? 'Scanning...' : 'Scan IP Range'}
                </button>
              </div>
            </div>
          </div>
        </div>
      </header>

      {showConnectModal && (
        <div className="fixed inset-0 bg-black bg-opacity-70 flex items-center justify-center z-50">
          <div className="bg-dark-surface border border-neon-red rounded-lg p-6 w-[500px] shadow-neon-xl">
            <h2 className="text-xl font-bold text-neon-red mb-2">Scan IP Range</h2>
            <p className="text-sm text-gray-400 mb-4">
              Scan a range of IP addresses to find ADB devices on your local network
            </p>

            <div className="space-y-4">
              <div className="grid grid-cols-2 gap-4">
                <div>
                  <label className="block text-sm text-gray-400 mb-2">IP Desde:</label>
                  <input
                    type="text"
                    value={ipFrom}
                    onChange={(e) => setIpFrom(e.target.value)}
                    placeholder="192.168.100.1"
                    className="w-full px-3 py-2 bg-dark-elevated border border-dark-border rounded-lg text-white focus:border-neon-red focus:outline-none"
                  />
                </div>

                <div>
                  <label className="block text-sm text-gray-400 mb-2">IP Hasta:</label>
                  <input
                    type="text"
                    value={ipTo}
                    onChange={(e) => setIpTo(e.target.value)}
                    placeholder="192.168.100.255"
                    className="w-full px-3 py-2 bg-dark-elevated border border-dark-border rounded-lg text-white focus:border-neon-red focus:outline-none"
                  />
                </div>
              </div>

              <div>
                <label className="block text-sm text-gray-400 mb-2">Puerto ADB</label>
                <input
                  type="text"
                  value={port}
                  onChange={(e) => setPort(e.target.value)}
                  placeholder="5555"
                  className="w-full px-3 py-2 bg-dark-elevated border border-dark-border rounded-lg text-white focus:border-neon-red focus:outline-none"
                />
              </div>

              <div className="bg-dark-elevated border border-dark-border rounded-lg p-3">
                <p className="text-xs text-gray-400">
                  <span className="text-yellow-400">⚠️ Nota:</span> El escaneo puede tomar algunos segundos.
                  El modal se cerrará automáticamente y verás el progreso en el header.
                </p>
              </div>

              <div className="flex space-x-3 pt-2">
                <button
                  onClick={handleConnect}
                  className="flex-1 px-4 py-2 bg-neon-red text-white rounded-lg hover:bg-neon-red-light transition-all duration-200 font-medium"
                >
                  Iniciar Escaneo
                </button>
                <button
                  onClick={() => {
                    setShowConnectModal(false);
                  }}
                  className="flex-1 px-4 py-2 bg-dark-elevated text-gray-400 rounded-lg hover:bg-dark-border transition-all duration-200"
                >
                  Cancelar
                </button>
              </div>
            </div>
          </div>
        </div>
      )}
    </>
  );
};

export default Header;
