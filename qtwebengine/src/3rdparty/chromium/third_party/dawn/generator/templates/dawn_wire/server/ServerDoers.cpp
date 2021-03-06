//* Copyright 2019 The Dawn Authors
//*
//* Licensed under the Apache License, Version 2.0 (the "License");
//* you may not use this file except in compliance with the License.
//* You may obtain a copy of the License at
//*
//*     http://www.apache.org/licenses/LICENSE-2.0
//*
//* Unless required by applicable law or agreed to in writing, software
//* distributed under the License is distributed on an "AS IS" BASIS,
//* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//* See the License for the specific language governing permissions and
//* limitations under the License.

#include "common/Assert.h"
#include "dawn_wire/server/Server.h"

namespace dawn_wire { namespace server {
    //* Implementation of the command doers
    {% for command in cmd_records["command"] %}
        {% set type = command.derived_object %}
        {% set method = command.derived_method %}
        {% set is_method = method is not none %}

        {% set Suffix = command.name.CamelCase() %}
        {% if Suffix not in client_side_commands %}
            {% if is_method and Suffix not in server_handwritten_commands %}
                bool Server::Do{{Suffix}}(
                    {%- for member in command.members -%}
                        {%- if member.is_return_value -%}
                            {%- if member.handle_type -%}
                                {{as_cType(member.handle_type.name)}}* {{as_varName(member.name)}}
                            {%- else -%}
                                {{as_cType(member.type.name)}}* {{as_varName(member.name)}}
                            {%- endif -%}
                        {%- else -%}
                            {{as_annotated_cType(member)}}
                        {%- endif -%}
                        {%- if not loop.last -%}, {% endif %}
                    {%- endfor -%}
                ) {
                    {% set ret = command.members|selectattr("is_return_value")|list %}
                    //* If there is a return value, assign it.
                    {% if ret|length == 1 %}
                        *{{as_varName(ret[0].name)}} =
                    {% else %}
                        //* Only one member should be a return value.
                        {{ assert(ret|length == 0) }}
                    {% endif %}
                    mProcs.{{as_varName(type.name, method.name)}}(
                        {%- for member in command.members if not member.is_return_value -%}
                            {{as_varName(member.name)}}
                            {%- if not loop.last -%}, {% endif %}
                        {%- endfor -%}
                    );
                    {% if ret|length == 1 %}
                        //* WebGPU error handling guarantees that no null object can be returned by
                        //* object creation functions.
                        ASSERT(*{{as_varName(ret[0].name)}} != nullptr);
                    {% endif %}
                    return true;
                }
            {% endif %}
        {% endif %}
    {% endfor %}

    bool Server::DoDestroyObject(ObjectType objectType, ObjectId objectId) {
        //* ID 0 are reserved for nullptr and cannot be destroyed.
        if (objectId == 0) {
            return false;
        }

        switch(objectType) {
            {% for type in by_category["object"] %}
                case ObjectType::{{type.name.CamelCase()}}: {
                    auto* data = {{type.name.CamelCase()}}Objects().Get(objectId);
                    if (data == nullptr) {
                        return false;
                    }
                    if (data->deviceInfo != nullptr) {
                        if (!UntrackDeviceChild(data->deviceInfo, objectType, objectId)) {
                            return false;
                        }
                    }
                    if (data->state == AllocationState::Allocated) {
                        ASSERT(data->handle != nullptr);
                        {% if type.name.CamelCase() in server_reverse_lookup_objects %}
                            {{type.name.CamelCase()}}ObjectIdTable().Remove(data->handle);
                        {% endif %}

                        {% if type.name.get() == "device" %}
                            //* TODO(crbug.com/dawn/384): This is a hack to make sure that all child objects
                            //* are destroyed before their device. We should have a solution in
                            //* Dawn native that makes all child objects internally null if their
                            //* Device is destroyed.
                            while (data->info->childObjectTypesAndIds.size() > 0) {
                                ObjectType childObjectType;
                                ObjectId childObjectId;
                                std::tie(childObjectType, childObjectId) = UnpackObjectTypeAndId(
                                    *data->info->childObjectTypesAndIds.begin());
                                if (!DoDestroyObject(childObjectType, childObjectId)) {
                                    return false;
                                }
                            }
                            if (data->handle != nullptr) {
                                //* Deregisters uncaptured error and device lost callbacks since
                                //* they should not be forwarded if the device no longer exists on the wire.
                                ClearDeviceCallbacks(data->handle);
                            }
                        {% endif %}

                        mProcs.{{as_varName(type.name, Name("release"))}}(data->handle);
                    }
                    {{type.name.CamelCase()}}Objects().Free(objectId);
                    return true;
                }
            {% endfor %}
            default:
                return false;
        }

        return true;
    }

}}  // namespace dawn_wire::server
