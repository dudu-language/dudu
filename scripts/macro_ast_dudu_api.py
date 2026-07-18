"""Hand-designed Dudu helpers appended to the generated public macro AST module."""


DUDU_CLASS_METHODS = {
    "Expansion": r'''
    def add_method(self, value: FunctionDecl):
        declaration = Declaration(kind=DeclarationKind.Function, function_decl=value)
        self.members.append(GeneratedDeclaration(declaration=declaration))

    def add_field(self, value: FieldDecl):
        declaration = Declaration(kind=DeclarationKind.Field, field_decl=value)
        self.members.append(GeneratedDeclaration(declaration=declaration))

    def add_constant(self, value: ConstantDecl):
        declaration = Declaration(kind=DeclarationKind.Constant, constant_decl=value)
        self.members.append(GeneratedDeclaration(declaration=declaration))

    def add_sibling(self, value: Declaration):
        self.siblings.append(GeneratedDeclaration(declaration=value))

    def add_implementation(self, value: ImplementationDecl):
        declaration = Declaration(
            kind=DeclarationKind.Implementation,
            implementation_decl=value,
        )
        self.implementations.append(GeneratedDeclaration(declaration=declaration))

    def add_diagnostic(self, value: Diagnostic):
        self.diagnostics.append(value)

    def require_module(self, module_path: str, alias: str):
        self.imports.append(GeneratedImport(module_path=module_path, alias=alias))
'''.strip("\n"),
}


