// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include "device/fido/fido_transport_protocol.h"
#include "device/fido/public_key_credential_descriptor.h"

namespace device {

namespace {

// Keys for storing credential descriptor information in CBOR map.
constexpr char kCredentialIdKey[] = "id";
constexpr char kCredentialTypeKey[] = "type";
constexpr char kTransportsKey[] = "transports";

}  // namespace

// static
base::Optional<PublicKeyCredentialDescriptor>
PublicKeyCredentialDescriptor::CreateFromCBORValue(const cbor::Value& cbor) {
  if (!cbor.is_map()) {
    return base::nullopt;
  }

  const cbor::Value::MapValue& map = cbor.GetMap();
  auto type = map.find(cbor::Value(kCredentialTypeKey));
  if (type == map.end() || !type->second.is_string() ||
      type->second.GetString() != kPublicKey)
    return base::nullopt;

  auto id = map.find(cbor::Value(kCredentialIdKey));
  if (id == map.end() || !id->second.is_bytestring())
    return base::nullopt;

  auto transports_it = map.find(cbor::Value(kTransportsKey));
  if (transports_it == map.end())
    return PublicKeyCredentialDescriptor(CredentialType::kPublicKey,
                                         id->second.GetBytestring());
  if (!transports_it->second.is_array())
    return base::nullopt;
  base::flat_set<FidoTransportProtocol> transports;
  for (const cbor::Value& transport_name : transports_it->second.GetArray()) {
    if (!transport_name.is_string()) {
      return base::nullopt;
    }
    base::Optional<FidoTransportProtocol> transport =
        ConvertToFidoTransportProtocol(transport_name.GetString());
    if (!transport) {
      continue;
    }
    transports.insert(*transport);
  }

  return PublicKeyCredentialDescriptor(CredentialType::kPublicKey,
                                       id->second.GetBytestring(),
                                       std::move(transports));
}

PublicKeyCredentialDescriptor::PublicKeyCredentialDescriptor() = default;

PublicKeyCredentialDescriptor::PublicKeyCredentialDescriptor(
    CredentialType credential_type,
    std::vector<uint8_t> id)
    : PublicKeyCredentialDescriptor(credential_type, std::move(id), {}) {}

PublicKeyCredentialDescriptor::PublicKeyCredentialDescriptor(
    CredentialType credential_type,
    std::vector<uint8_t> id,
    base::flat_set<FidoTransportProtocol> transports)
    : credential_type_(credential_type),
      id_(std::move(id)),
      transports_(std::move(transports)) {}

PublicKeyCredentialDescriptor::PublicKeyCredentialDescriptor(
    const PublicKeyCredentialDescriptor& other) = default;

PublicKeyCredentialDescriptor::PublicKeyCredentialDescriptor(
    PublicKeyCredentialDescriptor&& other) = default;

PublicKeyCredentialDescriptor& PublicKeyCredentialDescriptor::operator=(
    const PublicKeyCredentialDescriptor& other) = default;

PublicKeyCredentialDescriptor& PublicKeyCredentialDescriptor::operator=(
    PublicKeyCredentialDescriptor&& other) = default;

PublicKeyCredentialDescriptor::~PublicKeyCredentialDescriptor() = default;

bool PublicKeyCredentialDescriptor::operator==(
    const PublicKeyCredentialDescriptor& other) const {
  return credential_type_ == other.credential_type_ && id_ == other.id_ &&
         transports_ == other.transports_;
}

cbor::Value AsCBOR(const PublicKeyCredentialDescriptor& desc) {
  cbor::Value::MapValue cbor_descriptor_map;
  cbor_descriptor_map[cbor::Value(kCredentialIdKey)] = cbor::Value(desc.id());
  cbor_descriptor_map[cbor::Value(kCredentialTypeKey)] =
      cbor::Value(CredentialTypeToString(desc.credential_type()));
  std::vector<cbor::Value> transports;
  for (FidoTransportProtocol transport : desc.transports()) {
    transports.emplace_back(cbor::Value(ToString(transport)));
  }
  if (!transports.empty()) {
    cbor_descriptor_map[cbor::Value(kTransportsKey)] =
        cbor::Value(std::move(transports));
  }
  return cbor::Value(std::move(cbor_descriptor_map));
}

}  // namespace device
