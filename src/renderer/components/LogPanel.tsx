import { useEffect, useRef } from 'react';
import { useStore } from '../stores/useStore';
import { api } from '../utils/api';
import type { LogEntry } from '@shared/types';

const LogPanel = () => {
  const { logs, clearLogs } = useStore();
  const logContainerRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    if (logContainerRef.current) {
      logContainerRef.current.scrollTop = logContainerRef.current.scrollHeight;
    }
  }, [logs]);

  const handleClearLogs = async () => {
    await api.logs.clear();
    clearLogs();
  };

  const getLevelColor = (level: LogEntry['level']) => {
    switch (level) {
      case 'error':
        return 'text-red-400';
      case 'warn':
        return 'text-yellow-400';
      case 'success':
        return 'text-green-400';
      case 'info':
      default:
        return 'text-blue-400';
    }
  };

  const getLevelIcon = (level: LogEntry['level']) => {
    switch (level) {
      case 'error':
        return '✕';
      case 'warn':
        return '⚠';
      case 'success':
        return '✓';
      case 'info':
      default:
        return 'ℹ';
    }
  };

  const formatTime = (timestamp: number) => {
    const date = new Date(timestamp);
    return date.toLocaleTimeString('en-US', { hour12: false });
  };

  return (
    <div className="bg-dark-surface border border-dark-border rounded-lg p-6 flex flex-col h-[500px]">
      <div className="flex items-center justify-between mb-4">
        <h2 className="text-xl font-semibold text-neon-red">Logs</h2>
        <button
          onClick={handleClearLogs}
          className="px-3 py-1 text-sm bg-dark-elevated border border-dark-border text-gray-400 rounded-lg hover:border-neon-red hover:text-neon-red transition-all duration-200"
        >
          Clear Logs
        </button>
      </div>

      <div
        ref={logContainerRef}
        className="flex-1 overflow-y-auto space-y-1 font-mono text-xs bg-dark-bg rounded-lg p-3 border border-dark-border"
      >
        {logs.length === 0 && (
          <div className="text-gray-500 text-center py-8">No logs yet</div>
        )}

        {logs.map((log, index) => (
          <div key={index} className="flex items-start space-x-2 hover:bg-dark-elevated px-2 py-1 rounded">
            <span className="text-gray-500 flex-shrink-0">{formatTime(log.timestamp)}</span>

            <span className={`${getLevelColor(log.level)} flex-shrink-0 font-bold w-4 text-center`}>
              {getLevelIcon(log.level)}
            </span>

            {log.context && (
              <span className="text-purple-400 flex-shrink-0">[{log.context}]</span>
            )}

            {log.deviceSerial && (
              <span className="text-neon-red flex-shrink-0">[{log.deviceSerial}]</span>
            )}

            <span className="text-gray-300 flex-1">{log.message}</span>
          </div>
        ))}
      </div>

      <div className="mt-3 text-xs text-gray-500">
        {logs.length} log entries
      </div>
    </div>
  );
};

export default LogPanel;
