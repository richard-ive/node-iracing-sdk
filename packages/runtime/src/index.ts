import { EventEmitter } from 'events';
import path from 'path';
import type {
  IRacingClientOptions,
  TelemetryData,
  TelemetryValue,
  TelemetryVarHeader,
  SessionInfoObject,
  SessionUpdate,
  IRacingConstants
} from 'node-iracing-sdk';

interface NativeBinding {
  constants?: IRacingConstants;
  waitForData(timeoutMs: number): boolean;
  isConnected(): boolean;
  getStatusId(): number;
  getSessionInfoUpdateCount(): number;
  wasSessionInfoUpdated(): boolean;
  getSessionInfoObj(): SessionInfoObject | null;
  readVars(names: string[]): TelemetryData;
  readAllVars(): TelemetryData | null;
  getVarHeaders(): TelemetryVarHeader[];
  getVarValue(name: string, entry?: number | null): TelemetryValue;
  broadcastMsg(msg: number, var1: number | string, var2: number, var3?: number): void;
}

/**
 * Resolve and load the native binding from prebuilds or local build locations.
 * @returns The loaded native binding module.
 * @throws The last load error if no candidate path loads successfully.
 */
const binding: NativeBinding = (() => {
  let lastError: unknown;
  try {
    const load = require('node-gyp-build') as (dir: string) => NativeBinding;
    return load(path.join(__dirname, '..'));
  } catch (error) {
    lastError = error;
  }
  const candidates = ['./build/Release/irsdk_native.node', './build/Debug/irsdk_native.node'];
  // Attempt both release and debug outputs, keeping the last failure for context.
  for (const candidate of candidates) {
    try {
      return require(candidate) as NativeBinding;
    } catch (error) {
      lastError = error;
    }
  }
  // Re-throw the last error so callers can see the most relevant failure.
  throw lastError;
})();

/**
 * Provide an empty numeric enum table for missing native constants.
 * @returns A new empty mapping object.
 */
const emptyEnum = (): Record<string, number> => ({});
// Use native constants when available; otherwise default to empty tables.
const constants: IRacingConstants = binding.constants ?? {
  BroadcastMsg: emptyEnum(),
  ChatCommandMode: emptyEnum(),
  PitCommandMode: emptyEnum(),
  TelemCommandMode: emptyEnum(),
  FFBCommandMode: emptyEnum(),
  CameraState: emptyEnum(),
  ReplaySearchMode: emptyEnum(),
  ReplayPositionMode: emptyEnum(),
  ReplayStateMode: emptyEnum(),
  ReloadTexturesMode: emptyEnum(),
  VideoCaptureMode: emptyEnum(),
  CameraFocusMode: emptyEnum()
};

class IRacingClient extends EventEmitter {
  private _pollIntervalMs: number;
  private _waitTimeoutMs: number;
  private _telemetryVars: string[];
  private _useAllTelemetry: boolean;
  private _emitSessionOnConnect: boolean;
  private _timer: NodeJS.Timeout | null;
  private _connected: boolean;
  private _lastSessionUpdate: number;

  /**
   * Create a new telemetry client with optional polling configuration.
   * @param options.pollIntervalMs Poll interval in milliseconds.
   * @param options.waitTimeoutMs Wait timeout in milliseconds passed to the native wait.
   * @param options.telemetryVariables Names of telemetry variables to read on each tick.
   * @param options.emitSessionOnConnect Emit session payload immediately on connect.
   * @returns A new IRacingClient instance.
   */
  constructor(options: IRacingClientOptions = {}) {
    super();

    const {
      pollIntervalMs = 16,
      waitTimeoutMs = 0,
      telemetryVariables,
      emitSessionOnConnect = true
    } = options;
    const hasTelemetryOptions = Object.prototype.hasOwnProperty.call(options, 'telemetryVariables');

    // Validate and normalize options to known-safe defaults.
    this._pollIntervalMs = Number.isFinite(pollIntervalMs) ? pollIntervalMs : 16;
    this._waitTimeoutMs = Number.isFinite(waitTimeoutMs) ? waitTimeoutMs : 0;
    this._telemetryVars = Array.isArray(telemetryVariables) ? telemetryVariables.slice() : [];
    this._useAllTelemetry = !hasTelemetryOptions;
    this._emitSessionOnConnect = emitSessionOnConnect !== false;

    // Initialize runtime state.
    this._timer = null;
    this._connected = false;
    this._lastSessionUpdate = -1;
  }

  /**
   * Override the telemetry variables to read on each poll.
   * @param names List of telemetry variable names.
   * @returns void
   */
  setTelemetryVariables(names: string[]): void {
    // Copy the array to avoid external mutation.
    this._telemetryVars = Array.isArray(names) ? names.slice() : [];
    this._useAllTelemetry = false;
  }

