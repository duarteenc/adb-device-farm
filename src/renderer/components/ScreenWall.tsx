import { useEffect, useState } from 'react';
import { useStore } from '../stores/useStore';
import { api } from '../utils/api';

const ScreenWall = () => {
  const { devices } = useStore();
  const [screenshots, setScreenshots] = useState<Map<string, string>>(new Map());
  const [loading, setLoading] = useState<Set<string>>(new Set());

  const onlineDevices = devices.filter(d => d.status === 'online');

  useEffect(() => {
    if (onlineDevices.length === 0) return;

    // Start auto-capture for all online devices
    const startCaptures = async () => {
      for (const device of onlineDevices) {
        try {
          await api.screenshot.startAutoCapture(device.serial, 2000); // 2 second interval
        } catch (error) {
          console.error(`Failed to start auto-capture for ${device.serial}:`, error);
        }
      }
    };

    startCaptures();

    // Refresh screenshots every 2 seconds
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
    }, 2000);

    // Mark all as loading initially
    setLoading(new Set(onlineDevices.map(d => d.serial)));

    return () => {
      clearInterval(interval);
      // Stop auto-capture for all devices
      onlineDevices.forEach(device => {
        api.screenshot.stopAutoCapture(device.serial);
      });
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [onlineDevices.length]);

  const handleScreenClick = async (serial: string) => {
    // Open fullscreen scrcpy view
    try {
      await api.scrcpy.openLiveView(serial, {
        maxSize: 1080,
        maxFps: 30,
        bitRate: '4M',
      });
    } catch (error) {
      console.error('Failed to open live view:', error);
    }
  };

  if (onlineDevices.length === 0) {
    return (
      <div className="bg-dark-surface border border-dark-border rounded-lg p-12 text-center">
        <div className="text-gray-400 text-lg">No online devices to display</div>
        <p className="text-gray-500 text-sm mt-2">
          Connect devices and ensure they are online to see live screens
        </p>
      </div>
    );
  }

  return (
    <div className="space-y-4">
      <div className="flex items-center justify-between">
        <div>
          <h2 className="text-xl font-semibold text-neon-red">Live Device Screens</h2>
          <p className="text-sm text-gray-400 mt-1">
            Updating every 2 seconds • Click any screen for full control
          </p>
        </div>
        <div className="text-sm text-gray-400">
          {onlineDevices.length} device{onlineDevices.length !== 1 ? 's' : ''} online
        </div>
      </div>

      <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 xl:grid-cols-4 2xl:grid-cols-5 gap-4">
        {onlineDevices.map((device) => {
          const screenshot = screenshots.get(device.serial);
          const isLoading = loading.has(device.serial);

          return (
            <div
              key={device.serial}
              onClick={() => handleScreenClick(device.serial)}
              className="bg-dark-surface border border-dark-border rounded-lg overflow-hidden cursor-pointer hover:border-neon-red hover:shadow-neon-md transition-all duration-200 group"
            >
              {/* Screen Display */}
              <div className="aspect-[9/16] bg-dark-elevated relative overflow-hidden">
                {isLoading && !screenshot ? (
                  <div className="absolute inset-0 flex items-center justify-center">
                    <div className="text-gray-500">
                      <div className="animate-spin rounded-full h-12 w-12 border-t-2 border-b-2 border-neon-red"></div>
                      <div className="text-xs mt-2">Loading...</div>
                    </div>
                  </div>
                ) : screenshot ? (
                  <>
                    <img
                      src={`data:image/png;base64,${screenshot}`}
                      alt={`Screen of ${device.serial}`}
                      className="w-full h-full object-contain"
                    />
                    <div className="absolute inset-0 bg-black bg-opacity-0 group-hover:bg-opacity-30 transition-all duration-200 flex items-center justify-center">
                      <div className="opacity-0 group-hover:opacity-100 transition-opacity duration-200">
                        <div className="bg-neon-red text-white px-4 py-2 rounded-lg font-medium">
                          🖱️ Click for Full Control
                        </div>
                      </div>
                    </div>
                  </>
                ) : (
                  <div className="absolute inset-0 flex items-center justify-center text-gray-500 text-sm">
                    No screenshot available
                  </div>
                )}
              </div>

              {/* Device Info */}
              <div className="p-3 border-t border-dark-border">
                <div className="text-sm font-medium text-white truncate" title={device.serial}>
                  {device.model || device.serial}
                </div>
                <div className="text-xs text-gray-400 truncate mt-1" title={device.serial}>
                  {device.serial}
                </div>
                {device.battery !== undefined && (
                  <div className="text-xs text-gray-500 mt-1">
                    🔋 {device.battery}%
                  </div>
                )}
              </div>
            </div>
          );
        })}
      </div>

      <div className="bg-dark-elevated border border-dark-border rounded-lg p-4">
        <p className="text-xs text-gray-400">
          <span className="text-yellow-400">💡 Tip:</span> Screens update automatically. Click any device to open full control with scrcpy.
          Close the scrcpy window to return to the wall view.
        </p>
      </div>
    </div>
  );
};

export default ScreenWall;
