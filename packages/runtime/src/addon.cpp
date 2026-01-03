// Native bindings for the iRacing SDK C++ client.
// Exposes synchronous methods for polling, session info, and telemetry values.

#include <node_api.h>

#include <cerrno>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include "irsdk_defines.h"
#include "irsdk_client.h"

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

// Basic N-API value constructors used across the bindings.
static napi_value MakeBool(napi_env env, bool value)
{
  napi_value result = nullptr;
  NAPI_CALL(env, napi_get_boolean(env, value, &result));
  return result;
}

static napi_value MakeInt(napi_env env, int value)
{
  napi_value result = nullptr;
  NAPI_CALL(env, napi_create_int32(env, value, &result));
  return result;
}

static napi_value MakeDouble(napi_env env, double value)
{
  napi_value result = nullptr;
  NAPI_CALL(env, napi_create_double(env, value, &result));
  return result;
}

// Extract a UTF-8 string from a JS value.
static bool GetString(napi_env env, napi_value value, std::string* out)
{
  size_t len = 0;
  napi_status status = napi_get_value_string_utf8(env, value, nullptr, 0, &len);
  if (status != napi_ok) {
    return false;
  }

  std::string temp;
  temp.resize(len + 1);
  status = napi_get_value_string_utf8(env, value, &temp[0], len + 1, &len);
  if (status != napi_ok) {
    return false;
  }

  *out = std::string(temp.c_str(), len);
  return true;
}

static bool ParseCarNumberArg(napi_env env, napi_value value, int* out)
{
  napi_valuetype type = napi_undefined;
  if (!CheckNapi(env, napi_typeof(env, value, &type))) {
    return false;
  }

  if (type == napi_string) {
    std::string text;
    if (!GetString(env, value, &text) || text.empty()) {
      napi_throw_type_error(env, nullptr, "car number must be a non-empty numeric string");
      return false;
    }

    size_t index = 0;
    while (index < text.size() && text[index] == '0') {
      index += 1;
    }

    size_t zero_count = index;
    std::string digits = text.substr(index);

    int num = 0;
    if (digits.empty()) {
      zero_count = text.size() > 0 ? text.size() - 1 : 0;
      num = 0;
    } else {
      for (char ch : digits) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) {
          napi_throw_type_error(env, nullptr, "car number must be numeric");
          return false;
        }
      }

      errno = 0;
      char* end = nullptr;
      long parsed = std::strtol(digits.c_str(), &end, 10);
      if (errno == ERANGE || end == digits.c_str() || *end != '\0') {
        napi_throw_type_error(env, nullptr, "car number is out of range");
        return false;
      }
      num = static_cast<int>(parsed);
    }

    *out = irsdk_padCarNum(num, static_cast<int>(zero_count));
    return true;
  }

  int32_t num = 0;
  if (!CheckNapi(env, napi_get_value_int32(env, value, &num))) {
    return false;
  }
  *out = irsdk_padCarNum(num, 0);
  return true;
}

// Read a telemetry variable value and return the appropriate JS type.
static napi_value ReadVarValue(napi_env env, int idx, int type, int entry)
{
  irsdkClient& client = irsdkClient::instance();
  switch (type) {
    case irsdk_bool:
      return MakeBool(env, client.getVarBool(idx, entry));
    case irsdk_char:
    case irsdk_int:
    case irsdk_bitField:
      return MakeInt(env, client.getVarInt(idx, entry));
    case irsdk_float:
      return MakeDouble(env, static_cast<double>(client.getVarFloat(idx, entry)));
    case irsdk_double:
      return MakeDouble(env, client.getVarDouble(idx, entry));
    default:
      break;
  }

  napi_value result = nullptr;
  NAPI_CALL(env, napi_get_null(env, &result));
  return result;
}

// Blocks until new telemetry is ready or the timeout elapses.
static napi_value WaitForData(napi_env env, napi_callback_info info)
{
  size_t argc = 1;
  napi_value args[1];
  NAPI_CALL(env, napi_get_cb_info(env, info, &argc, args, nullptr, nullptr));

  int32_t timeout_ms = 0;
  if (argc >= 1) {
    napi_valuetype type = napi_undefined;
    NAPI_CALL(env, napi_typeof(env, args[0], &type));
    if (type != napi_undefined && type != napi_null) {
      NAPI_CALL(env, napi_get_value_int32(env, args[0], &timeout_ms));
    }
  }

  bool ready = irsdkClient::instance().waitForData(timeout_ms);
  return MakeBool(env, ready);
}

