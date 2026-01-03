# node-iracing-sdk

Node.js bindings for the iRacing SDK client with event-driven telemetry, session info parsing, and sim control broadcasts.

## Installation

Prerequisites:
- Windows (the iRacing SDK relies on Windows shared memory APIs).
- iRacing running and in a session to expose telemetry.

```bash
npm install node-iracing-sdk
```

TypeScript types are published separately:

```bash
npm install --save-dev @types/node-iracing-sdk
```

### Prebuilt binaries

This package ships prebuilt Windows binaries. `npm install` should not require Visual Studio or node-gyp.

### Building from source

If you need to build locally (contributors, unsupported Node ABI), install the Windows build tools
and run:

```bash
npm run build
```

Note: the `packages/runtime/irsdk_1_19/` folder contains iRacing SDK headers and sources required
to compile the native addon locally. It is intentionally excluded from npm packages and GitHub to
avoid redistributing the SDK contents. Place the SDK sources in `packages/runtime/irsdk_1_19/`
before building from source.

## Quick start

```js
const { IRacingClient } = require('node-iracing-sdk');

const client = new IRacingClient({
  pollIntervalMs: 16,
  waitTimeoutMs: 0,
  telemetryVariables: ['Speed', 'RPM', 'Lap', 'Gear']
});

client.on('connect', () => {
  console.log('Connected to iRacing');
});

client.on('disconnect', () => {
  console.log('Disconnected from iRacing');
});

client.on('session', ({ updateCount, sessionInfo }) => {
  console.log('Session info update', updateCount, sessionInfo.WeekendInfo?.TrackName);
});

client.on('telemetry', (data) => {
  console.log('Telemetry', data);
});

client.on('error', (err) => {
  console.error('Telemetry error', err);
});

client.start();
```

## API

### Exports

```js
const { IRacingClient, constants } = require('node-iracing-sdk');

// Local development:
// const { IRacingClient, constants } = require('./');
```

### `new IRacingClient(options)`

Create a polling client that emits events.

Options:
- `pollIntervalMs` (number): Polling cadence in milliseconds. Default: `16`.
- `waitTimeoutMs` (number): Timeout passed to native wait call. `0` = non-blocking. Default: `0`.
- `telemetryVariables` (string[]): List of telemetry variables to read each tick. If omitted, all telemetry values are returned. Default: `undefined`.
- `emitSessionOnConnect` (boolean): Emit a session snapshot immediately after connect. Default: `true`.

### Events

- `connect`: Fired once when the SDK connection becomes active.
- `disconnect`: Fired once when the SDK connection is lost.
- `session`: Fired when the session info string changes. Payload:
  - `updateCount` (number): Session info update counter.
  - `sessionInfo` (object): Parsed session info JSON object.
- `telemetry`: Fired on each telemetry tick with telemetry data (object).
- `error`: Fired on native or parsing errors (Error).

### Client methods

#### `start()`
Start the polling loop.

#### `stop()`
Stop the polling loop.

#### `setTelemetryVariables(names)`
Replace the list of telemetry variables to read each tick. Passing an empty array disables telemetry emission.

#### `isConnected()`
Returns `true` if the SDK connection is active.

#### `getStatusId()`
Returns the SDK status ID, which increments on reconnects.

#### `getSessionInfoObj()`
Returns the parsed session info object, or `null` if unavailable.

#### `readVars(names)`
Read telemetry values for the provided list of variable names (or the configured list if `names` is omitted).

#### `readAllVars()`
Read all telemetry values into a JS object keyed by variable name. Returns `null` if the SDK is not connected.

#### `getVarHeaders()`
Return telemetry variable metadata from the SDK as an array of objects:
`{ name, type, count, offset, countAsTime, desc, unit }`.

#### `getVarValue(name, entry)`
Read a single telemetry variable value. Optional `entry` selects array index for multi-entry variables.

#### `broadcastMsg(msg, var1, var2, var3)`
Low-level broadcast wrapper. Sends an iRacing broadcast message with either 2 or 3 integer parameters.

#### `broadcastMsgFloat(msg, var1, value)`
Low-level broadcast wrapper for float values (used by FFB commands).

#### `switchCameraByPos(carPos, group, camera)`
Switch camera using a car position.

#### `switchCameraByNum(driverNum, group, camera)`
Switch camera using a driver number. `driverNum` can be a number or a numeric string to preserve leading zeros (for example `"001"`).

#### `setCameraState(cameraState)`
Set the camera system state (bitflags).

#### `replaySetPlaySpeed(speed, slowMotion = 0)`
Set replay playback speed.

