# Copyright 2020 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import web_idl

from . import name_style
from .blink_v8_bridge import blink_class_name
from .blink_v8_bridge import blink_type_info
from .blink_v8_bridge import make_v8_to_blink_value
from .blink_v8_bridge import native_value_tag
from .blink_v8_bridge import v8_bridge_class_name
from .code_node import EmptyNode
from .code_node import ListNode
from .code_node import SymbolNode
from .code_node import SymbolScopeNode
from .code_node import TextNode
from .code_node_cxx import CxxBlockNode
from .code_node_cxx import CxxClassDefNode
from .code_node_cxx import CxxFuncDeclNode
from .code_node_cxx import CxxFuncDefNode
from .code_node_cxx import CxxNamespaceNode
from .code_node_cxx import CxxSwitchNode
from .code_node_cxx import CxxUnlikelyIfNode
from .codegen_accumulator import CodeGenAccumulator
from .codegen_context import CodeGenContext
from .codegen_format import format_template as _format
from .codegen_utils import collect_forward_decls_and_include_headers
from .codegen_utils import component_export
from .codegen_utils import component_export_header
from .codegen_utils import enclose_with_header_guard
from .codegen_utils import make_copyright_header
from .codegen_utils import make_forward_declarations
from .codegen_utils import make_header_include_directives
from .codegen_utils import write_code_node_to_file
from .mako_renderer import MakoRenderer
from .package_initializer import package_initializer
from .path_manager import PathManager
from .task_queue import TaskQueue


class _UnionMember(object):
    def __init__(self, base_name):
        assert isinstance(base_name, str)

        self._base_name = base_name
        self._is_null = False
        # Do not apply |name_style| in order to respect the original name
        # (Web spec'ed name) as much as possible.
        self._content_type = "k{}".format(self._base_name)
        self._api_pred = "Is{}".format(self._base_name)
        self._api_get = "GetAs{}".format(self._base_name)
        self._api_set = "Set"
        self._var_name = name_style.member_var("member", self._base_name)
        self._idl_type = None
        self._type_info = None
        self._typedef_aliases = ()

    @property
    def is_null(self):
        return self._is_null

    def content_type(self, with_enum_name=True):
        if with_enum_name:
            return "ContentType::{}".format(self._content_type)
        else:
            return self._content_type

    @property
    def api_pred(self):
        return self._api_pred

    @property
    def api_get(self):
        return self._api_get

    @property
    def api_set(self):
        return self._api_set

    @property
    def var_name(self):
        return self._var_name

    @property
    def idl_type(self):
        return self._idl_type

    @property
    def type_info(self):
        return self._type_info

    @property
    def typedef_aliases(self):
        return self._typedef_aliases


class _UnionMemberImpl(_UnionMember):
    def __init__(self, union, idl_type):
        assert isinstance(union, web_idl.NewUnion)
        assert idl_type is None or isinstance(idl_type, web_idl.IdlType)

        if idl_type is None:
            base_name = "Null"
        else:
            base_name = idl_type.type_name_with_extended_attribute_key_values

        _UnionMember.__init__(self, base_name=base_name)
        self._is_null = idl_type is None
        if not self._is_null:
            self._idl_type = idl_type
            self._type_info = blink_type_info(idl_type)
        self._typedef_aliases = tuple([
            _UnionMemberAlias(impl=self, typedef=typedef)
            for typedef in union.typedef_members
            if typedef.idl_type == idl_type
        ])


class _UnionMemberSubunion(_UnionMember):
    def __init__(self, union, subunion):
        assert isinstance(union, web_idl.NewUnion)
        assert isinstance(subunion, web_idl.NewUnion)

        _UnionMember.__init__(self, base_name=blink_class_name(subunion))
        self._type_info = blink_type_info(subunion.idl_types[0],
                                          use_new_union=True)
        self._typedef_aliases = tuple(
            map(lambda typedef: _UnionMemberAlias(impl=self, typedef=typedef),
                subunion.aliasing_typedefs))
        self._blink_class_name = blink_class_name(subunion)

    @property
    def blink_class_name(self):
        return self._blink_class_name


