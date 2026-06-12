import { useState } from 'react';
import type { Device } from '@shared/types';
import { api } from '../utils/api';
import { useStore } from '../stores/useStore';

interface DeviceCardProps {
  device: Device;
  isSelected: boolean;
  onToggleSelect: () => void;
}

const DeviceCard = ({ device, isSelected, onToggleSelect }: DeviceCardProps) => {
  const { activeLiveViews, scrcpyAvailable, addActiveLiveView, removeActiveLiveView } = useStore();
  const [isOpening, setIsOpening] = useState(false);
  const isLiveViewActive = activeLiveViews.has(device.serial);

  const handleLiveView = async (e: React.MouseEvent) => {
    e.stopPropagation(); // Prevent card selection

    if (isLiveViewActive) {
      // Close live view
      const success = await api.scrcpy.closeLiveView(device.serial);
      if (success) {
        removeActiveLiveView(device.serial);
      }
      return;
    }

    // Open live view
    setIsOpening(true);
    try {
      const success = await api.scrcpy.openLiveView(device.serial, {
        maxSize: 1080,
        maxFps: 30,
        bitRate: '4M',
      });

      if (success) {
        addActiveLiveView(device.serial);
      }
    } catch (error) {
      console.error('Failed to open live view:', error);
    } finally {
      setIsOpening(false);
    }
  };

  const getStatusColor = () => {
    switch (device.status) {
      case 'online':
        return 'bg-green-500';
      case 'offline':
        return 'bg-yellow-500';
      case 'unauthorized':
        return 'bg-orange-500';
      case 'disconnected':
        return 'bg-gray-500';
      default:
        return 'bg-gray-500';
    }
  };

  const getStatusText = () => {
    switch (device.status) {
      case 'online':
        return 'Online';
      case 'offline':
        return 'Offline';
      case 'unauthorized':
        return 'Unauthorized';
      case 'disconnected':
        return 'Disconnected';
      default:
        return 'Unknown';
    }
  };

  return (
    <div
      onClick={onToggleSelect}
      className={`bg-dark-surface border rounded-lg p-4 cursor-pointer transition-all duration-200 ${
        isSelected
          ? 'border-neon-red shadow-neon-md'
          : 'border-dark-border hover:border-neon-red hover:shadow-neon-sm'
      }`}
    >
      <div className="flex items-start justify-between mb-3">
        <div className="flex items-center space-x-2">
          <div className={`w-3 h-3 rounded-full ${getStatusColor()} animate-pulse-glow`}></div>
          <span className="text-sm font-medium text-gray-300">{getStatusText()}</span>
        </div>

        {isSelected && (
          <div className="w-5 h-5 bg-neon-red rounded flex items-center justify-center">
            <svg className="w-3 h-3 text-white" fill="currentColor" viewBox="0 0 20 20">
              <path
                fillRule="evenodd"
                d="M16.707 5.293a1 1 0 010 1.414l-8 8a1 1 0 01-1.414 0l-4-4a1 1 0 011.414-1.414L8 12.586l7.293-7.293a1 1 0 011.414 0z"
                clipRule="evenodd"
              />
            </svg>
          </div>
        )}
      </div>

      <div className="space-y-2">
        <div>
          <div className="text-xs text-gray-500">Serial</div>
          <div className="text-sm text-white font-mono truncate" title={device.serial}>
            {device.serial}
          </div>
        </div>

        {device.model && (
          <div>
            <div className="text-xs text-gray-500">Model</div>
            <div className="text-sm text-white">{device.model}</div>
          </div>
        )}

        {device.manufacturer && (
          <div>
            <div className="text-xs text-gray-500">Manufacturer</div>
            <div className="text-sm text-white">{device.manufacturer}</div>
          </div>
        )}

        {device.androidVersion && (
          <div>
            <div className="text-xs text-gray-500">Android</div>
            <div className="text-sm text-white">{device.androidVersion}</div>
          </div>
        )}

        {device.resolution && (
          <div>
            <div className="text-xs text-gray-500">Resolution</div>
            <div className="text-sm text-white">{device.resolution}</div>
          </div>
        )}

        {device.battery !== undefined && (
          <div>
            <div className="text-xs text-gray-500">Battery</div>
            <div className="text-sm text-white">{device.battery}%</div>
          </div>
        )}

        <div className="pt-2 border-t border-dark-border">
          <div className="text-xs text-gray-500">Connection</div>
          <div className="text-sm text-neon-red font-medium uppercase">{device.connectionType}</div>
        </div>

        {/* Live View Button */}
        {device.status === 'online' && (
          <div className="pt-3">
            <button
              onClick={handleLiveView}
              disabled={isOpening || !scrcpyAvailable}
              className={`w-full px-3 py-2 text-sm font-medium rounded-lg transition-all duration-200 ${
                isLiveViewActive
                  ? 'bg-green-600 text-white hover:bg-green-700 shadow-lg'
                  : 'bg-neon-red text-white hover:bg-neon-red-light shadow-neon-sm hover:shadow-neon-md'
              } disabled:opacity-50 disabled:cursor-not-allowed`}
              title={!scrcpyAvailable ? 'scrcpy not installed' : undefined}
            >
              {isOpening ? (
                '⏳ Opening...'
              ) : isLiveViewActive ? (
                '✓ Live View Active'
              ) : (
                '📱 Open Live View'
              )}
            </button>
            {!scrcpyAvailable && (
              <p className="text-xs text-yellow-400 mt-1 text-center">
                Install scrcpy first
              </p>
            )}
          </div>
        )}
      </div>
    </div>
  );
};

export default DeviceCard;
