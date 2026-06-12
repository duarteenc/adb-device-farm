import { useEffect, useState } from 'react';
import { useStore } from '../stores/useStore';
import { api } from '../utils/api';

const MinimalScreenWall = () => {
  const { devices } = useStore();
  const [screenshots, setScreenshots] = useState<Map<string, string>>(new Map());
  const [selectedDevice, setSelectedDevice] = useState<string | null>(null);
  const [loading, setLoading] = useState<Set<string>>(new Set());

  const onlineDevices = devices.filter(d => d.status === 'online');

  useEffect(() => {
    if (onlineDevices.length === 0) return;

    // Start auto-capture for all online devices
    const startCaptures = async () => {
      for (const device of onlineDevices) {
        try {
          await api.screenshot.startAutoCapture(device.serial, 1500); // 1.5 second interval
        } catch (error) {
          console.error(`Failed to start auto-capture for ${device.serial}:`, error);
        }
      }
    };

    startCaptures();

    // Refresh screenshots
    const interval = setInterval(async () => {
      for (const device of onlineDevices) {
        try {
          const base64 = await api.screenshot.getCached(device.serial);
          if (base64) {
            setScreenshots(prev => new Map(prev).set(device.serial, base64));
            setLoading(prev => {
              const newSet = new Set(prev);
              newSet.delete(device.serial);
              return newSet;
            });
          }
        } catch (error) {
          console.error(`Failed to get screenshot for ${device.serial}:`, error);
        }
      }
    }, 1500);

    setLoading(new Set(onlineDevices.map(d => d.serial)));

    return () => {
      clearInterval(interval);
      onlineDevices.forEach(device => {
        api.screenshot.stopAutoCapture(device.serial);
      });
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [onlineDevices.length]);

  const handleScreenClick = async (serial: string) => {
    setSelectedDevice(serial);
    try {
      await api.scrcpy.openLiveView(serial, {
        maxSize: 1080,
        maxFps: 60,
        bitRate: '8M',
      });
    } catch (error) {
      console.error('Failed to open live view:', error);
    }
  };

  if (onlineDevices.length === 0) {
    return (
      <div className="flex items-center justify-center h-screen">
        <div className="text-center">
          <div className="text-gray-600 text-sm mb-2">NO DEVICES ONLINE</div>
          <div className="text-gray-500 text-xs">Connect devices to start monitoring</div>
        </div>
      </div>
    );
  }

  return (
    <div className="h-screen bg-black overflow-hidden">
      {/* Minimal Header */}
      <div className="fixed top-0 left-0 right-0 bg-black bg-opacity-90 backdrop-blur-sm border-b border-gray-900 z-50">
        <div className="flex items-center justify-between px-4 py-2">
          <div className="flex items-center space-x-4">
            <div className="text-red-500 font-bold text-sm">ADB FARM</div>
            <div className="text-gray-500 text-xs">
              {onlineDevices.length} ONLINE
            </div>
          </div>
          <div className="text-gray-600 text-xs font-mono">
            {new Date().toLocaleTimeString('en-US', { hour12: false })}
          </div>
        </div>
      </div>

      {/* Screen Grid - Minimal Design */}
      <div className="pt-12 h-full overflow-y-auto">
        <div className="grid grid-cols-2 md:grid-cols-3 lg:grid-cols-4 xl:grid-cols-5 2xl:grid-cols-6 gap-1 p-1">
          {onlineDevices.map((device) => {
            const screenshot = screenshots.get(device.serial);
            const isLoading = loading.has(device.serial);
            const isSelected = selectedDevice === device.serial;

            return (
              <div
                key={device.serial}
                onClick={() => handleScreenClick(device.serial)}
                className={`
                  relative aspect-[9/16] bg-black cursor-pointer group
                  ${isSelected ? 'ring-2 ring-red-500' : 'ring-1 ring-gray-900 hover:ring-gray-700'}
                  transition-all duration-150
                `}
              >
                {/* Screenshot */}
                {isLoading && !screenshot ? (
                  <div className="absolute inset-0 flex items-center justify-center">
                    <div className="w-8 h-8 border-2 border-t-red-500 border-gray-800 rounded-full animate-spin" />
                  </div>
                ) : screenshot ? (
                  <img
                    src={`data:image/png;base64,${screenshot}`}
                    alt=""
                    className="w-full h-full object-cover"
                  />
                ) : (
                  <div className="absolute inset-0 flex items-center justify-center text-gray-700 text-xs">
                    NO SIGNAL
                  </div>
                )}

                {/* Minimal Overlay on Hover */}
                <div className="absolute inset-0 bg-black bg-opacity-0 group-hover:bg-opacity-40 transition-opacity duration-150">
                  <div className="absolute top-0 left-0 right-0 bg-gradient-to-b from-black to-transparent p-2 opacity-0 group-hover:opacity-100 transition-opacity duration-150">
                    <div className="text-white text-xs font-mono truncate">
                      {device.model || device.serial.split(':')[0]}
                    </div>
                  </div>

                  <div className="absolute bottom-0 left-0 right-0 bg-gradient-to-t from-black to-transparent p-2 opacity-0 group-hover:opacity-100 transition-opacity duration-150">
                    <div className="flex items-center justify-between text-xs">
                      <span className="text-gray-400 font-mono">
                        {device.serial.split(':')[0].split('.').slice(-1)[0]}
                      </span>
                      {device.battery !== undefined && (
                        <span className="text-gray-400">
                          {device.battery}%
                        </span>
                      )}
                    </div>
                  </div>
                </div>

                {/* Status Indicator */}
                <div className="absolute top-1 right-1">
                  <div className="w-1.5 h-1.5 bg-green-500 rounded-full" />
                </div>
              </div>
            );
          })}
        </div>
      </div>

      {/* Minimal Footer Info */}
      <div className="fixed bottom-0 left-0 right-0 bg-black bg-opacity-90 backdrop-blur-sm border-t border-gray-900 z-50">
        <div className="px-4 py-1 text-center text-gray-600 text-xs">
          Click any screen for full control • Auto-refresh 1.5s
        </div>
      </div>
    </div>
  );
};

export default MinimalScreenWall;