// Returns whether the SDK client is connected to the sim.
static napi_value IsConnected(napi_env env, napi_callback_info info)
{
  (void)info;
  return MakeBool(env, irsdkClient::instance().isConnected());
}

// Exposes the SDK connection status ID, which increments on reconnects.
static napi_value GetStatusId(napi_env env, napi_callback_info info)
{
  (void)info;
  return MakeInt(env, irsdkClient::instance().getStatusID());
}

// Exposes the session info update counter from the SDK.
static napi_value GetSessionInfoUpdateCount(napi_env env, napi_callback_info info)
{
  (void)info;
  return MakeInt(env, irsdkClient::instance().getSessionCt());
}

// Returns true if the session info string changed since last read.
static napi_value WasSessionInfoUpdated(napi_env env, napi_callback_info info)
{
  (void)info;
  return MakeBool(env, irsdkClient::instance().wasSessionStrUpdated());
}

static napi_value BroadcastMsg(napi_env env, napi_callback_info info)
{
  size_t argc = 4;
  napi_value args[4];
  NAPI_CALL(env, napi_get_cb_info(env, info, &argc, args, nullptr, nullptr));

  if (argc < 3) {
    napi_throw_type_error(env, nullptr, "broadcastMsg expects (msg, var1, var2[, var3])");
    return nullptr;
  }

  int32_t msg = 0;
  int32_t var1 = 0;
  NAPI_CALL(env, napi_get_value_int32(env, args[0], &msg));
  if (msg == irsdk_BroadcastCamSwitchNum) {
    if (!ParseCarNumberArg(env, args[1], &var1)) {
      return nullptr;
    }
  } else {
    NAPI_CALL(env, napi_get_value_int32(env, args[1], &var1));
  }

  if (msg == irsdk_BroadcastFFBCommand) {
    double value = 0.0;
    NAPI_CALL(env, napi_get_value_double(env, args[2], &value));
    irsdk_broadcastMsg(static_cast<irsdk_BroadcastMsg>(msg), var1, static_cast<float>(value));
  } else if (argc >= 4) {
    int32_t var2 = 0;
    int32_t var3 = 0;
    NAPI_CALL(env, napi_get_value_int32(env, args[2], &var2));
    NAPI_CALL(env, napi_get_value_int32(env, args[3], &var3));
    irsdk_broadcastMsg(static_cast<irsdk_BroadcastMsg>(msg), var1, var2, var3);
  } else {
    int32_t var2 = 0;
    NAPI_CALL(env, napi_get_value_int32(env, args[2], &var2));
    irsdk_broadcastMsg(static_cast<irsdk_BroadcastMsg>(msg), var1, var2);
  }

  napi_value result = nullptr;
  NAPI_CALL(env, napi_get_undefined(env, &result));
  return result;
}

static bool TryParseInt64(const std::string& value, int64_t* out)
{
  if (value.empty()) {
    return false;
  }
  if (value.size() > 2 && value[0] == '0' && (value[1] == 'x' || value[1] == 'X')) {
    return false;
  }
  char* end = nullptr;
  errno = 0;
  long long parsed = std::strtoll(value.c_str(), &end, 10);
  if (errno == ERANGE || end == value.c_str() || *end != '\0') {
    return false;
  }
  *out = static_cast<int64_t>(parsed);
  return true;
}

static bool TryParseDouble(const std::string& value, double* out)
{
  if (value.empty()) {
    return false;
  }
  if (value.size() > 2 && value[0] == '0' && (value[1] == 'x' || value[1] == 'X')) {
    return false;
  }
  char* end = nullptr;
  errno = 0;
  double parsed = std::strtod(value.c_str(), &end);
  if (errno == ERANGE || end == value.c_str() || *end != '\0') {
    return false;
  }
  *out = parsed;
  return true;
}

static std::string Trim(const std::string& value)
{
  size_t start = 0;
  while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
    start += 1;
  }
  if (start == value.size()) {
    return std::string();
  }
  size_t end = value.size();
  while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
    end -= 1;
  }
  return value.substr(start, end - start);
}