  /**
   * Query whether iRacing is currently connected.
   * @returns True if connected.
   */
  isConnected(): boolean {
    return binding.isConnected();
  }

  /**
   * Read the native iRacing status id.
   * @returns Numeric status id.
   */
  getStatusId(): number {
    return binding.getStatusId();
  }

  /**
   * Retrieve the latest session info as a structured object.
   * @returns Session info object or null if unavailable.
   */
  getSessionInfoObj(): SessionInfoObject | null {
    return binding.getSessionInfoObj();
  }

  /**
   * Read telemetry variables by name.
   * @param names Optional list of names; defaults to configured list.
   * @returns Telemetry data mapping.
   */
  readVars(names?: string[]): TelemetryData {
    // Fall back to the configured names if none are provided.
    const vars = Array.isArray(names) ? names : this._telemetryVars;
    return binding.readVars(vars);
  }

  /**
   * Read all available telemetry variables.
   * @returns Telemetry data mapping or null when unavailable.
   */
  readAllVars(): TelemetryData | null {
    return binding.readAllVars();
  }

  /**
   * Get telemetry variable headers (name, type, count, etc).
   * @returns Array of variable headers.
   */
  getVarHeaders(): TelemetryVarHeader[] {
    return binding.getVarHeaders();
  }

  /**
   * Read a single telemetry variable value.
   * @param name Telemetry variable name.
   * @param entry Optional array index for multi-value variables.
   * @returns The telemetry value.
   */
  getVarValue(name: string, entry?: number | null): TelemetryValue {
    return binding.getVarValue(name, entry);
  }

  /**
   * Send a raw broadcast message with optional third parameter.
   * @param msg Broadcast message id.
   * @param var1 First parameter (number or string depending on message).
   * @param var2 Second parameter.
   * @param var3 Optional third parameter.
   * @returns void
   */
  broadcastMsg(msg: number, var1: number | string, var2: number, var3?: number): void {
    // Use the 3-argument native call only when a valid third value is provided.
    if (Number.isFinite(var3)) {
      binding.broadcastMsg(msg, var1, var2, var3);
      return;
    }
    binding.broadcastMsg(msg, var1, var2);
  }

  /**
   * Send a broadcast message that expects a float value parameter.
   * @param msg Broadcast message id.
   * @param var1 First parameter (number or string depending on message).
   * @param value Float value parameter.
   * @returns void
   */
  broadcastMsgFloat(msg: number, var1: number | string, value: number): void {
    binding.broadcastMsg(msg, var1, value);
  }

  /**
   * Switch camera by car position.
   * @param carPos Car position index.
   * @param group Camera group.
   * @param camera Camera index.
   * @returns void
   */
  switchCameraByPos(carPos: number, group: number, camera: number): void {
    this.broadcastMsg(constants.BroadcastMsg.CamSwitchPos, carPos, group, camera);
  }

  /**
   * Switch camera by driver number.
   * @param driverNum Driver number or string identifier.
   * @param group Camera group.
   * @param camera Camera index.
   * @returns void
   */
  switchCameraByNum(driverNum: number | string, group: number, camera: number): void {
    this.broadcastMsg(constants.BroadcastMsg.CamSwitchNum, driverNum, group, camera);
  }

  /**
   * Set the current camera state.
   * @param cameraState Camera state enum value.
   * @returns void
   */
  setCameraState(cameraState: number): void {
    this.broadcastMsg(constants.BroadcastMsg.CamSetState, cameraState, 0);
  }

  /**
   * Control replay playback speed.
   * @param speed Playback speed.
   * @param slowMotion Slow motion flag value.
   * @returns void
   */
  replaySetPlaySpeed(speed: number, slowMotion: number = 0): void {
    this.broadcastMsg(constants.BroadcastMsg.ReplaySetPlaySpeed, speed, slowMotion);
  }

  /**
   * Set replay position by frame number.
   * @param mode Position mode enum value.
   * @param frameNumber Frame number.
   * @returns void
   */
  replaySetPlayPosition(mode: number, frameNumber: number): void {
    // Split the 32-bit frame number into low/high 16-bit values.
    const low = frameNumber & 0xffff;
    const high = (frameNumber >> 16) & 0xffff;
    this.broadcastMsg(constants.BroadcastMsg.ReplaySetPlayPosition, mode, low, high);
  }

  /**
   * Search replay by mode.
   * @param mode Replay search mode enum value.
   * @returns void
   */
  replaySearch(mode: number): void {
    this.broadcastMsg(constants.BroadcastMsg.ReplaySearch, mode, 0);
  }

  /**
   * Set the replay state.
   * @param state Replay state enum value.
   * @returns void
   */
  replaySetState(state: number): void {
    this.broadcastMsg(constants.BroadcastMsg.ReplaySetState, state, 0);
  }