class _UnionMemberAlias(_UnionMember):
    def __init__(self, impl, typedef):
        assert isinstance(impl, (_UnionMemberImpl, _UnionMemberSubunion))
        assert isinstance(typedef, web_idl.Typedef)

        _UnionMember.__init__(self, base_name=typedef.identifier)
        self._var_name = impl.var_name
        self._type_info = impl.type_info


def create_union_members(union):
    assert isinstance(union, web_idl.NewUnion)

    union_members = map(
        lambda member_type: _UnionMemberImpl(union, member_type),
        union.flattened_member_types)
    if union.does_include_nullable_type:
        union_members.append(_UnionMemberImpl(union, idl_type=None))
    return tuple(union_members)


def make_content_type_enum_class_def(cg_context):
    assert isinstance(cg_context, CodeGenContext)

    entries = []
    for member in cg_context.union_members:
        entries.append(member.content_type(with_enum_name=False))
        for alias in member.typedef_aliases:
            entries.append("{} = {}".format(
                alias.content_type(with_enum_name=False),
                member.content_type(with_enum_name=False)))

    return ListNode([
        TextNode("// The type of the content value of this IDL union."),
        TextNode("enum class ContentType {"),
        ListNode(map(TextNode, entries), separator=", "),
        TextNode("};"),
    ])


