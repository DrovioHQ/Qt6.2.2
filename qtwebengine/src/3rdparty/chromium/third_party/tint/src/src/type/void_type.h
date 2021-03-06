// Copyright 2020 The Tint Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef SRC_TYPE_VOID_TYPE_H_
#define SRC_TYPE_VOID_TYPE_H_

#include <string>

#include "src/type/type.h"

namespace tint {
namespace type {

/// A void type
class Void : public Castable<Void, Type> {
 public:
  /// Constructor
  Void();
  /// Move constructor
  Void(Void&&);
  ~Void() override;

  /// @returns the name for this type
  std::string type_name() const override;

  /// @param symbols the program's symbol table
  /// @returns the name for this type that closely resembles how it would be
  /// declared in WGSL.
  std::string FriendlyName(const SymbolTable& symbols) const override;

  /// Clones this type and all transitive types using the `CloneContext` `ctx`.
  /// @param ctx the clone context
  /// @return the newly cloned type
  Void* Clone(CloneContext* ctx) const override;
};

}  // namespace type
}  // namespace tint

#endif  // SRC_TYPE_VOID_TYPE_H_