  /**
   * Reload textures for a car or all cars.
   * @param mode Reload textures mode enum value.
   * @param carIdx Optional car index.
   * @returns void
   */
  reloadTextures(mode: number, carIdx: number = 0): void {
    this.broadcastMsg(constants.BroadcastMsg.ReloadTextures, mode, carIdx);
  }

  /**
   * Send a chat command to iRacing.
   * @param command Chat command enum value.
   * @param subCommand Optional sub-command value.
   * @returns void
   */
  sendChatCommand(command: number, subCommand: number = 0): void {
    this.broadcastMsg(constants.BroadcastMsg.ChatCommand, command, subCommand);
  }

  /**
   * Send a pit command to iRacing.
   * @param command Pit command enum value.
   * @param parameter Optional command parameter.
   * @returns void
   */
  sendPitCommand(command: number, parameter: number = 0): void {
    this.broadcastMsg(constants.BroadcastMsg.PitCommand, command, parameter);
  }

  /**
   * Send a telemetry command to iRacing.
   * @param command Telemetry command enum value.
   * @returns void
   */
  sendTelemCommand(command: number): void {
    this.broadcastMsg(constants.BroadcastMsg.TelemCommand, command, 0);
  }

  /**
   * Send a force feedback command with a float value.
   * @param command FFB command enum value.
   * @param value Float parameter.
   * @returns void
   */
  sendFFBCommand(command: number, value: number): void {
    this.broadcastMsgFloat(constants.BroadcastMsg.FFBCommand, command, value);
  }

  /**
   * Search replay by session time.
   * @param sessionNum Session number.
   * @param sessionTimeMs Session time in milliseconds.
   * @returns void
   */
  replaySearchSessionTime(sessionNum: number, sessionTimeMs: number): void {
    // Split the 32-bit time into low/high 16-bit values.
    const low = sessionTimeMs & 0xffff;
    const high = (sessionTimeMs >> 16) & 0xffff;
    this.broadcastMsg(constants.BroadcastMsg.ReplaySearchSessionTime, sessionNum, low, high);
  }

  /**
   * Start or stop video capture.
   * @param mode Video capture mode enum value.
   * @returns void
   */
  videoCapture(mode: number): void {
    this.broadcastMsg(constants.BroadcastMsg.VideoCapture, mode, 0);
  }

  /**
   * Start the polling loop if not already running.
   * @returns void
   */
  start(): void {
    // Avoid creating multiple intervals.
    if (this._timer) {
      return;
    }

    // Schedule a periodic tick to read iRacing data.
    this._timer = setInterval(() => {
      this._tick();
    }, this._pollIntervalMs);
  }

  /**
   * Stop the polling loop if running.
   * @returns void
   */
  stop(): void {
    if (this._timer) {
      // Clear the interval and reset state.
      clearInterval(this._timer);
      this._timer = null;
    }
  }

  /**
   * Poll native state and emit events.
   * @returns void
   */
  private _tick(): void {
    try {
      // Wait for new data and read connection state.
      const hadData = binding.waitForData(this._waitTimeoutMs);
      const isConnected = binding.isConnected();

      // Transition to connected state and optionally emit session payload.
      if (isConnected && !this._connected) {
        this._connected = true;
        this.emit('connect');
        if (this._emitSessionOnConnect) {
          this._emitSessionUpdate();
        }
      // Transition to disconnected state.
      } else if (!isConnected && this._connected) {
        this._connected = false;
        this.emit('disconnect');
      }

      // Skip telemetry processing when not connected.
      if (!isConnected) {
        return;
      }

      // Only read telemetry when the native layer reports new data.
      if (hadData) {
        // Emit session update when it changes.
        if (binding.wasSessionInfoUpdated()) {
          this._emitSessionUpdate();
        }

        // Emit all telemetry variables, or only the configured subset.
        if (this._useAllTelemetry) {
          const telemetry = binding.readAllVars();
          if (telemetry) {
            this.emit('telemetry', telemetry);
          }
        } else if (this._telemetryVars.length > 0) {
          const telemetry = binding.readVars(this._telemetryVars);
          this.emit('telemetry', telemetry);
        }
      }
    } catch (error) {
      // Surface unexpected native errors to the consumer.
      this.emit('error', error);
    }
  }

  /**
   * Emit the latest session payload if available.
   * @returns void
   */
  private _emitSessionUpdate(): void {
    const sessionInfo = binding.getSessionInfoObj();
    if (!sessionInfo) {
      return;
    }
    // Track update count and emit a unified session payload.
    this._lastSessionUpdate = binding.getSessionInfoUpdateCount();
    const payload: SessionUpdate = {
      updateCount: this._lastSessionUpdate,
      sessionInfo
    };
    this.emit('session', payload);
  }
}

export { IRacingClient, constants };
