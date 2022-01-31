// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_BINDINGS_CONNECTIVITY_DATA_GENERATOR_H_
#define DIAGNOSTICS_BINDINGS_CONNECTIVITY_DATA_GENERATOR_H_

#include <memory>
#include <string>
#include <type_traits>

#include <absl/types/optional.h>

#include "diagnostics/bindings/connectivity/context.h"

namespace diagnostics {
namespace bindings {
namespace connectivity {

template <typename T>
class DataGeneratorInterface {
 public:
  using Type = T;
  // Generates T for TestConsumer and TestProvider to test the parameters. This
  // should return value even if |HasNext()| is false.
  virtual T Generate() = 0;
  // Returns true if there are values need to be generated by |Generate|. Most
  // of the cases this only returns true before the first |Generate|. Some types
  // require more than one |Generate| to test different values.
  virtual bool HasNext() = 0;
};

// Generator for primitive c++ types and string.
template <typename T>
class DataGenerator : public DataGeneratorInterface<T> {
  static_assert(std::is_same_v<T, bool> || std::is_same_v<T, int8_t> ||
                    std::is_same_v<T, uint8_t> || std::is_same_v<T, int16_t> ||
                    std::is_same_v<T, uint16_t> || std::is_same_v<T, int32_t> ||
                    std::is_same_v<T, uint32_t> || std::is_same_v<T, int64_t> ||
                    std::is_same_v<T, uint64_t> || std::is_same_v<T, float> ||
                    std::is_same_v<T, double> || std::is_same_v<T, std::string>,
                "Undefined DataGenerator type.");

 public:
  DataGenerator(const DataGenerator&) = delete;
  DataGenerator& operator=(const DataGenerator&) = delete;
  virtual ~DataGenerator() = default;

  static std::unique_ptr<DataGenerator> Create(Context*) {
    return std::unique_ptr<DataGenerator>(new DataGenerator<T>());
  }

 public:
  // DataGeneratorInterface overrides.
  T Generate() override {
    has_next_ = false;
    return T();
  }

  bool HasNext() override { return has_next_; }

 protected:
  DataGenerator() = default;

 private:
  bool has_next_ = true;
};

// Generator for optional types.
template <typename GeneratorType>
class OptionalGenerator : public DataGeneratorInterface<
                              absl::optional<typename GeneratorType::Type>> {
 public:
  OptionalGenerator(const OptionalGenerator&) = delete;
  OptionalGenerator& operator=(const OptionalGenerator&) = delete;
  virtual ~OptionalGenerator() = default;

  static std::unique_ptr<OptionalGenerator> Create(Context* context) {
    return std::unique_ptr<OptionalGenerator>(
        new OptionalGenerator<GeneratorType>(context));
  }

 public:
  // DataGeneratorInterface overrides.
  absl::optional<typename GeneratorType::Type> Generate() override {
    if (generator_->HasNext())
      return generator_->Generate();
    returned_null_ = true;
    return absl::nullopt;
  }

  bool HasNext() override { return !returned_null_ || generator_->HasNext(); }

 protected:
  explicit OptionalGenerator(Context* context) {
    generator_ = GeneratorType::Create(context);
  }

 private:
  std::unique_ptr<GeneratorType> generator_;
  bool returned_null_ = false;
};

}  // namespace connectivity
}  // namespace bindings
}  // namespace diagnostics

#endif  // DIAGNOSTICS_BINDINGS_CONNECTIVITY_DATA_GENERATOR_H_