def make_factory_methods(cg_context):
    assert isinstance(cg_context, CodeGenContext)

    S = SymbolNode
    T = TextNode

    func_decl = CxxFuncDeclNode(name="Create",
                                arg_decls=[
                                    "v8::Isolate* isolate",
                                    "v8::Local<v8::Value> v8_value",
                                    "ExceptionState& exception_state",
                                ],
                                return_type="${class_name}*",
                                static=True)

    func_def = CxxFuncDefNode(name="Create",
                              arg_decls=[
                                  "v8::Isolate* isolate",
                                  "v8::Local<v8::Value> v8_value",
                                  "ExceptionState& exception_state",
                              ],
                              return_type="${class_name}*",
                              class_name="${class_name}")
    func_def.set_base_template_vars(cg_context.template_bindings())

    body = func_def.body
    body.add_template_vars({
        "isolate": "isolate",
        "v8_value": "v8_value",
        "exception_state": "exception_state",
    })

    # Create an instance from v8::Value based on the overload resolution
    # algorithm.
    # https://heycam.github.io/webidl/#dfn-overload-resolution-algorithm

    union_members = cg_context.union_members
    member = None  # Will be a found member in union_members.

    def find_by_member(test):
        for member in union_members:
            if test(member):
                return member
        return None

    def find_by_type(test):
        for member in union_members:
            if member.idl_type and test(member.idl_type):
                return member
        return None

    def dispatch_if(cond_text, value_symbol=None):
        scope_node = SymbolScopeNode(
            [T("return MakeGarbageCollected<${class_name}>(${blink_value});")])
        if not value_symbol:
            value_symbol = make_v8_to_blink_value(
                "blink_value",
                "${v8_value}",
                member.idl_type,
                error_exit_return_statement="return nullptr;")
        scope_node.register_code_symbol(value_symbol)
        if cond_text is True:
            body.append(CxxBlockNode(body=scope_node))
        else:
            body.append(CxxUnlikelyIfNode(cond=cond_text, body=scope_node))

    # 12.3. if V is null or undefined, ...
    member = find_by_member(lambda m: m.is_null)
    if member:
        dispatch_if("${v8_value}->IsNullOrUndefined()",
                    S("blink_value", "auto&& ${blink_value} = nullptr;"))

    # 12.4. if V is a platform object, ...
    interface_members = filter(
        lambda member: member.idl_type and member.idl_type.is_interface,
        union_members)
    interface_members = sorted(
        interface_members,
        key=lambda member: (len(member.idl_type.type_definition_object.
                                inclusive_inherited_interfaces), member.
                            idl_type.type_definition_object.identifier),
        reverse=True)
    # Attempt to match from most derived to least derived.
    for member in interface_members:
        v8_bridge_name = v8_bridge_class_name(
            member.idl_type.type_definition_object)
        dispatch_if(
            _format("{}::HasInstance(${isolate}, ${v8_value})",
                    v8_bridge_name))

    # 12.5. if Type(V) is Object, V has an [[ArrayBufferData]] internal
    #   slot, ...
    member = find_by_type(lambda t: t.is_array_buffer)
    if member:
        dispatch_if("${v8_value}->IsArrayBuffer() || "
                    "${v8_value}->IsSharedArrayBuffer()")

    # V8 specific optimization: ArrayBufferView
    member = find_by_type(lambda t: t.is_array_buffer_view)
    if member:
        dispatch_if("${v8_value}->IsArrayBufferView()")

    # 12.6. if Type(V) is Object, V has a [[DataView]] internal slot, ...
    member = find_by_type(lambda t: t.is_data_view)
    if member:
        dispatch_if("${v8_value}->IsDataView()")

    # 12.7. if Type(V) is Object, V has a [[TypedArrayName]] internal slot, ...
    typed_array_types = ("Int8Array", "Int16Array", "Int32Array", "Uint8Array",
                         "Uint16Array", "Uint32Array", "Uint8ClampedArray",
                         "Float32Array", "Float64Array")
    for typed_array_type in typed_array_types:
        member = find_by_type(lambda t: t.keyword_typename == typed_array_type)
        if member:
            dispatch_if(_format("${v8_value}->Is{}()", typed_array_type))

    # 12.8. if IsCallable(V) is true, ...
    member = find_by_type(lambda t: t.is_callback_function)
    if member:
        dispatch_if("${v8_value}->IsFunction()")

    # 12.9. if Type(V) is Object and ... @@iterator ...
    member = find_by_type(lambda t: t.is_sequence or t.is_frozen_array)
    if member:
        dispatch_if("${v8_value}->IsArray() || "  # Excessive optimization
                    "bindings::IsEsIterableObject"
                    "(${isolate}, ${v8_value}, ${exception_state})")
        body.append(
            CxxUnlikelyIfNode(cond="${exception_state}.HadException()",
                              body=T("return nullptr;")))

    # 12.10. if Type(V) is Object and ...
    member = find_by_type(lambda t: t.is_callback_interface or t.is_dictionary
                          or t.is_record or t.is_object)
    if member:
        dispatch_if("${v8_value}->IsObject()")

    # 12.11. if Type(V) is Boolean and ...
    member = find_by_type(lambda t: t.is_boolean)
    if member:
        dispatch_if("${v8_value}->IsBoolean()")

    # 12.12. if Type(V) is Number and ...
    member = find_by_type(lambda t: t.is_numeric)
    if member:
        dispatch_if("${v8_value}->IsNumber()")

    # 12.13. if there is an entry in S that has ... a string type ...
    # 12.14. if there is an entry in S that has ... a numeric type ...
    # 12.15. if there is an entry in S that has ... boolean ...
    member = find_by_type(lambda t: t.is_enumeration or t.is_string or t.
                          is_numeric or t.is_boolean)
    if member:
        dispatch_if(True)
    else:
        body.append(
            T("${exception_state}.ThrowTypeError("
              "ExceptionMessages::ValueNotOfType("
              "UnionNameInIDL().Ascii().c_str()));"))
        body.append(T("return nullptr;"))

    return func_decl, func_def


def make_constructors(cg_context):
    assert isinstance(cg_context, CodeGenContext)

    decls = ListNode()

    for member in cg_context.union_members:
        if member.is_null:
            func_def = CxxFuncDefNode(name=cg_context.class_name,
                                      arg_decls=["std::nullptr_t"],
                                      return_type="",
                                      explicit=True,
                                      member_initializer_list=[
                                          "content_type_({})".format(
                                              member.content_type()),
                                      ])
        else:
            func_def = CxxFuncDefNode(
                name=cg_context.class_name,
                arg_decls=["{} value".format(member.type_info.member_ref_t)],
                return_type="",
                explicit=True,
                member_initializer_list=[
                    "content_type_({})".format(member.content_type()),
                    "{}(value)".format(member.var_name),
                ])
        decls.append(func_def)

    return decls, None


