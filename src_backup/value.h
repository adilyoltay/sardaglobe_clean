#pragma once

#include <string>
#include <map>
#include <vector>
#include <memory>

struct Value {
  enum class Type {
    Null,
    Number,
    Boolean,
    String,
    Object,
    Array
  };

  Type type = Type::Null;
  double number = 0.0;
  bool boolean = false;
  std::string string;
  std::map<std::string, std::shared_ptr<Value>> object;
  std::vector<std::shared_ptr<Value>> array;

  Value() = default;

  static Value Null() {
    return Value();
  }

  static Value Number(double v) {
    Value val;
    val.type = Type::Number;
    val.number = v;
    return val;
  }

  static Value Bool(bool v) {
    Value val;
    val.type = Type::Boolean;
    val.boolean = v;
    return val;
  }

  static Value String(const std::string& v) {
    Value val;
    val.type = Type::String;
    val.string = v;
    return val;
  }

  static Value Object() {
    Value val;
    val.type = Type::Object;
    return val;
  }

  static Value Array() {
    Value val;
    val.type = Type::Array;
    return val;
  }

  void Set(const std::string& key, const Value& v) {
    if (type != Type::Object) {
      type = Type::Object;
      object.clear();
    }
    object[key] = std::make_shared<Value>(v);
  }

  void Push(const Value& v) {
    if (type != Type::Array) {
      type = Type::Array;
      array.clear();
    }
    array.push_back(std::make_shared<Value>(v));
  }

  Value Get(const std::string& key) const {
    if (type == Type::Object) {
      auto it = object.find(key);
      if (it != object.end() && it->second) {
        return *it->second;
      }
    }
    return Null();
  }

  Value At(size_t index) const {
    if (type == Type::Array && index < array.size() && array[index]) {
      return *array[index];
    }
    return Null();
  }

  size_t Size() const {
    if (type == Type::Array) return array.size();
    if (type == Type::Object) return object.size();
    return 0;
  }

  double AsNumber(double fallback = 0.0) const {
    return type == Type::Number ? number : fallback;
  }

  bool AsBool(bool fallback = false) const {
    return type == Type::Boolean ? boolean : fallback;
  }

  std::string AsString(const std::string& fallback = "") const {
    return type == Type::String ? string : fallback;
  }

  bool IsNull() const { return type == Type::Null; }
  bool IsNumber() const { return type == Type::Number; }
  bool IsBool() const { return type == Type::Boolean; }
  bool IsString() const { return type == Type::String; }
  bool IsObject() const { return type == Type::Object; }
  bool IsArray() const { return type == Type::Array; }
};
