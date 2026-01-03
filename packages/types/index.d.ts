import { EventEmitter } from 'events';

declare module 'node-iracing-sdk' {
  export interface IRacingClientOptions {
    pollIntervalMs?: number;
    waitTimeoutMs?: number;
    telemetryVariables?: string[];
    emitSessionOnConnect?: boolean;
  }

  export type TelemetryValue = number | boolean | null;
  export type TelemetryEntry = TelemetryValue | TelemetryValue[];
  export type TelemetryData = Record<string, TelemetryEntry>;

  export interface TelemetryVarHeader {
    name: string;
    type: number;
    count: number;
    offset: number;
    countAsTime: boolean;
    desc: string;
    unit: string;
  }

  export type SessionInfoValue =
    | string
    | number
    | boolean
    | null
    | SessionInfoValue[]
    | SessionInfoObject;

  export interface SessionInfoObject {
    [key: string]: SessionInfoValue;
  }

  export interface SessionUpdate {
    updateCount: number;
    sessionInfo: SessionInfoObject;
  }

  export interface IRacingConstants {
    BroadcastMsg: Record<string, number>;
    ChatCommandMode: Record<string, number>;
    PitCommandMode: Record<string, number>;
    TelemCommandMode: Record<string, number>;
    FFBCommandMode: Record<string, number>;
    CameraState: Record<string, number>;
    ReplaySearchMode: Record<string, number>;
    ReplayPositionMode: Record<string, number>;
    ReplayStateMode: Record<string, number>;
    ReloadTexturesMode: Record<string, number>;
    VideoCaptureMode: Record<string, number>;
    CameraFocusMode: Record<string, number>;
  }

  export class IRacingClient extends EventEmitter {
    private _pollIntervalMs: number;
    private _waitTimeoutMs: number;
    private _telemetryVars: string[];
    private _useAllTelemetry: boolean;
    private _emitSessionOnConnect: boolean;
    private _timer: NodeJS.Timeout | null;
    private _connected: boolean;
    private _lastSessionUpdate: number;

    constructor(options?: IRacingClientOptions);

    setTelemetryVariables(names: string[]): void;

    isConnected(): boolean;
    getStatusId(): number;
    getSessionInfoObj(): SessionInfoObject | null;

    readVars(names?: string[]): TelemetryData;
    readAllVars(): TelemetryData | null;

    getVarHeaders(): TelemetryVarHeader[];
    getVarValue(name: string, entry?: number | null): TelemetryValue;

    broadcastMsg(msg: number, var1: number | string, var2: number, var3?: number): void;
    broadcastMsgFloat(msg: number, var1: number | string, value: number): void;

    switchCameraByPos(carPos: number, group: number, camera: number): void;
    switchCameraByNum(driverNum: number | string, group: number, camera: number): void;
    setCameraState(cameraState: number): void;

    replaySetPlaySpeed(speed: number, slowMotion?: number): void;
    replaySetPlayPosition(mode: number, frameNumber: number): void;
    replaySearch(mode: number): void;
    replaySetState(state: number): void;

    reloadTextures(mode: number, carIdx?: number): void;
    sendChatCommand(command: number, subCommand?: number): void;
    sendPitCommand(command: number, parameter?: number): void;
    sendTelemCommand(command: number): void;
    sendFFBCommand(command: number, value: number): void;
    replaySearchSessionTime(sessionNum: number, sessionTimeMs: number): void;
    videoCapture(mode: number): void;

    start(): void;
    stop(): void;

    private _tick(): void;
    private _emitSessionUpdate(): void;

    on(event: 'connect', listener: () => void): this;
    on(event: 'disconnect', listener: () => void): this;
    on(event: 'session', listener: (payload: SessionUpdate) => void): this;
    on(event: 'telemetry', listener: (data: TelemetryData) => void): this;
    on(event: 'error', listener: (error: Error) => void): this;
    on(event: string, listener: (...args: unknown[]) => void): this;

    once(event: 'connect', listener: () => void): this;
    once(event: 'disconnect', listener: () => void): this;
    once(event: 'session', listener: (payload: SessionUpdate) => void): this;
    once(event: 'telemetry', listener: (data: TelemetryData) => void): this;
    once(event: 'error', listener: (error: Error) => void): this;
    once(event: string, listener: (...args: unknown[]) => void): this;

    off(event: 'connect', listener: () => void): this;
    off(event: 'disconnect', listener: () => void): this;
    off(event: 'session', listener: (payload: SessionUpdate) => void): this;
    off(event: 'telemetry', listener: (data: TelemetryData) => void): this;
    off(event: 'error', listener: (error: Error) => void): this;
    off(event: string, listener: (...args: unknown[]) => void): this;

    emit(event: 'connect'): boolean;
    emit(event: 'disconnect'): boolean;
    emit(event: 'session', payload: SessionUpdate): boolean;
    emit(event: 'telemetry', data: TelemetryData): boolean;
    emit(event: 'error', error: Error): boolean;
    emit(event: string, ...args: unknown[]): boolean;
  }

  export const constants: IRacingConstants;
}