def make_accessor_functions(cg_context):
    assert isinstance(cg_context, CodeGenContext)

    T = TextNode
    F = lambda *args, **kwargs: T(_format(*args, **kwargs))

    decls = ListNode()
    defs = ListNode()

    func_def = CxxFuncDefNode(name="GetContentType",
                              arg_decls=[],
                              return_type="ContentType",
                              const=True)
    func_def.set_base_template_vars(cg_context.template_bindings())
    func_def.body.append(T("return content_type_;"))
    decls.extend([
        T("// Returns the type of the content value."),
        func_def,
        EmptyNode(),
    ])

    def make_api_pred(member):
        func_def = CxxFuncDefNode(name=member.api_pred,
                                  arg_decls=[],
                                  return_type="bool",
                                  const=True)
        func_def.set_base_template_vars(cg_context.template_bindings())
        func_def.body.append(
            F("return content_type_ == {};", member.content_type()))
        return func_def

    def make_api_get(member):
        func_def = CxxFuncDefNode(name=member.api_get,
                                  arg_decls=[],
                                  return_type=member.type_info.member_ref_t,
                                  const=True)
        func_def.set_base_template_vars(cg_context.template_bindings())
        func_def.body.extend([
            F("DCHECK_EQ(content_type_, {});", member.content_type()),
            F("return {};", member.var_name),
        ])
        return func_def

    def make_api_set(member):
        func_def = CxxFuncDefNode(
            name=member.api_set,
            arg_decls=["{} value".format(member.type_info.member_ref_t)],
            return_type="void")
        func_def.set_base_template_vars(cg_context.template_bindings())
        func_def.body.extend([
            T("Clear();"),
            F("{} = value;", member.var_name),
            F("content_type_ = {};", member.content_type()),
        ])
        return func_def

    def make_api_set_null(member):
        func_def = CxxFuncDefNode(name=member.api_set,
                                  arg_decls=["std::nullptr_t"],
                                  return_type="void")
        func_def.set_base_template_vars(cg_context.template_bindings())
        func_def.body.extend([
            T("Clear();"),
            F("content_type_ = {};", member.content_type()),
        ])
        return func_def

    for member in cg_context.union_members:
        if member.is_null:
            decls.append(make_api_pred(member))
            decls.append(make_api_set_null(member))
        else:
            decls.append(make_api_pred(member))
            for alias in member.typedef_aliases:
                decls.append(make_api_pred(alias))
            decls.append(make_api_get(member))
            for alias in member.typedef_aliases:
                decls.append(make_api_get(alias))
            decls.append(make_api_set(member))
        decls.append(EmptyNode())

    def make_api_subunion_pred(subunion, subunion_members):
        func_def = CxxFuncDefNode(name=subunion.api_pred,
                                  arg_decls=[],
                                  return_type="bool",
                                  const=True)
        func_def.set_base_template_vars(cg_context.template_bindings())
        expr = " || ".join(
            map(
                lambda member: "content_type_ == {}".format(
                    member.content_type()), subunion_members))
        func_def.body.append(F("return {};", expr))
        return func_def, None

    def make_api_subunion_get(subunion, subunion_members):
        func_decl = CxxFuncDeclNode(name=subunion.api_get,
                                    arg_decls=[],
                                    return_type=subunion.type_info.value_t,
                                    const=True)
        func_def = CxxFuncDefNode(name=subunion.api_get,
                                  arg_decls=[],
                                  return_type=subunion.type_info.value_t,
                                  const=True,
                                  class_name=cg_context.class_name)
        func_def.set_base_template_vars(cg_context.template_bindings())
        node = CxxSwitchNode(cond="content_type_")
        node.append(case=None,
                    body=[T("NOTREACHED();"),
                          T("return nullptr;")],
                    should_add_break=False)
        for member in subunion_members:
            node.append(case=member.content_type(),
                        body=F("return MakeGarbageCollected<{}>({}());",
                               subunion.blink_class_name, member.api_get),
                        should_add_break=False)
        func_def.body.append(node)
        return func_decl, func_def

    def make_api_subunion_set(subunion, subunion_members):
        func_decl = CxxFuncDeclNode(
            name=subunion.api_set,
            arg_decls=["{} value".format(subunion.type_info.const_ref_t)],
            return_type="void")
        func_def = CxxFuncDefNode(
            name=subunion.api_set,
            arg_decls=["{} value".format(subunion.type_info.const_ref_t)],
            return_type="void",
            class_name=cg_context.class_name)
        func_def.set_base_template_vars(cg_context.template_bindings())
        node = CxxSwitchNode(cond="value->GetContentType()")
        for member in subunion_members:
            node.append(case=F("{}::{}", subunion.blink_class_name,
                               member.content_type()),
                        body=F("Set(value->{}());", member.api_get))
        func_def.body.append(node)
        return func_decl, func_def

    for subunion in cg_context.union.union_members:
        subunion_members = create_union_members(subunion)
        subunion = _UnionMemberSubunion(cg_context.union, subunion)
        func_decl, func_def = make_api_subunion_pred(subunion,
                                                     subunion_members)
        decls.append(func_decl)
        defs.append(func_def)
        defs.append(EmptyNode())
        func_decl, func_def = make_api_subunion_get(subunion, subunion_members)
        decls.append(func_decl)
        defs.append(func_def)
        defs.append(EmptyNode())
        func_decl, func_def = make_api_subunion_set(subunion, subunion_members)
        decls.append(func_decl)
        defs.append(func_def)
        defs.append(EmptyNode())
        decls.append(EmptyNode())

    return decls, defs