static int LeadingIndent(const std::string& value)
{
  int indent = 0;
  for (size_t i = 0; i < value.size(); ++i) {
    if (value[i] == ' ' || value[i] == '\t') {
      indent += 1;
    } else {
      break;
    }
  }
  return indent;
}

static void SplitLines(const char* text, std::vector<std::string>* lines)
{
  std::string current;
  for (const char* ptr = text; *ptr; ++ptr) {
    char ch = *ptr;
    if (ch == '\r') {
      continue;
    }
    if (ch == '\n') {
      lines->push_back(current);
      current.clear();
    } else {
      current.push_back(ch);
    }
  }
  lines->push_back(current);
}

struct NextLineInfo {
  bool found;
  int indent;
  bool starts_with_dash;
};

static NextLineInfo PeekNextLine(const std::vector<std::string>& lines, size_t start)
{
  for (size_t i = start; i < lines.size(); ++i) {
    std::string trimmed = Trim(lines[i]);
    if (!trimmed.empty()) {
      return NextLineInfo{true, LeadingIndent(lines[i]), !trimmed.empty() && trimmed[0] == '-'};
    }
  }
  return NextLineInfo{false, 0, false};
}

static napi_value ParseScalarToJs(napi_env env, const std::string& value)
{
  if (value.empty()) {
    napi_value result = nullptr;
    NAPI_CALL(env, napi_get_null(env, &result));
    return result;
  }
  if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
    const std::string unquoted = value.substr(1, value.size() - 2);
    napi_value result = nullptr;
    NAPI_CALL(env, napi_create_string_utf8(env, unquoted.c_str(), NAPI_AUTO_LENGTH, &result));
    return result;
  }
  if (value == "true") {
    return MakeBool(env, true);
  }
  if (value == "false") {
    return MakeBool(env, false);
  }
  int64_t int_value = 0;
  if (TryParseInt64(value, &int_value)) {
    napi_value result = nullptr;
    NAPI_CALL(env, napi_create_int64(env, int_value, &result));
    return result;
  }
  double double_value = 0.0;
  if (TryParseDouble(value, &double_value)) {
    return MakeDouble(env, double_value);
  }

  napi_value result = nullptr;
  NAPI_CALL(env, napi_create_string_utf8(env, value.c_str(), NAPI_AUTO_LENGTH, &result));
  return result;
}

static bool AppendToArray(napi_env env, napi_value array, napi_value value)
{
  uint32_t length = 0;
  if (!CheckNapi(env, napi_get_array_length(env, array, &length))) {
    return false;
  }
  return CheckNapi(env, napi_set_element(env, array, length, value));
}

static bool SetObjectProperty(napi_env env, napi_value obj, const std::string& key, napi_value value)
{
  napi_value js_key = nullptr;
  if (!CheckNapi(env, napi_create_string_utf8(env, key.c_str(), NAPI_AUTO_LENGTH, &js_key))) {
    return false;
  }
  return CheckNapi(env, napi_set_property(env, obj, js_key, value));
}

