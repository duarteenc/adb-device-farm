import { useState } from 'react';
import { useStore } from '../stores/useStore';
import { api } from '../utils/api';

const Header = () => {
  const { devices, setDevices, setLoading } = useStore();
  const [showConnectModal, setShowConnectModal] = useState(false);
  const [ip, setIp] = useState('');
  const [port, setPort] = useState('5555');

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

  const handleConnect = async () => {
    if (!ip) return;

    setLoading(true);
    try {
      const success = await api.devices.connect(ip, parseInt(port));
      if (success) {
        setShowConnectModal(false);
        setIp('');
        await handleRefresh();
      }
    } catch (error) {
      console.error('Failed to connect device:', error);
    } finally {
      setLoading(false);
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
              </div>

              <div className="flex space-x-3">
                <button
                  onClick={handleRefresh}
                  className="px-4 py-2 bg-dark-elevated border border-neon-red text-neon-red rounded-lg hover:bg-neon-red hover:text-white transition-all duration-200 shadow-neon-sm hover:shadow-neon-md"
                >
                  Refresh
                </button>

                <button
                  onClick={() => setShowConnectModal(true)}
                  className="px-4 py-2 bg-neon-red text-white rounded-lg hover:bg-neon-red-light transition-all duration-200 shadow-neon-md hover:shadow-neon-lg"
                >
                  Connect Device
                </button>
              </div>
            </div>
          </div>
        </div>
      </header>

      {showConnectModal && (
        <div className="fixed inset-0 bg-black bg-opacity-70 flex items-center justify-center z-50">
          <div className="bg-dark-surface border border-neon-red rounded-lg p-6 w-96 shadow-neon-xl">
            <h2 className="text-xl font-bold text-neon-red mb-4">Connect Device via IP</h2>

            <div className="space-y-4">
              <div>
                <label className="block text-sm text-gray-400 mb-2">IP Address</label>
                <input
                  type="text"
                  value={ip}
                  onChange={(e) => setIp(e.target.value)}
                  placeholder="192.168.1.100"
                  className="w-full px-3 py-2 bg-dark-elevated border border-dark-border rounded-lg text-white focus:border-neon-red focus:outline-none"
                />
              </div>

              <div>
                <label className="block text-sm text-gray-400 mb-2">Port</label>
                <input
                  type="text"
                  value={port}
                  onChange={(e) => setPort(e.target.value)}
                  placeholder="5555"
                  className="w-full px-3 py-2 bg-dark-elevated border border-dark-border rounded-lg text-white focus:border-neon-red focus:outline-none"
                />
              </div>

              <div className="flex space-x-3 pt-2">
                <button
                  onClick={handleConnect}
                  className="flex-1 px-4 py-2 bg-neon-red text-white rounded-lg hover:bg-neon-red-light transition-all duration-200"
                >
                  Connect
                </button>
                <button
                  onClick={() => {
                    setShowConnectModal(false);
                    setIp('');
                  }}
                  className="flex-1 px-4 py-2 bg-dark-elevated text-gray-400 rounded-lg hover:bg-dark-border transition-all duration-200"
                >
                  Cancel
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
