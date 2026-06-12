import { useStore } from '../stores/useStore';
import DeviceCard from './DeviceCard';

const DeviceGrid = () => {
  const { devices, selectedDevices, toggleDeviceSelection, selectAll, clearSelection } = useStore();

  if (devices.length === 0) {
    return (
      <div className="bg-dark-surface border border-dark-border rounded-lg p-12 text-center">
        <div className="text-gray-400 text-lg">No devices detected</div>
        <p className="text-gray-500 text-sm mt-2">
          Connect devices via USB or click "Connect Device" to add network devices
        </p>
      </div>
    );
  }

  return (
    <div className="space-y-4">
      <div className="flex items-center justify-between">
        <h2 className="text-xl font-semibold text-neon-red">Devices</h2>

        <div className="flex space-x-3">
          <button
            onClick={selectAll}
            className="px-3 py-1 text-sm bg-dark-elevated border border-dark-border text-gray-400 rounded-lg hover:border-neon-red hover:text-neon-red transition-all duration-200"
          >
            Select All
          </button>
          <button
            onClick={clearSelection}
            className="px-3 py-1 text-sm bg-dark-elevated border border-dark-border text-gray-400 rounded-lg hover:border-neon-red hover:text-neon-red transition-all duration-200"
          >
            Clear Selection
          </button>
          <div className="px-3 py-1 text-sm bg-dark-elevated border border-neon-red text-neon-red rounded-lg">
            {selectedDevices.size} selected
          </div>
        </div>
      </div>

      <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 xl:grid-cols-4 gap-4">
        {devices.map((device) => (
          <DeviceCard
            key={device.serial}
            device={device}
            isSelected={selectedDevices.has(device.serial)}
            onToggleSelect={() => toggleDeviceSelection(device.serial)}
          />
        ))}
      </div>
    </div>
  );
};

export default DeviceGrid;
