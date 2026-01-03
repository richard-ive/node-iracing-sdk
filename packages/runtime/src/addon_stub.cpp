// Stub bindings for non-Windows platforms.
// Provides the same surface area but throws a clear error when called.

#include <node_api.h>

#include <initializer_list>
#include <utility>

#include "irsdk_defines.h"

namespace {

// Helper macro to convert N-API status codes into JS exceptions.
#define NAPI_CALL(env, call)                                    \
  do {                                                          \
    napi_status status = (call);                                \
    if (status != napi_ok) {                                    \
      const napi_extended_error_info* error_info = nullptr;     \
      napi_get_last_error_info((env), &error_info);             \
      const char* msg = error_info && error_info->error_message \
                            ? error_info->error_message         \
                            : "napi error";                    \
      napi_throw_error((env), nullptr, msg);                    \
      return nullptr;                                           \
    }                                                           \
  } while (0)

static bool CheckNapi(napi_env env, napi_status status)
{
  if (status == napi_ok) {
    return true;
  }

  const napi_extended_error_info* error_info = nullptr;
  napi_get_last_error_info(env, &error_info);
  const char* msg = error_info && error_info->error_message ? error_info->error_message : "napi error";
  napi_throw_error(env, nullptr, msg);
  return false;
}

// Throw on use to signal that the native bindings are Windows-only.
static napi_value ThrowUnsupported(napi_env env, napi_callback_info info)
{
  (void)info;
  napi_throw_error(env, nullptr, "iRacing SDK native bindings are supported on Windows only");
  return nullptr;
}

static void SetIntProp(napi_env env, napi_value obj, const char* name, int value)
{
  napi_value js_value = nullptr;
  if (!CheckNapi(env, napi_create_int32(env, value, &js_value))) {
    return;
  }
  CheckNapi(env, napi_set_named_property(env, obj, name, js_value));
}

static napi_value CreateEnumObject(napi_env env, const std::initializer_list<std::pair<const char*, int>>& entries)
{
  napi_value result = nullptr;
  NAPI_CALL(env, napi_create_object(env, &result));
  for (const auto& entry : entries) {
    SetIntProp(env, result, entry.first, entry.second);
  }
  return result;
}

static napi_value SetEnum(napi_env env,
                          napi_value target,
                          const char* name,
                          const std::initializer_list<std::pair<const char*, int>>& entries)
{
  napi_value value = CreateEnumObject(env, entries);
  NAPI_CALL(env, napi_set_named_property(env, target, name, value));
  return value;
}

// Register stub methods so requiring the module succeeds.
static napi_value Init(napi_env env, napi_value exports)
{
  napi_property_descriptor descriptors[] = {
    {"waitForData", nullptr, ThrowUnsupported, nullptr, nullptr, nullptr, napi_default, nullptr},
    {"isConnected", nullptr, ThrowUnsupported, nullptr, nullptr, nullptr, napi_default, nullptr},
    {"getStatusId", nullptr, ThrowUnsupported, nullptr, nullptr, nullptr, napi_default, nullptr},
    {"getSessionInfoUpdateCount", nullptr, ThrowUnsupported, nullptr, nullptr, nullptr, napi_default, nullptr},
    {"wasSessionInfoUpdated", nullptr, ThrowUnsupported, nullptr, nullptr, nullptr, napi_default, nullptr},
    {"getSessionInfoObj", nullptr, ThrowUnsupported, nullptr, nullptr, nullptr, napi_default, nullptr},
    {"getVarValue", nullptr, ThrowUnsupported, nullptr, nullptr, nullptr, napi_default, nullptr},
    {"readVars", nullptr, ThrowUnsupported, nullptr, nullptr, nullptr, napi_default, nullptr},
    {"readAllVars", nullptr, ThrowUnsupported, nullptr, nullptr, nullptr, napi_default, nullptr},
    {"getVarHeaders", nullptr, ThrowUnsupported, nullptr, nullptr, nullptr, napi_default, nullptr},
    {"broadcastMsg", nullptr, ThrowUnsupported, nullptr, nullptr, nullptr, napi_default, nullptr}
  };

  NAPI_CALL(env, napi_define_properties(env, exports, sizeof(descriptors) / sizeof(descriptors[0]), descriptors));

  napi_value constants = nullptr;
  NAPI_CALL(env, napi_create_object(env, &constants));

  SetEnum(env, constants, "BroadcastMsg", {
    {"CamSwitchPos", irsdk_BroadcastCamSwitchPos},
    {"CamSwitchNum", irsdk_BroadcastCamSwitchNum},
    {"CamSetState", irsdk_BroadcastCamSetState},
    {"ReplaySetPlaySpeed", irsdk_BroadcastReplaySetPlaySpeed},
    {"ReplaySetPlayPosition", irsdk_BroadcastReplaySetPlayPosition},
    {"ReplaySearch", irsdk_BroadcastReplaySearch},
    {"ReplaySetState", irsdk_BroadcastReplaySetState},
    {"ReloadTextures", irsdk_BroadcastReloadTextures},
    {"ChatCommand", irsdk_BroadcastChatComand},
    {"PitCommand", irsdk_BroadcastPitCommand},
    {"TelemCommand", irsdk_BroadcastTelemCommand},
    {"FFBCommand", irsdk_BroadcastFFBCommand},
    {"ReplaySearchSessionTime", irsdk_BroadcastReplaySearchSessionTime},
    {"VideoCapture", irsdk_BroadcastVideoCapture}
  });

  SetEnum(env, constants, "ChatCommandMode", {
    {"Macro", irsdk_ChatCommand_Macro},
    {"BeginChat", irsdk_ChatCommand_BeginChat},
    {"Reply", irsdk_ChatCommand_Reply},
    {"Cancel", irsdk_ChatCommand_Cancel}
  });

  SetEnum(env, constants, "PitCommandMode", {
    {"Clear", irsdk_PitCommand_Clear},
    {"WS", irsdk_PitCommand_WS},
    {"Fuel", irsdk_PitCommand_Fuel},
    {"LF", irsdk_PitCommand_LF},
    {"RF", irsdk_PitCommand_RF},
    {"LR", irsdk_PitCommand_LR},
    {"RR", irsdk_PitCommand_RR},
    {"ClearTires", irsdk_PitCommand_ClearTires},
    {"FR", irsdk_PitCommand_FR},
    {"ClearWS", irsdk_PitCommand_ClearWS},
    {"ClearFR", irsdk_PitCommand_ClearFR},
    {"ClearFuel", irsdk_PitCommand_ClearFuel},
    {"TC", irsdk_PitCommand_TC}
  });

  SetEnum(env, constants, "TelemCommandMode", {
    {"Stop", irsdk_TelemCommand_Stop},
    {"Start", irsdk_TelemCommand_Start},
    {"Restart", irsdk_TelemCommand_Restart}
  });

  SetEnum(env, constants, "FFBCommandMode", {
    {"MaxForce", irsdk_FFBCommand_MaxForce}
  });

  SetEnum(env, constants, "CameraState", {
    {"IsSessionScreen", irsdk_IsSessionScreen},
    {"IsScenicActive", irsdk_IsScenicActive},
    {"CamToolActive", irsdk_CamToolActive},
    {"UIHidden", irsdk_UIHidden},
    {"UseAutoShotSelection", irsdk_UseAutoShotSelection},
    {"UseTemporaryEdits", irsdk_UseTemporaryEdits},
    {"UseKeyAcceleration", irsdk_UseKeyAcceleration},
    {"UseKey10xAcceleration", irsdk_UseKey10xAcceleration},
    {"UseMouseAimMode", irsdk_UseMouseAimMode}
  });

  SetEnum(env, constants, "ReplaySearchMode", {
    {"ToStart", irsdk_RpySrch_ToStart},
    {"ToEnd", irsdk_RpySrch_ToEnd},
    {"PrevSession", irsdk_RpySrch_PrevSession},
    {"NextSession", irsdk_RpySrch_NextSession},
    {"PrevLap", irsdk_RpySrch_PrevLap},
    {"NextLap", irsdk_RpySrch_NextLap},
    {"PrevFrame", irsdk_RpySrch_PrevFrame},
    {"NextFrame", irsdk_RpySrch_NextFrame},
    {"PrevIncident", irsdk_RpySrch_PrevIncident},
    {"NextIncident", irsdk_RpySrch_NextIncident}
  });

  SetEnum(env, constants, "ReplayPositionMode", {
    {"Begin", irsdk_RpyPos_Begin},
    {"Current", irsdk_RpyPos_Current},
    {"End", irsdk_RpyPos_End}
  });

  SetEnum(env, constants, "ReplayStateMode", {
    {"EraseTape", irsdk_RpyState_EraseTape}
  });

  SetEnum(env, constants, "ReloadTexturesMode", {
    {"All", irsdk_ReloadTextures_All},
    {"CarIdx", irsdk_ReloadTextures_CarIdx}
  });

  SetEnum(env, constants, "VideoCaptureMode", {
    {"TriggerScreenShot", irsdk_VideoCapture_TriggerScreenShot},
    {"StartVideoCapture", irsdk_VideoCaptuer_StartVideoCapture},
    {"EndVideoCapture", irsdk_VideoCaptuer_EndVideoCapture},
    {"ToggleVideoCapture", irsdk_VideoCaptuer_ToggleVideoCapture},
    {"ShowVideoTimer", irsdk_VideoCaptuer_ShowVideoTimer},
    {"HideVideoTimer", irsdk_VideoCaptuer_HideVideoTimer}
  });

  SetEnum(env, constants, "CameraFocusMode", {
    {"FocusAtIncident", irsdk_csFocusAtIncident},
    {"FocusAtLeader", irsdk_csFocusAtLeader},
    {"FocusAtExiting", irsdk_csFocusAtExiting},
    {"FocusAtDriver", irsdk_csFocusAtDriver}
  });

  NAPI_CALL(env, napi_set_named_property(env, exports, "constants", constants));
  return exports;
}

}  // namespace

// N-API module entry point.
NAPI_MODULE(NODE_GYP_MODULE_NAME, Init)
