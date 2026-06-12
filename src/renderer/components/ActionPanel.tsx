import { useState } from 'react';
import { useStore } from '../stores/useStore';
import { api } from '../utils/api';

const ActionPanel = () => {
  const { selectedDevices, devices } = useStore();
  const [activeTab, setActiveTab] = useState<'input' | 'app' | 'system'>('input');

  // Input tab state
  const [tapX, setTapX] = useState('');
  const [tapY, setTapY] = useState('');
  const [textInput, setTextInput] = useState('');

  // App tab state
  const [packageName, setPackageName] = useState('');

  const selectedDevicesList = Array.from(selectedDevices)
    .map((serial) => devices.find((d) => d.serial === serial))
    .filter((d) => d?.status === 'online');

  const hasSelection = selectedDevicesList.length > 0;

  const executeOnSelected = async (action: (serial: string) => Promise<unknown>) => {
    for (const device of selectedDevicesList) {
      if (device) {
        try {
          await action(device.serial);
        } catch (error) {
          console.error(`Action failed for ${device.serial}:`, error);
        }
      }
    }
  };

  const handleTap = async () => {
    const x = parseInt(tapX);
    const y = parseInt(tapY);
    if (isNaN(x) || isNaN(y)) return;

    await executeOnSelected((serial) => api.adb.tap(serial, x, y));
  };

  const handleInputText = async () => {
    if (!textInput) return;
    await executeOnSelected((serial) => api.adb.inputText(serial, textInput));
  };

  const handlePressKey = async (keycode: string | number) => {
    await executeOnSelected((serial) => api.adb.pressKey(serial, keycode));
  };

  const handleStartApp = async () => {
    if (!packageName) return;
    await executeOnSelected((serial) => api.adb.startApp(serial, packageName));
  };

  const handleStopApp = async () => {
    if (!packageName) return;
    await executeOnSelected((serial) => api.adb.stopApp(serial, packageName));
  };

  const handleClearAppData = async () => {
    if (!packageName) return;
    if (!confirm(`Clear data for ${packageName} on ${selectedDevicesList.length} device(s)?`)) return;
    await executeOnSelected((serial) => api.adb.clearAppData(serial, packageName));
  };

  const handleReboot = async () => {
    if (!confirm(`Reboot ${selectedDevicesList.length} device(s)?`)) return;
    await executeOnSelected((serial) => api.adb.reboot(serial));
  };

  return (
    <div className="bg-dark-surface border border-dark-border rounded-lg p-6">
      <h2 className="text-xl font-semibold text-neon-red mb-4">Quick Actions</h2>

      {!hasSelection && (
        <div className="text-center py-8 text-gray-400">
          Select at least one online device to perform actions
        </div>
      )}

      {hasSelection && (
        <>
          <div className="flex space-x-2 mb-6 border-b border-dark-border">
            {(['input', 'app', 'system'] as const).map((tab) => (
              <button
                key={tab}
                onClick={() => setActiveTab(tab)}
                className={`px-4 py-2 font-medium capitalize transition-all duration-200 ${
                  activeTab === tab
                    ? 'text-neon-red border-b-2 border-neon-red'
                    : 'text-gray-400 hover:text-neon-red'
                }`}
              >
                {tab}
              </button>
            ))}
          </div>

          <div className="space-y-4">
            {activeTab === 'input' && (
              <>
                <div className="space-y-3">
                  <h3 className="text-sm font-medium text-gray-300">Tap Screen</h3>
                  <div className="flex space-x-2">
                    <input
                      type="number"
                      placeholder="X"
                      value={tapX}
                      onChange={(e) => setTapX(e.target.value)}
                      className="flex-1 px-3 py-2 bg-dark-elevated border border-dark-border rounded-lg text-white focus:border-neon-red focus:outline-none"
                    />
                    <input
                      type="number"
                      placeholder="Y"
                      value={tapY}
                      onChange={(e) => setTapY(e.target.value)}
                      className="flex-1 px-3 py-2 bg-dark-elevated border border-dark-border rounded-lg text-white focus:border-neon-red focus:outline-none"
                    />
                    <button
                      onClick={handleTap}
                      className="px-4 py-2 bg-neon-red text-white rounded-lg hover:bg-neon-red-light transition-all duration-200"
                    >
                      Tap
                    </button>
                  </div>
                </div>

                <div className="space-y-3">
                  <h3 className="text-sm font-medium text-gray-300">Input Text</h3>
                  <div className="flex space-x-2">
                    <input
                      type="text"
                      placeholder="Text to input"
                      value={textInput}
                      onChange={(e) => setTextInput(e.target.value)}
                      className="flex-1 px-3 py-2 bg-dark-elevated border border-dark-border rounded-lg text-white focus:border-neon-red focus:outline-none"
                    />
                    <button
                      onClick={handleInputText}
                      className="px-4 py-2 bg-neon-red text-white rounded-lg hover:bg-neon-red-light transition-all duration-200"
                    >
                      Send
                    </button>
                  </div>
                </div>

                <div className="space-y-3">
                  <h3 className="text-sm font-medium text-gray-300">Key Events</h3>
                  <div className="grid grid-cols-3 gap-2">
                    <button
                      onClick={() => handlePressKey('KEYCODE_HOME')}
                      className="px-3 py-2 bg-dark-elevated border border-dark-border text-white rounded-lg hover:border-neon-red hover:bg-dark-surface transition-all duration-200"
                    >
                      Home
                    </button>
                    <button
                      onClick={() => handlePressKey('KEYCODE_BACK')}
                      className="px-3 py-2 bg-dark-elevated border border-dark-border text-white rounded-lg hover:border-neon-red hover:bg-dark-surface transition-all duration-200"
                    >
                      Back
                    </button>
                    <button
                      onClick={() => handlePressKey('KEYCODE_APP_SWITCH')}
                      className="px-3 py-2 bg-dark-elevated border border-dark-border text-white rounded-lg hover:border-neon-red hover:bg-dark-surface transition-all duration-200"
                    >
                      Recents
                    </button>
                  </div>
                </div>
              </>
            )}

            {activeTab === 'app' && (
              <>
                <div className="space-y-3">
                  <h3 className="text-sm font-medium text-gray-300">Package Name</h3>
                  <input
                    type="text"
                    placeholder="com.example.app"
                    value={packageName}
                    onChange={(e) => setPackageName(e.target.value)}
                    className="w-full px-3 py-2 bg-dark-elevated border border-dark-border rounded-lg text-white focus:border-neon-red focus:outline-none"
                  />
                </div>

                <div className="grid grid-cols-2 gap-3">
                  <button
                    onClick={handleStartApp}
                    className="px-4 py-2 bg-neon-red text-white rounded-lg hover:bg-neon-red-light transition-all duration-200"
                  >
                    Start App
                  </button>
                  <button
                    onClick={handleStopApp}
                    className="px-4 py-2 bg-dark-elevated border border-dark-border text-white rounded-lg hover:border-neon-red hover:bg-dark-surface transition-all duration-200"
                  >
                    Stop App
                  </button>
                  <button
                    onClick={handleClearAppData}
                    className="col-span-2 px-4 py-2 bg-orange-600 text-white rounded-lg hover:bg-orange-700 transition-all duration-200"
                  >
                    Clear App Data
                  </button>
                </div>
              </>
            )}

            {activeTab === 'system' && (
              <div className="space-y-3">
                <button
                  onClick={handleReboot}
                  className="w-full px-4 py-2 bg-red-600 text-white rounded-lg hover:bg-red-700 transition-all duration-200"
                >
                  Reboot Device(s)
                </button>
              </div>
            )}
          </div>

          <div className="mt-6 pt-4 border-t border-dark-border">
            <div className="text-xs text-gray-400">
              Actions will be executed on {selectedDevicesList.length} device(s)
            </div>
          </div>
        </>
      )}
    </div>
  );
};

export default ActionPanel;