DUDU_API = r'''
def named_type(name: str) -> TypeRef:
    return TypeRef(kind=TypeKind.Named, name=name)


def qualified_type(name: str) -> TypeRef:
    return TypeRef(kind=TypeKind.Qualified, name=name)


def value_type(value: str) -> TypeRef:
    return TypeRef(kind=TypeKind.Value, value=value)


def generic_type(name: str, arguments: list[TypeRef]) -> TypeRef:
    return TypeRef(kind=TypeKind.Template, name=name, children=arguments)


def associated_type(owner: TypeRef, name: str) -> TypeRef:
    return TypeRef(kind=TypeKind.Associated, name=name, children=[owner])


def pointer_type(value: TypeRef) -> TypeRef:
    return TypeRef(kind=TypeKind.Pointer, children=[value])


def reference_type(value: TypeRef) -> TypeRef:
    return TypeRef(kind=TypeKind.Reference, children=[value])


def const_type(value: TypeRef) -> TypeRef:
    return TypeRef(kind=TypeKind.Const, children=[value])


def volatile_type(value: TypeRef) -> TypeRef:
    return TypeRef(kind=TypeKind.Volatile, children=[value])


def atomic_type(value: TypeRef) -> TypeRef:
    return TypeRef(kind=TypeKind.Atomic, children=[value])


def device_type(value: TypeRef) -> TypeRef:
    return TypeRef(kind=TypeKind.Device, children=[value])


def storage_type(value: TypeRef) -> TypeRef:
    return TypeRef(kind=TypeKind.Storage, children=[value])


def shared_type(value: TypeRef) -> TypeRef:
    return TypeRef(kind=TypeKind.Shared, children=[value])


def static_type(value: TypeRef) -> TypeRef:
    return TypeRef(kind=TypeKind.Static, children=[value])


def fixed_array_type(value: TypeRef, extents: list[TypeRef]) -> TypeRef:
    children = [value]
    for extent in extents:
        children.append(extent)
    return TypeRef(kind=TypeKind.FixedArray, children=children)


def function_type(parameters: list[TypeRef], result: TypeRef) -> TypeRef:
    children = parameters
    children.append(result)
    return TypeRef(kind=TypeKind.Function, children=children)


def pack_type(value: TypeRef) -> TypeRef:
    return TypeRef(kind=TypeKind.PackExpansion, children=[value])


def shaped_type(value: TypeRef, extents: list[TypeRef]) -> TypeRef:
    children = [value]
    for extent in extents:
        children.append(extent)
    return TypeRef(kind=TypeKind.Shaped, children=children)


def name_expression(name: str) -> Expression:
    return Expression(kind=ExpressionKind.Name, name=name)


def bool_expression(value: bool) -> Expression:
    if value:
        return Expression(kind=ExpressionKind.BoolLiteral, value="True")
    return Expression(kind=ExpressionKind.BoolLiteral, value="False")


def int_expression(value: str) -> Expression:
    return Expression(kind=ExpressionKind.IntLiteral, value=value)


def float_expression(value: str) -> Expression:
    return Expression(kind=ExpressionKind.FloatLiteral, value=value)


def string_expression(value: str) -> Expression:
    return Expression(kind=ExpressionKind.StringLiteral, value=value)


def none_expression() -> Expression:
    return Expression(kind=ExpressionKind.NoneLiteral)


def unary_expression(operator_name: str, value: Expression) -> Expression:
    return Expression(kind=ExpressionKind.Unary, operator_name=operator_name, children=[value])


def binary_expression(left: Expression, operator_name: str, right: Expression) -> Expression:
    return Expression(
        kind=ExpressionKind.Binary,
        operator_name=operator_name,
        children=[left, right],
    )


def member_expression(value: Expression, name: str) -> Expression:
    return Expression(kind=ExpressionKind.Member, name=name, children=[value])


def call_expression(callee: Expression, arguments: list[Expression]) -> Expression:
    return Expression(kind=ExpressionKind.Call, children=arguments, callee=[callee])


def template_call_expression(
    callee: Expression,
    type_arguments: list[TypeRef],
    arguments: list[Expression],
) -> Expression:
    return Expression(
        kind=ExpressionKind.TemplateCall,
        children=arguments,
        type_arguments=type_arguments,
        callee=[callee],
    )


def index_expression(value: Expression, indices: list[Expression]) -> Expression:
    children = [value]
    for index in indices:
        children.append(index)
    return Expression(kind=ExpressionKind.Index, children=children)


def list_expression(values: list[Expression]) -> Expression:
    return Expression(kind=ExpressionKind.ListLiteral, children=values)


def tuple_expression(values: list[Expression]) -> Expression:
    return Expression(kind=ExpressionKind.TupleLiteral, children=values)


def set_expression(values: list[Expression]) -> Expression:
    return Expression(kind=ExpressionKind.SetLiteral, children=values)


def dict_entry_expression(key: Expression, value: Expression) -> Expression:
    return Expression(kind=ExpressionKind.DictEntry, children=[key, value])


def dict_expression(entries: list[Expression]) -> Expression:
    return Expression(kind=ExpressionKind.DictLiteral, children=entries)


def named_argument_expression(name: str, value: Expression) -> Expression:
    return Expression(kind=ExpressionKind.NamedArg, name=name, children=[value])


def slice_expression(parts: list[Expression]) -> Expression:
    return Expression(kind=ExpressionKind.Slice, children=parts)


def ellipsis_expression() -> Expression:
    return Expression(kind=ExpressionKind.Ellipsis)


def new_axis_expression() -> Expression:
    return Expression(kind=ExpressionKind.NewAxis)


def pack_expression(value: Expression) -> Expression:
    return Expression(kind=ExpressionKind.PackExpansion, children=[value])


def expression_statement(value: Expression) -> Statement:
    return Statement(kind=StatementKind.Expression, expression=value)


def variable_statement(name: str, type: TypeRef, value: Expression) -> Statement:
    return Statement(kind=StatementKind.Variable, name=name, type=type, value=value)


def assignment_statement(target: Expression, value: Expression) -> Statement:
    return Statement(kind=StatementKind.Assign, value=value, target=target)


def compound_assignment_statement(
    target: Expression,
    operator_name: str,
    value: Expression,
) -> Statement:
    return Statement(
        kind=StatementKind.CompoundAssign,
        value=value,
        target=target,
        operator_name=operator_name,
    )


def return_statement(value: Expression) -> Statement:
    return Statement(kind=StatementKind.Return, value=value)


def bare_return_statement() -> Statement:
    return Statement(kind=StatementKind.Return)


def if_statement(condition: Expression, body: list[Statement]) -> Statement:
    return Statement(kind=StatementKind.If, condition=condition, children=body)


def elif_statement(condition: Expression, body: list[Statement]) -> Statement:
    return Statement(kind=StatementKind.Elif, condition=condition, children=body)


def else_statement(body: list[Statement]) -> Statement:
    return Statement(kind=StatementKind.Else, children=body)


def match_statement(value: Expression, cases: list[Statement]) -> Statement:
    return Statement(kind=StatementKind.Match, expression=value, children=cases)


def case_statement(pattern: Expression, guard: Expression, body: list[Statement]) -> Statement:
    return Statement(kind=StatementKind.Case, children=body, pattern=pattern, guard=guard)


def unguarded_case_statement(pattern: Expression, body: list[Statement]) -> Statement:
    return Statement(kind=StatementKind.Case, children=body, pattern=pattern)


def while_statement(condition: Expression, body: list[Statement]) -> Statement:
    return Statement(kind=StatementKind.While, condition=condition, children=body)


def for_statement(name: str, iterable: Expression, body: list[Statement]) -> Statement:
    return Statement(kind=StatementKind.For, name=name, children=body, iterable=iterable)


def break_statement() -> Statement:
    return Statement(kind=StatementKind.Break)


def continue_statement() -> Statement:
    return Statement(kind=StatementKind.Continue)


def try_statement(body: list[Statement]) -> Statement:
    return Statement(kind=StatementKind.Try, children=body)


def except_statement(pattern: Expression, body: list[Statement]) -> Statement:
    return Statement(kind=StatementKind.Except, children=body, pattern=pattern)


def raise_statement(value: Expression) -> Statement:
    return Statement(kind=StatementKind.Raise, value=value)


def delete_statement(value: Expression) -> Statement:
    return Statement(kind=StatementKind.Delete, value=value)


def assert_statement(condition: Expression, message: Expression) -> Statement:
    return Statement(kind=StatementKind.Assert, condition=condition, message=message)


def debug_assert_statement(condition: Expression, message: Expression) -> Statement:
    return Statement(kind=StatementKind.DebugAssert, condition=condition, message=message)


def pass_statement() -> Statement:
    return Statement(kind=StatementKind.Pass)


def attribute_argument(name: str, value: Expression) -> AttributeArgument:
    return AttributeArgument(name=name, value=value)


def attribute(name: str, arguments: list[AttributeArgument]) -> Attribute:
    return Attribute(name=name, arguments=arguments)


def generic_parameter(name: str) -> GenericParameter:
    return GenericParameter(name=name)


def value_parameter(name: str, type: TypeRef) -> GenericParameter:
    return GenericParameter(name=name, type=type)


def parameter(name: str, type: TypeRef) -> Parameter:
    return Parameter(name=name, type=type)


def field(name: str, type: TypeRef) -> FieldDecl:
    return FieldDecl(name=name, type=type)


def function(
    name: str,
    parameters: list[Parameter],
    return_type: TypeRef,
    body: list[Statement],
) -> FunctionDecl:
    return FunctionDecl(
        name=name,
        parameters=parameters,
        return_type=return_type,
        body=body,
    )


def procedure(name: str, parameters: list[Parameter], body: list[Statement]) -> FunctionDecl:
    return FunctionDecl(name=name, parameters=parameters, body=body)


def enum_variant(name: str, fields: list[FieldDecl]) -> EnumVariant:
    return EnumVariant(name=name, fields=fields)


def class_declaration(value: ClassDecl) -> Declaration:
    return Declaration(kind=DeclarationKind.Class, class_decl=value)


def enum_declaration(value: EnumDecl) -> Declaration:
    return Declaration(kind=DeclarationKind.Enum, enum_decl=value)


def function_declaration(value: FunctionDecl) -> Declaration:
    return Declaration(kind=DeclarationKind.Function, function_decl=value)


def field_declaration(value: FieldDecl) -> Declaration:
    return Declaration(kind=DeclarationKind.Field, field_decl=value)


def constant_declaration(value: ConstantDecl) -> Declaration:
    return Declaration(kind=DeclarationKind.Constant, constant_decl=value)


def implementation_declaration(value: ImplementationDecl) -> Declaration:
    return Declaration(kind=DeclarationKind.Implementation, implementation_decl=value)


def generated(declaration: Declaration) -> GeneratedDeclaration:
    return GeneratedDeclaration(declaration=declaration)


def generated_at(declaration: Declaration, origin: SourceOrigin) -> GeneratedDeclaration:
    return GeneratedDeclaration(declaration=declaration, origin=origin)


def invocation_origin(location: SourceRange, macro_name: str) -> SourceOrigin:
    return SourceOrigin(
        kind=OriginKind.MacroInvocation,
        range=location,
        macro_name=macro_name,
    )


def generated_origin(location: SourceRange, macro_name: str) -> SourceOrigin:
    return SourceOrigin(kind=OriginKind.Generated, range=location, macro_name=macro_name)


def expansion() -> Expansion:
    return Expansion()


def diagnostic(
    severity: DiagnosticSeverity,
    code: str,
    location: SourceRange,
    message: str,
) -> Diagnostic:
    return Diagnostic(severity=severity, code=code, message=message, range=location)


def note(location: SourceRange, message: str) -> Diagnostic:
    return diagnostic(DiagnosticSeverity.Note, "dudu.macro", location, message)


def warning(location: SourceRange, message: str) -> Diagnostic:
    return diagnostic(DiagnosticSeverity.Warning, "dudu.macro", location, message)


def error(location: SourceRange, message: str) -> Diagnostic:
    return diagnostic(DiagnosticSeverity.Error, "dudu.macro", location, message)


def find_attribute(attributes: &const[list[Attribute]], name: str) -> Option[Attribute]:
    for attribute in attributes:
        if attribute.name == name:
            return attribute
    return None


def find_argument(attribute: &const[Attribute], name: str) -> Option[Expression]:
    for argument in attribute.arguments:
        if argument.name == name:
            return argument.value
    return None


def string_argument(attribute: &const[Attribute], name: str, fallback: str) -> str:
    value = find_argument(attribute, name)
    if not value.has_value():
        return fallback
    return value.value().value


def bool_argument(attribute: &const[Attribute], name: str, fallback: bool) -> bool:
    value = find_argument(attribute, name)
    if not value.has_value():
        return fallback
    return value.value().value == "True"


def int_argument(attribute: &const[Attribute], name: str, fallback: i64) -> i64:
    value = find_argument(attribute, name)
    if not value.has_value():
        return fallback
    return cpp("std::stoll(value.value().value)")


def float_argument(attribute: &const[Attribute], name: str, fallback: f64) -> f64:
    value = find_argument(attribute, name)
    if not value.has_value():
        return fallback
    return cpp("std::stod(value.value().value)")


def expression_argument(
    attribute: &const[Attribute],
    name: str,
    fallback: Expression,
) -> Expression:
    value = find_argument(attribute, name)
    if not value.has_value():
        return fallback
    return value.value()
'''