static napi_value ParseSessionYamlToJs(napi_env env, const char* session)
{
  std::vector<std::string> lines;
  SplitLines(session, &lines);

  napi_value root = nullptr;
  NAPI_CALL(env, napi_create_object(env, &root));

  struct Context {
    int indent;
    bool is_array;
    napi_value container;
  };

  std::vector<Context> stack;
  stack.push_back(Context{-1, false, root});

  for (size_t i = 0; i < lines.size(); ++i) {
    const std::string& raw_line = lines[i];
    std::string trimmed = Trim(raw_line);
    if (trimmed.empty()) {
      continue;
    }

    int indent = LeadingIndent(raw_line);
    while (stack.size() > 1 && indent <= stack.back().indent) {
      stack.pop_back();
    }

    Context& current = stack.back();
    if (!trimmed.empty() && trimmed[0] == '-') {
      if (!current.is_array) {
        continue;
      }

      std::string item_text = Trim(trimmed.substr(1));
      if (item_text.empty()) {
        NextLineInfo next_info = PeekNextLine(lines, i + 1);
        bool next_is_array = next_info.found && next_info.starts_with_dash;
        napi_value child = nullptr;
        if (next_is_array) {
          NAPI_CALL(env, napi_create_array(env, &child));
        } else {
          NAPI_CALL(env, napi_create_object(env, &child));
        }
        if (!AppendToArray(env, current.container, child)) {
          return nullptr;
        }
        int child_indent = next_info.found ? next_info.indent : indent + 1;
        stack.push_back(Context{child_indent, next_is_array, child});
        continue;
      }

      size_t colon = item_text.find(':');
      if (colon != std::string::npos) {
        std::string key = Trim(item_text.substr(0, colon));
        std::string raw_value = Trim(item_text.substr(colon + 1));

        napi_value item_obj = nullptr;
        NAPI_CALL(env, napi_create_object(env, &item_obj));

        NextLineInfo next_info = PeekNextLine(lines, i + 1);
        if (raw_value.empty()) {
          bool next_is_array = next_info.found && next_info.starts_with_dash;
          napi_value child = nullptr;
          if (next_is_array) {
            NAPI_CALL(env, napi_create_array(env, &child));
          } else {
            NAPI_CALL(env, napi_create_object(env, &child));
          }
          if (!SetObjectProperty(env, item_obj, key, child)) {
            return nullptr;
          }
          if (!AppendToArray(env, current.container, item_obj)) {
            return nullptr;
          }
          stack.push_back(Context{indent, false, item_obj});
          int child_indent = next_info.found ? next_info.indent : indent + 1;
          stack.push_back(Context{child_indent, next_is_array, child});
        } else {
          napi_value value = ParseScalarToJs(env, raw_value);
          if (!SetObjectProperty(env, item_obj, key, value)) {
            return nullptr;
          }
          if (!AppendToArray(env, current.container, item_obj)) {
            return nullptr;
          }
          if (next_info.found && next_info.indent > indent) {
            stack.push_back(Context{indent, false, item_obj});
          }
        }
      } else {
        napi_value value = ParseScalarToJs(env, item_text);
        if (!AppendToArray(env, current.container, value)) {
          return nullptr;
        }
      }

      continue;
    }

    if (current.is_array) {
      continue;
    }

    size_t colon = trimmed.find(':');
    if (colon == std::string::npos) {
      continue;
    }

    std::string key = Trim(trimmed.substr(0, colon));
    std::string raw_value = Trim(trimmed.substr(colon + 1));
    if (raw_value.empty()) {
      NextLineInfo next_info = PeekNextLine(lines, i + 1);
      bool next_is_array = next_info.found && next_info.starts_with_dash;
      napi_value child = nullptr;
      if (next_is_array) {
        NAPI_CALL(env, napi_create_array(env, &child));
      } else {
        NAPI_CALL(env, napi_create_object(env, &child));
      }
      if (!SetObjectProperty(env, current.container, key, child)) {
        return nullptr;
      }
      int child_indent = next_info.found ? next_info.indent : indent + 1;
      stack.push_back(Context{child_indent, next_is_array, child});
    } else {
      napi_value value = ParseScalarToJs(env, raw_value);
      if (!SetObjectProperty(env, current.container, key, value)) {
        return nullptr;
      }
    }
  }

  return root;
}

// Parses the session info YAML into a JS object.
static napi_value GetSessionInfoObj(napi_env env, napi_callback_info info)
{
  (void)info;
  const char* session = irsdkClient::instance().getSessionStr();
  if (!session) {
    napi_value result = nullptr;
    NAPI_CALL(env, napi_get_null(env, &result));
    return result;
  }

  return ParseSessionYamlToJs(env, session);
}

// Returns a single value for the variable, optionally at an array entry.
static napi_value GetVarValue(napi_env env, napi_callback_info info)
{
  size_t argc = 2;
  napi_value args[2];
  NAPI_CALL(env, napi_get_cb_info(env, info, &argc, args, nullptr, nullptr));
  if (argc < 1) {
    napi_throw_error(env, nullptr, "getVarValue requires a variable name");
    return nullptr;
  }

  std::string name;
  if (!GetString(env, args[0], &name)) {
    napi_throw_error(env, nullptr, "invalid variable name");
    return nullptr;
  }

  int entry = 0;
  if (argc >= 2) {
    napi_valuetype type = napi_undefined;
    NAPI_CALL(env, napi_typeof(env, args[1], &type));
    if (type != napi_undefined && type != napi_null) {
      NAPI_CALL(env, napi_get_value_int32(env, args[1], &entry));
    }
  }

  irsdkClient& client = irsdkClient::instance();
  int idx = client.getVarIdx(name.c_str());
  if (idx < 0) {
    napi_value result = nullptr;
    NAPI_CALL(env, napi_get_null(env, &result));
    return result;
  }

  int count = client.getVarCount(idx);
  if (entry < 0 || entry >= count) {
    napi_throw_range_error(env, nullptr, "entry index out of range");
    return nullptr;
  }

  int type = client.getVarType(idx);
  return ReadVarValue(env, idx, type, entry);
}