#### `replaySetPlayPosition(mode, frameNumber)`
Set replay playback position using a frame number.

#### `replaySearch(mode)`
Search the replay tape.

#### `replaySetState(state)`
Set replay tape state (e.g., erase).

#### `reloadTextures(mode, carIdx = 0)`
Reload textures for all cars or a specific car index.

#### `sendChatCommand(command, subCommand = 0)`
Send a chat command (macro, begin chat, reply, cancel).

#### `sendPitCommand(command, parameter = 0)`
Send a pit command (fuel, tires, windshield, fast repair, etc.).

#### `sendTelemCommand(command)`
Start/stop/restart telemetry recording.

#### `sendFFBCommand(command, value)`
Send force feedback command (float value).

#### `replaySearchSessionTime(sessionNum, sessionTimeMs)`
Search replay by session time (ms).

#### `videoCapture(mode)`
Trigger video capture actions (screenshot, start/stop, show timer).

### Constants

All enum values are exported under `constants` for convenience:
- `constants.BroadcastMsg`
- `constants.ChatCommandMode`
- `constants.PitCommandMode`
- `constants.TelemCommandMode`
- `constants.FFBCommandMode`
- `constants.CameraState`
- `constants.ReplaySearchMode`
- `constants.ReplayPositionMode`
- `constants.ReplayStateMode`
- `constants.ReloadTexturesMode`
- `constants.VideoCaptureMode`
- `constants.CameraFocusMode`

## Examples

### Receive all telemetry values

```js
const { IRacingClient } = require('node-iracing-sdk');

const client = new IRacingClient({
  pollIntervalMs: 16,
  waitTimeoutMs: 0
});

client.on('telemetry', (data) => {
  console.log('All telemetry keys', Object.keys(data));
});

client.start();
```

### Read a custom telemetry list

```js
const { IRacingClient } = require('node-iracing-sdk');

const client = new IRacingClient({
  telemetryVariables: ['Speed', 'RPM', 'Gear']
});

client.on('telemetry', (data) => {
  console.log('Speed', data.Speed, 'RPM', data.RPM, 'Gear', data.Gear);
});

client.start();
```

### Send a chat macro

```js
const { IRacingClient, constants } = require('node-iracing-sdk');

const client = new IRacingClient();

client.on('connect', () => {
  client.sendChatCommand(constants.ChatCommandMode.Macro, 1);
});

client.start();
```

### Request fuel and change tires

```js
const { IRacingClient, constants } = require('node-iracing-sdk');

const client = new IRacingClient();

client.on('connect', () => {
  client.sendPitCommand(constants.PitCommandMode.Clear);
  client.sendPitCommand(constants.PitCommandMode.Fuel, 20);
  client.sendPitCommand(constants.PitCommandMode.LF, 165);
  client.sendPitCommand(constants.PitCommandMode.RF, 165);
  client.sendPitCommand(constants.PitCommandMode.LR, 165);
  client.sendPitCommand(constants.PitCommandMode.RR, 165);
});

client.start();
```

### Switch cameras

```js
const { IRacingClient, constants } = require('node-iracing-sdk');

const client = new IRacingClient();

client.on('connect', () => {
  client.switchCameraByNum(0, 1, 1);
  client.setCameraState(constants.CameraState.CamToolActive);
});

client.start();
```

### Replay control

```js
const { IRacingClient, constants } = require('node-iracing-sdk');

const client = new IRacingClient();

client.on('connect', () => {
  client.replaySetPlaySpeed(1, 0);
  client.replaySearch(constants.ReplaySearchMode.PrevIncident);
  client.replaySetPlayPosition(constants.ReplayPositionMode.Current, 120000);
});

client.start();
```

### Video capture

```js
const { IRacingClient, constants } = require('node-iracing-sdk');

const client = new IRacingClient();

client.on('connect', () => {
  client.videoCapture(constants.VideoCaptureMode.TriggerScreenShot);
});

client.start();
```

### List telemetry variables with metadata

```js
const { IRacingClient } = require('node-iracing-sdk');

const client = new IRacingClient();

client.on('connect', () => {
  const vars = client.getVarHeaders();
  console.log('Telemetry var count', vars.length);
  console.log(vars[0]);
});

client.start();
```

## Notes

- Windows only: the iRacing SDK relies on Windows shared memory APIs.
- Session info is parsed in the native layer and emitted as a JSON object.
- Omit `telemetryVariables` to receive all telemetry values each tick.
- Use `telemetryVariables` to control which telemetry values are polled.
- If the SDK is disconnected, `readAllVars()` returns `null` and no telemetry events fire.