def make_clear_function(cg_context):
    assert isinstance(cg_context, CodeGenContext)

    func_decl = CxxFuncDeclNode(name="Clear", arg_decls=[], return_type="void")

    func_def = CxxFuncDefNode(name="Clear",
                              arg_decls=[],
                              return_type="void",
                              class_name=cg_context.class_name)
    func_def.set_base_template_vars(cg_context.template_bindings())
    body = func_def.body

    for member in cg_context.union_members:
        if member.is_null:
            continue
        clear_expr = member.type_info.clear_member_var_expr(member.var_name)
        if clear_expr:
            body.append(TextNode("{};".format(clear_expr)))

    return func_decl, func_def


def make_tov8value_function(cg_context):
    assert isinstance(cg_context, CodeGenContext)

    func_decl = CxxFuncDeclNode(name="ToV8Value",
                                arg_decls=["ScriptState* script_state"],
                                return_type="v8::MaybeLocal<v8::Value>",
                                override=True)

    func_def = CxxFuncDefNode(name="ToV8Value",
                              arg_decls=["ScriptState* script_state"],
                              return_type="v8::MaybeLocal<v8::Value>",
                              class_name=cg_context.class_name)
    func_def.set_base_template_vars(cg_context.template_bindings())
    body = func_def.body
    body.add_template_vars({"script_state": "script_state"})

    branches = CxxSwitchNode(cond="content_type_")
    for member in cg_context.union_members:
        if member.is_null:
            text = "return v8::Null(${script_state}->GetIsolate());"
        else:
            text = _format("return ToV8Traits<{}>::ToV8(${script_state}, {});",
                           native_value_tag(member.idl_type), member.var_name)
        branches.append(case=member.content_type(),
                        body=TextNode(text),
                        should_add_break=False)

    body.extend([
        branches,
        EmptyNode(),
        TextNode("NOTREACHED();"),
        TextNode("return v8::MaybeLocal<v8::Value>();"),
    ])

    return func_decl, func_def


def make_trace_function(cg_context):
    assert isinstance(cg_context, CodeGenContext)

    func_decl = CxxFuncDeclNode(name="Trace",
                                arg_decls=["Visitor* visitor"],
                                return_type="void",
                                const=True,
                                override=True)

    func_def = CxxFuncDefNode(name="Trace",
                              arg_decls=["Visitor* visitor"],
                              return_type="void",
                              class_name=cg_context.class_name,
                              const=True)
    func_def.set_base_template_vars(cg_context.template_bindings())
    body = func_def.body

    for member in cg_context.union_members:
        if member.is_null:
            continue
        body.append(
            TextNode("TraceIfNeeded<{}>::Trace(visitor, {});".format(
                member.type_info.member_t, member.var_name)))
    body.append(TextNode("${base_class_name}::Trace(visitor);"))

    return func_decl, func_def