// Reads multiple variables in one call and returns a name->value map.
static napi_value ReadVars(napi_env env, napi_callback_info info)
{
  size_t argc = 1;
  napi_value args[1];
  NAPI_CALL(env, napi_get_cb_info(env, info, &argc, args, nullptr, nullptr));
  if (argc < 1) {
    napi_throw_error(env, nullptr, "readVars requires an array of variable names");
    return nullptr;
  }

  bool is_array = false;
  NAPI_CALL(env, napi_is_array(env, args[0], &is_array));
  if (!is_array) {
    napi_throw_error(env, nullptr, "readVars requires an array of variable names");
    return nullptr;
  }

  uint32_t length = 0;
  NAPI_CALL(env, napi_get_array_length(env, args[0], &length));

  napi_value result = nullptr;
  NAPI_CALL(env, napi_create_object(env, &result));

  irsdkClient& client = irsdkClient::instance();

  for (uint32_t i = 0; i < length; ++i) {
    napi_value name_value = nullptr;
    NAPI_CALL(env, napi_get_element(env, args[0], i, &name_value));

    std::string name;
    if (!GetString(env, name_value, &name)) {
      continue;
    }

    napi_value js_value = nullptr;
    int idx = client.getVarIdx(name.c_str());
    if (idx < 0) {
      NAPI_CALL(env, napi_get_null(env, &js_value));
    } else {
      int count = client.getVarCount(idx);
      int type = client.getVarType(idx);
      if (count <= 1) {
        js_value = ReadVarValue(env, idx, type, 0);
      } else {
        NAPI_CALL(env, napi_create_array_with_length(env, count, &js_value));
        for (int j = 0; j < count; ++j) {
          napi_value entry_value = ReadVarValue(env, idx, type, j);
          NAPI_CALL(env, napi_set_element(env, js_value, static_cast<uint32_t>(j), entry_value));
        }
      }
    }

    napi_value key = nullptr;
    NAPI_CALL(env, napi_create_string_utf8(env, name.c_str(), NAPI_AUTO_LENGTH, &key));
    NAPI_CALL(env, napi_set_property(env, result, key, js_value));
  }

  return result;
}

