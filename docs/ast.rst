PragmaDirective

Pragma directive, only version requirements in the form `pragma solidity "^0.4.0";` are
supported for now.

ContractDefinition

Definition of a contract or library. This is the only AST nodes where child nodes are not visited in document order. It first visits all struct declarations, then all variable declarations and finally all function declarations.

CallableDeclaration

Base class for all nodes that define function-like objects, i.e. FunctionDefinition, EventDefinition and ModifierDefinition.

FunctionDefinition

EventDefinition

ModifierDefinition

StructDefinition

ParameterList

Parameter list, used as function parameter list and return list. None of the parameters is allowed to contain mappings (not even recursively inside structs.

VariableDeclarationStatement

Definition of one or more variables as a statement inside a function. If multiple variables are declared, a value has to be assigned directly. If only a single variable is declared, the value can be missing.

Examples:
uint[] memory a; uint a = 2;
(uint a, bytes32 b, ) = f(); (, uint a, , StructName storage x) = g();

VariableDeclaration

Declaration of a variable. This can be used in various places, e.g. in function parameter lists, struct definitions and even function bodies.

ArrayTypeName

An array type, can be "typename[]" or "typename[<expression>]".

Mapping

A mapping type. Its source form is "mapping('keyType' => 'valueType')"

UserDefinedTypeName

Name referring to a user-defined type (i.e. a struct, contract, etc.).

ElementaryTypeName

Any pre-defined type name represented by a single keyword (and possibly a state mutability for address types), i.e. it excludes mappings, contracts, functions, etc.

Block

Brace-enclosed block containing zero or more statements.

Return

Break

Continue

BinaryOperation

Operation involving a binary operator. Examples: 1 + 2, true && false or 1 <= 4

Identifier

An identifier, i.e. a reference to a declaration by name like a variable or function.

Literal

A literal string or number. @see ExpressionCompiler::endVisit() is used to actually parse its value.

ExpressionStatement

A statement that contains only an expression (i.e. an assignment, function call, ...).

FunctionCall

Can be ordinary function call, type cast or struct construction.

UnaryOperation

Operation involving a unary operator, pre- or postfix.
Examples: ++i, delete x or !true

MemberAccess

Access to a member of an object. Example: x.name

IndexAccess

Index access to an array or mapping. Example: a[2]

ForStatement

For loop statement

WhileStatement

IfStatement

If-statement with an optional "else" part. Note that "else if" is modeled by having a new if-statement as the false (else) body.