def make_name_function(cg_context):
    assert isinstance(cg_context, CodeGenContext)

    func_def = CxxFuncDefNode(name="UnionNameInIDL",
                              arg_decls=[],
                              return_type="String",
                              static=True)
    func_def.set_base_template_vars(cg_context.template_bindings())
    body = func_def.body

    body.extend([
        TextNode("static constexpr const char* const member_names[] = {"),
        ListNode(list(
            map(
                lambda name: TextNode("\"{}\"".format(name)),
                sorted(
                    map(lambda idl_type: idl_type.syntactic_form,
                        cg_context.union.flattened_member_types)))),
                 separator=", "),
        TextNode("};"),
        TextNode("return ProduceUnionNameInIDL(member_names);"),
    ])

    return func_def, None


def make_member_vars_def(cg_context):
    assert isinstance(cg_context, CodeGenContext)

    member_vars_def = ListNode()
    member_vars_def.extend([
        TextNode("ContentType content_type_;"),
        EmptyNode(),
    ])

    entries = [
        "{} {};".format(member.type_info.member_t, member.var_name)
        for member in cg_context.union_members if not member.is_null
    ]
    member_vars_def.extend(map(TextNode, entries))

    return member_vars_def


def generate_union(union_identifier):
    assert isinstance(union_identifier, web_idl.Identifier)

    web_idl_database = package_initializer().web_idl_database()
    union = web_idl_database.find(union_identifier)

    path_manager = PathManager(union)
    assert path_manager.api_component == path_manager.impl_component
    api_component = path_manager.api_component
    for_testing = union.code_generator_info.for_testing

    # Class names
    class_name = blink_class_name(union)

    cg_context = CodeGenContext(union=union,
                                union_members=create_union_members(union),
                                class_name=class_name,
                                base_class_name="bindings::UnionBase")

    # Filepaths
    header_path = path_manager.api_path(ext="h")
    source_path = path_manager.api_path(ext="cc")

    # Root nodes
    header_node = ListNode(tail="\n")
    header_node.set_accumulator(CodeGenAccumulator())
    header_node.set_renderer(MakoRenderer())
    source_node = ListNode(tail="\n")
    source_node.set_accumulator(CodeGenAccumulator())
    source_node.set_renderer(MakoRenderer())

    # Namespaces
    header_blink_ns = CxxNamespaceNode(name_style.namespace("blink"))
    source_blink_ns = CxxNamespaceNode(name_style.namespace("blink"))

    # Class definition
    class_def = CxxClassDefNode(cg_context.class_name,
                                base_class_names=["bindings::UnionBase"],
                                final=True,
                                export=component_export(
                                    api_component, for_testing))
    class_def.set_base_template_vars(cg_context.template_bindings())

    # Implementation parts
    content_type_enum_class_def = make_content_type_enum_class_def(cg_context)
    factory_decls, factory_defs = make_factory_methods(cg_context)
    ctor_decls, ctor_defs = make_constructors(cg_context)
    accessor_decls, accessor_defs = make_accessor_functions(cg_context)
    clear_func_decls, clear_func_defs = make_clear_function(cg_context)
    tov8value_func_decls, tov8value_func_defs = make_tov8value_function(
        cg_context)
    trace_func_decls, trace_func_defs = make_trace_function(cg_context)
    name_func_decls, name_func_defs = make_name_function(cg_context)
    member_vars_def = make_member_vars_def(cg_context)

    # Header part (copyright, include directives, and forward declarations)
    header_node.extend([
        make_copyright_header(),
        EmptyNode(),
        enclose_with_header_guard(
            ListNode([
                make_header_include_directives(header_node.accumulator),
                EmptyNode(),
                header_blink_ns,
            ]), name_style.header_guard(header_path)),
    ])
    header_blink_ns.body.extend([
        make_forward_declarations(header_node.accumulator),
        EmptyNode(),
    ])
    source_node.extend([
        make_copyright_header(),
        EmptyNode(),
        TextNode("#include \"{}\"".format(header_path)),
        EmptyNode(),
        make_header_include_directives(source_node.accumulator),
        EmptyNode(),
        source_blink_ns,
    ])
    source_blink_ns.body.extend([
        make_forward_declarations(source_node.accumulator),
        EmptyNode(),
    ])

    # Assemble the parts.
    header_node.accumulator.add_class_decls([
        "ExceptionState",
    ])
    header_node.accumulator.add_include_headers([
        component_export_header(api_component, for_testing),
        "third_party/blink/renderer/platform/bindings/union_base.h",
    ])
    source_node.accumulator.add_include_headers([
        "third_party/blink/renderer/bindings/core/v8/generated_code_helper.h",
        "third_party/blink/renderer/bindings/core/v8/native_value_traits_impl.h",
        "third_party/blink/renderer/bindings/core/v8/to_v8_traits.h",
        "third_party/blink/renderer/platform/bindings/exception_state.h",
    ])
    header_node.accumulator.add_class_decls(
        map(blink_class_name, union.union_members))
    source_node.accumulator.add_include_headers(
        map(lambda subunion: PathManager(subunion).api_path(ext="h"),
            union.union_members))
    source_node.accumulator.add_include_headers([
        PathManager(idl_type.type_definition_object).api_path(ext="h")
        for idl_type in union.flattened_member_types if idl_type.is_interface
    ])
    (header_forward_decls, header_include_headers, source_forward_decls,
     source_include_headers) = collect_forward_decls_and_include_headers(
         union.flattened_member_types)
    header_node.accumulator.add_class_decls(header_forward_decls)
    header_node.accumulator.add_include_headers(header_include_headers)
    source_node.accumulator.add_class_decls(source_forward_decls)
    source_node.accumulator.add_include_headers(source_include_headers)

    header_blink_ns.body.append(class_def)
    header_blink_ns.body.append(EmptyNode())

    class_def.public_section.append(content_type_enum_class_def)
    class_def.public_section.append(EmptyNode())

    class_def.public_section.append(factory_decls)
    class_def.public_section.append(EmptyNode())
    source_blink_ns.body.append(factory_defs)
    source_blink_ns.body.append(EmptyNode())

    class_def.public_section.append(ctor_decls)
    class_def.public_section.append(EmptyNode())
    source_blink_ns.body.append(ctor_defs)
    source_blink_ns.body.append(EmptyNode())

    class_def.public_section.append(accessor_decls)
    class_def.public_section.append(EmptyNode())
    source_blink_ns.body.append(accessor_defs)
    source_blink_ns.body.append(EmptyNode())

    class_def.public_section.append(clear_func_decls)
    class_def.public_section.append(EmptyNode())
    source_blink_ns.body.append(clear_func_defs)
    source_blink_ns.body.append(EmptyNode())

    class_def.public_section.append(tov8value_func_decls)
    class_def.public_section.append(EmptyNode())
    source_blink_ns.body.append(tov8value_func_defs)
    source_blink_ns.body.append(EmptyNode())

    class_def.public_section.append(trace_func_decls)
    class_def.public_section.append(EmptyNode())
    source_blink_ns.body.append(trace_func_defs)
    source_blink_ns.body.append(EmptyNode())

    class_def.private_section.append(name_func_decls)
    class_def.private_section.append(EmptyNode())
    source_blink_ns.body.append(name_func_defs)
    source_blink_ns.body.append(EmptyNode())

    class_def.private_section.append(member_vars_def)
    class_def.private_section.append(EmptyNode())

    # Write down to the files.
    write_code_node_to_file(header_node, path_manager.gen_path_to(header_path))
    write_code_node_to_file(source_node, path_manager.gen_path_to(source_path))


def generate_unions(task_queue):
    assert isinstance(task_queue, TaskQueue)

    web_idl_database = package_initializer().web_idl_database()

    for union in web_idl_database.new_union_types:
        task_queue.post_task(generate_union, union.identifier)