// Reads every variable available in the SDK and returns a name->value map.
static napi_value ReadAllVars(napi_env env, napi_callback_info info)
{
  (void)info;
  if (!irsdkClient::instance().isConnected()) {
    napi_value result = nullptr;
    NAPI_CALL(env, napi_get_null(env, &result));
    return result;
  }

  const irsdk_header* header = irsdk_getHeader();
  if (!header || header->numVars <= 0) {
    napi_value result = nullptr;
    NAPI_CALL(env, napi_create_object(env, &result));
    return result;
  }

  napi_value result = nullptr;
  NAPI_CALL(env, napi_create_object(env, &result));

  for (int idx = 0; idx < header->numVars; ++idx) {
    const irsdk_varHeader* vh = irsdk_getVarHeaderEntry(idx);
    if (!vh) {
      continue;
    }

    napi_value js_value = nullptr;
    if (vh->count <= 1) {
      js_value = ReadVarValue(env, idx, vh->type, 0);
    } else {
      NAPI_CALL(env, napi_create_array_with_length(env, vh->count, &js_value));
      for (int entry = 0; entry < vh->count; ++entry) {
        napi_value entry_value = ReadVarValue(env, idx, vh->type, entry);
        NAPI_CALL(env, napi_set_element(env, js_value, static_cast<uint32_t>(entry), entry_value));
      }
    }

    const char* name = vh->name;
    if (!name || name[0] == '\0') {
      continue;
    }
    napi_value key = nullptr;
    NAPI_CALL(env, napi_create_string_utf8(env, name, NAPI_AUTO_LENGTH, &key));
    NAPI_CALL(env, napi_set_property(env, result, key, js_value));
  }

  return result;
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

// Return the list of telemetry variable headers (name, type, unit, desc, count).
static napi_value GetVarHeaders(napi_env env, napi_callback_info info)
{
  (void)info;
  const irsdk_header* header = irsdk_getHeader();
  if (!header || header->numVars <= 0) {
    napi_value empty = nullptr;
    NAPI_CALL(env, napi_create_array(env, &empty));
    return empty;
  }

  napi_value result = nullptr;
  NAPI_CALL(env, napi_create_array_with_length(env, header->numVars, &result));

  for (int index = 0; index < header->numVars; ++index) {
    const irsdk_varHeader* var = irsdk_getVarHeaderEntry(index);
    if (!var) {
      continue;
    }

    napi_value entry = nullptr;
    NAPI_CALL(env, napi_create_object(env, &entry));

    napi_value name = nullptr;
    NAPI_CALL(env, napi_create_string_utf8(env, var->name, NAPI_AUTO_LENGTH, &name));
    NAPI_CALL(env, napi_set_named_property(env, entry, "name", name));

    napi_value type = nullptr;
    NAPI_CALL(env, napi_create_int32(env, var->type, &type));
    NAPI_CALL(env, napi_set_named_property(env, entry, "type", type));

    napi_value count = nullptr;
    NAPI_CALL(env, napi_create_int32(env, var->count, &count));
    NAPI_CALL(env, napi_set_named_property(env, entry, "count", count));

    napi_value offset = nullptr;
    NAPI_CALL(env, napi_create_int32(env, var->offset, &offset));
    NAPI_CALL(env, napi_set_named_property(env, entry, "offset", offset));

    napi_value count_as_time = nullptr;
    NAPI_CALL(env, napi_get_boolean(env, var->countAsTime, &count_as_time));
    NAPI_CALL(env, napi_set_named_property(env, entry, "countAsTime", count_as_time));

    napi_value desc = nullptr;
    NAPI_CALL(env, napi_create_string_utf8(env, var->desc, NAPI_AUTO_LENGTH, &desc));
    NAPI_CALL(env, napi_set_named_property(env, entry, "desc", desc));

    napi_value unit = nullptr;
    NAPI_CALL(env, napi_create_string_utf8(env, var->unit, NAPI_AUTO_LENGTH, &unit));
    NAPI_CALL(env, napi_set_named_property(env, entry, "unit", unit));

    NAPI_CALL(env, napi_set_element(env, result, static_cast<uint32_t>(index), entry));
  }

  return result;
}

// Register native methods on the module exports object.
static napi_value Init(napi_env env, napi_value exports)
{
  napi_property_descriptor descriptors[] = {
    {"waitForData", nullptr, WaitForData, nullptr, nullptr, nullptr, napi_default, nullptr},
    {"isConnected", nullptr, IsConnected, nullptr, nullptr, nullptr, napi_default, nullptr},
    {"getStatusId", nullptr, GetStatusId, nullptr, nullptr, nullptr, napi_default, nullptr},
    {"getSessionInfoUpdateCount", nullptr, GetSessionInfoUpdateCount, nullptr, nullptr, nullptr, napi_default, nullptr},
    {"wasSessionInfoUpdated", nullptr, WasSessionInfoUpdated, nullptr, nullptr, nullptr, napi_default, nullptr},
    {"getSessionInfoObj", nullptr, GetSessionInfoObj, nullptr, nullptr, nullptr, napi_default, nullptr},
    {"getVarValue", nullptr, GetVarValue, nullptr, nullptr, nullptr, napi_default, nullptr},
    {"readVars", nullptr, ReadVars, nullptr, nullptr, nullptr, napi_default, nullptr},
    {"readAllVars", nullptr, ReadAllVars, nullptr, nullptr, nullptr, napi_default, nullptr},
    {"getVarHeaders", nullptr, GetVarHeaders, nullptr, nullptr, nullptr, napi_default, nullptr},
    {"broadcastMsg", nullptr, BroadcastMsg, nullptr, nullptr, nullptr, napi_default, nullptr}
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
