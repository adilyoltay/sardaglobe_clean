#pragma once

#include "value.h"
#include <string>

class JsonParser {
public:
  explicit JsonParser(const std::string& input)
      : data_(input), cur_(data_.c_str()), end_(data_.c_str() + data_.size()) {}

  bool Parse(Value& out) {
    SkipWs();
    if (!ParseValue(out)) return false;
    SkipWs();
    return cur_ == end_ || *cur_ == '\0';
  }

private:
  const std::string data_;
  const char* cur_;
  const char* end_;

  void SkipWs() {
    while (cur_ < end_ && (*cur_ == ' ' || *cur_ == '\n' || *cur_ == '\r' || *cur_ == '\t')) {
      ++cur_;
    }
  }

  bool MatchLiteral(const char* lit) {
    size_t len = std::strlen(lit);
    if (static_cast<size_t>(end_ - cur_) < len) return false;
    if (std::strncmp(cur_, lit, len) == 0) {
      cur_ += len;
      return true;
    }
    return false;
  }

  bool ParseValue(Value& out) {
    SkipWs();
    if (cur_ >= end_) return false;
    char c = *cur_;
    if (c == '{') return ParseObject(out);
    if (c == '[') return ParseArray(out);
    if (c == '"') return ParseString(out);
    if (c == '-' || (c >= '0' && c <= '9')) return ParseNumber(out);
    if (MatchLiteral("true")) {
      out = Value::Bool(true);
      return true;
    }
    if (MatchLiteral("false")) {
      out = Value::Bool(false);
      return true;
    }
    if (MatchLiteral("null")) {
      out = Value::Null();
      return true;
    }
    return false;
  }

  bool ParseObject(Value& out) {
    if (*cur_ != '{') return false;
    ++cur_;
    out = Value::Object();
    SkipWs();
    if (cur_ < end_ && *cur_ == '}') {
      ++cur_;
      return true;
    }
    while (cur_ < end_) {
      Value keyVal;
      if (!ParseString(keyVal)) return false;
      std::string key = keyVal.AsString();
      SkipWs();
      if (cur_ >= end_ || *cur_ != ':') return false;
      ++cur_;
      Value val;
      if (!ParseValue(val)) return false;
      out.Set(key, val);
      SkipWs();
      if (cur_ < end_ && *cur_ == ',') {
        ++cur_;
        SkipWs();
        continue;
      }
      if (cur_ < end_ && *cur_ == '}') {
        ++cur_;
        return true;
      }
      return false;
    }
    return false;
  }

  bool ParseArray(Value& out) {
    if (*cur_ != '[') return false;
    ++cur_;
    out = Value::Array();
    SkipWs();
    if (cur_ < end_ && *cur_ == ']') {
      ++cur_;
      return true;
    }
    while (cur_ < end_) {
      Value val;
      if (!ParseValue(val)) return false;
      out.Push(val);
      SkipWs();
      if (cur_ < end_ && *cur_ == ',') {
        ++cur_;
        SkipWs();
        continue;
      }
      if (cur_ < end_ && *cur_ == ']') {
        ++cur_;
        return true;
      }
      return false;
    }
    return false;
  }

  bool ParseString(Value& out) {
    if (*cur_ != '"') return false;
    ++cur_;
    std::string result;
    while (cur_ < end_) {
      char c = *cur_++;
      if (c == '"') {
        out = Value::String(result);
        return true;
      }
      if (c == '\\') {
        if (cur_ >= end_) return false;
        char esc = *cur_++;
        switch (esc) {
          case '"': result.push_back('"'); break;
          case '\\': result.push_back('\\'); break;
          case '/': result.push_back('/'); break;
          case 'b': result.push_back('\b'); break;
          case 'f': result.push_back('\f'); break;
          case 'n': result.push_back('\n'); break;
          case 'r': result.push_back('\r'); break;
          case 't': result.push_back('\t'); break;
          case 'u': {
            if (end_ - cur_ < 4) return false;
            unsigned int code = 0;
            for (int i = 0; i < 4; ++i) {
              char h = *cur_++;
              code <<= 4;
              if (h >= '0' && h <= '9') code |= static_cast<unsigned int>(h - '0');
              else if (h >= 'a' && h <= 'f') code |= static_cast<unsigned int>(h - 'a' + 10);
              else if (h >= 'A' && h <= 'F') code |= static_cast<unsigned int>(h - 'A' + 10);
              else return false;
            }
            if (code <= 0x7F) {
              result.push_back(static_cast<char>(code));
            } else if (code <= 0x7FF) {
              result.push_back(static_cast<char>(0xC0 | (code >> 6)));
              result.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            } else {
              result.push_back(static_cast<char>(0xE0 | (code >> 12)));
              result.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
              result.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            }
            break;
          }
          default:
            return false;
        }
      } else {
        result.push_back(c);
      }
    }
    return false;
  }

  bool ParseNumber(Value& out) {
    char* end;
    double val = std::strtod(cur_, &end);
    if (cur_ == end) return false;
    cur_ = end;
    out = Value::Number(val);
    return true;
  }
};
