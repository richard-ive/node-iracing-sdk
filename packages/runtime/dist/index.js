"use strict";
var __importDefault = (this && this.__importDefault) || function (mod) {
    return (mod && mod.__esModule) ? mod : { "default": mod };
};
Object.defineProperty(exports, "__esModule", { value: true });
exports.constants = exports.IRacingClient = void 0;
const events_1 = require("events");
const path_1 = __importDefault(require("path"));
/**
 * Resolve and load the native binding from prebuilds or local build locations.
 * @returns The loaded native binding module.
 * @throws The last load error if no candidate path loads successfully.
 */
const binding = (() => {
    let lastError;
    try {
        const load = require('node-gyp-build');
        return load(path_1.default.join(__dirname, '..'));
    }
    catch (error) {
        lastError = error;
    }
    const candidates = ['./build/Release/irsdk_native.node', './build/Debug/irsdk_native.node'];
    // Attempt both release and debug outputs, keeping the last failure for context.
    for (const candidate of candidates) {
        try {
            return require(candidate);
        }
        catch (error) {
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
const emptyEnum = () => ({});
// Use native constants when available; otherwise default to empty tables.
const constants = binding.constants ?? {
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
exports.constants = constants;
class IRacingClient extends events_1.EventEmitter {
    _pollIntervalMs;
    _waitTimeoutMs;
    _telemetryVars;
    _useAllTelemetry;
    _emitSessionOnConnect;
    _timer;
    _connected;
    _lastSessionUpdate;
    /**
     * Create a new telemetry client with optional polling configuration.
     * @param options.pollIntervalMs Poll interval in milliseconds.
     * @param options.waitTimeoutMs Wait timeout in milliseconds passed to the native wait.
     * @param options.telemetryVariables Names of telemetry variables to read on each tick.
     * @param options.emitSessionOnConnect Emit session payload immediately on connect.
     * @returns A new IRacingClient instance.
     */
    constructor(options = {}) {
        super();
        const { pollIntervalMs = 16, waitTimeoutMs = 0, telemetryVariables, emitSessionOnConnect = true } = options;
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
    setTelemetryVariables(names) {
        // Copy the array to avoid external mutation.
        this._telemetryVars = Array.isArray(names) ? names.slice() : [];
        this._useAllTelemetry = false;
    }
    /**
     * Query whether iRacing is currently connected.
     * @returns True if connected.
     */
    isConnected() {
        return binding.isConnected();
    }
    /**
     * Read the native iRacing status id.
     * @returns Numeric status id.
     */
    getStatusId() {
        return binding.getStatusId();
    }
    /**
     * Retrieve the latest session info as a structured object.
     * @returns Session info object or null if unavailable.
     */
    getSessionInfoObj() {
        return binding.getSessionInfoObj();
    }
    /**
     * Read telemetry variables by name.
     * @param names Optional list of names; defaults to configured list.
     * @returns Telemetry data mapping.
     */
    readVars(names) {
        // Fall back to the configured names if none are provided.
        const vars = Array.isArray(names) ? names : this._telemetryVars;
        return binding.readVars(vars);
    }
    /**
     * Read all available telemetry variables.
     * @returns Telemetry data mapping or null when unavailable.
     */
    readAllVars() {
        return binding.readAllVars();
    }
    /**
     * Get telemetry variable headers (name, type, count, etc).
     * @returns Array of variable headers.
     */
    getVarHeaders() {
        return binding.getVarHeaders();
    }
    /**
     * Read a single telemetry variable value.
     * @param name Telemetry variable name.
     * @param entry Optional array index for multi-value variables.
     * @returns The telemetry value.
     */
    getVarValue(name, entry) {
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
    broadcastMsg(msg, var1, var2, var3) {
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
    broadcastMsgFloat(msg, var1, value) {
        binding.broadcastMsg(msg, var1, value);
    }
    /**
     * Switch camera by car position.
     * @param carPos Car position index.
     * @param group Camera group.
     * @param camera Camera index.
     * @returns void
     */
    switchCameraByPos(carPos, group, camera) {
        this.broadcastMsg(constants.BroadcastMsg.CamSwitchPos, carPos, group, camera);
    }
    /**
     * Switch camera by driver number.
     * @param driverNum Driver number or string identifier.
     * @param group Camera group.
     * @param camera Camera index.
     * @returns void
     */
    switchCameraByNum(driverNum, group, camera) {
        this.broadcastMsg(constants.BroadcastMsg.CamSwitchNum, driverNum, group, camera);
    }
    /**
     * Set the current camera state.
     * @param cameraState Camera state enum value.
     * @returns void
     */
    setCameraState(cameraState) {
        this.broadcastMsg(constants.BroadcastMsg.CamSetState, cameraState, 0);
    }
    /**
     * Control replay playback speed.
     * @param speed Playback speed.
     * @param slowMotion Slow motion flag value.
     * @returns void
     */
    replaySetPlaySpeed(speed, slowMotion = 0) {
        this.broadcastMsg(constants.BroadcastMsg.ReplaySetPlaySpeed, speed, slowMotion);
    }
    /**
     * Set replay position by frame number.
     * @param mode Position mode enum value.
     * @param frameNumber Frame number.
     * @returns void
     */
    replaySetPlayPosition(mode, frameNumber) {
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
    replaySearch(mode) {
        this.broadcastMsg(constants.BroadcastMsg.ReplaySearch, mode, 0);
    }
    /**
     * Set the replay state.
     * @param state Replay state enum value.
     * @returns void
     */
    replaySetState(state) {
        this.broadcastMsg(constants.BroadcastMsg.ReplaySetState, state, 0);
    }
    /**
     * Reload textures for a car or all cars.
     * @param mode Reload textures mode enum value.
     * @param carIdx Optional car index.
     * @returns void
     */
    reloadTextures(mode, carIdx = 0) {
        this.broadcastMsg(constants.BroadcastMsg.ReloadTextures, mode, carIdx);
    }
    /**
     * Send a chat command to iRacing.
     * @param command Chat command enum value.
     * @param subCommand Optional sub-command value.
     * @returns void
     */
    sendChatCommand(command, subCommand = 0) {
        this.broadcastMsg(constants.BroadcastMsg.ChatCommand, command, subCommand);
    }
    /**
     * Send a pit command to iRacing.
     * @param command Pit command enum value.
     * @param parameter Optional command parameter.
     * @returns void
     */
    sendPitCommand(command, parameter = 0) {
        this.broadcastMsg(constants.BroadcastMsg.PitCommand, command, parameter);
    }
    /**
     * Send a telemetry command to iRacing.
     * @param command Telemetry command enum value.
     * @returns void
     */
    sendTelemCommand(command) {
        this.broadcastMsg(constants.BroadcastMsg.TelemCommand, command, 0);
    }
    /**
     * Send a force feedback command with a float value.
     * @param command FFB command enum value.
     * @param value Float parameter.
     * @returns void
     */
    sendFFBCommand(command, value) {
        this.broadcastMsgFloat(constants.BroadcastMsg.FFBCommand, command, value);
    }
    /**
     * Search replay by session time.
     * @param sessionNum Session number.
     * @param sessionTimeMs Session time in milliseconds.
     * @returns void
     */
    replaySearchSessionTime(sessionNum, sessionTimeMs) {
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
    videoCapture(mode) {
        this.broadcastMsg(constants.BroadcastMsg.VideoCapture, mode, 0);
    }
    /**
     * Start the polling loop if not already running.
     * @returns void
     */
    start() {
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
    stop() {
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
    _tick() {
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
            }
            else if (!isConnected && this._connected) {
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
                }
                else if (this._telemetryVars.length > 0) {
                    const telemetry = binding.readVars(this._telemetryVars);
                    this.emit('telemetry', telemetry);
                }
            }
        }
        catch (error) {
            // Surface unexpected native errors to the consumer.
            this.emit('error', error);
        }
    }
    /**
     * Emit the latest session payload if available.
     * @returns void
     */
    _emitSessionUpdate() {
        const sessionInfo = binding.getSessionInfoObj();
        if (!sessionInfo) {
            return;
        }
        // Track update count and emit a unified session payload.
        this._lastSessionUpdate = binding.getSessionInfoUpdateCount();
        const payload = {
            updateCount: this._lastSessionUpdate,
            sessionInfo
        };
        this.emit('session', payload);
    }
}
exports.IRacingClient = IRacingClient;
