extends RefCounted


const SymbolTable := preload("../../symbol_table.gd")
const Token := preload("../tokenizer/token.gd")


class ASTNode:
	var parent : WeakRef
	var token : Token
	var children : Array[ASTNode]
	func _init(p : ASTNode) -> void: parent = weakref(p)
	func _to_string() -> String: return "pass" if parent else "root"
	func get_parent() -> ASTNode: return parent.get_ref() as ASTNode if parent else null
	func get_children() -> Array[ASTNode]: return children
	func print_tree(identation : int = 0, identation_str : String = ">   ") -> String:
		var str : String = "\n" + (identation_str.repeat(identation) if identation > 0 else "") + str(self)
		for child in get_children():
			str += child.print_tree(identation + 1)
		return str


class Sequence extends ASTNode:
	var token_from : Token
	var token_to : Token
	var statements : Array[ASTNode]
	func _to_string() -> String: return "sequence"
	func get_children() -> Array[ASTNode]: return statements


class If extends ASTNode:
	var condition : Expr
	var body : Sequence
	#var body_true : Sequence
	#var body_false : Sequence
	func _to_string() -> String: return "if"
	#func get_children() -> Array[ASTNode]: return [condition, body_true] if !body_false else [condition, body_true, body_false]
	func get_children() -> Array[ASTNode]: return body.statements


class Elif extends ASTNode:
	var condition : Expr
	var body : Sequence
	#var body_true : Sequence
	#var body_false : Sequence
	func _to_string() -> String: return "elif"
	func get_children() -> Array[ASTNode]: return body.statements


class Else extends ASTNode:
	var body : Sequence
	#var body_true : Sequence
	#var body_false : Sequence
	func _to_string() -> String: return "else"
	func get_children() -> Array[ASTNode]: return body.statements


class Match extends ASTNode:
	var body : Sequence
	func _to_string() -> String: return "match"
	func get_children() -> Array[ASTNode]: return body.statements


class For extends ASTNode:
	var iterator : Var
	var expr : Expr
	var body : Sequence
	func _to_string() -> String: return "for"
	func get_children() -> Array[ASTNode]: return ([iterator] as Array[ASTNode]) + body.statements


class Iterator extends Var:
	func _to_string() -> String: return "iterator " + str(symbol)


class While extends ASTNode:
	var condition : Expr
	var body : Sequence
	func _to_string() -> String: return "while"
	func get_children() -> Array[ASTNode]: return body.statements


class SymbolDeclaration extends ASTNode:
	var symbol : SymbolTable.Symbol
	func _to_string() -> String: return "declare symbol " + str(symbol)


class Class extends SymbolDeclaration:
	var ext : SymbolTable.SymbolPath
	var body : Sequence
	func _to_string() -> String: return "class " + str(symbol) + " extends " + str(ext)
	func get_children() -> Array[ASTNode]: return body.statements


class Func extends SymbolDeclaration:
	var params : Array[Parameter]
	var body : Sequence
	func _to_string() -> String: return "func " + str(symbol)
	func get_children() -> Array: return params + body.statements


class SignalDef extends SymbolDeclaration:
	var params : Array[Parameter]
	func _to_string() -> String: return "signal " + str(symbol)
	func get_children() -> Array: return params


class EnumDef extends SymbolDeclaration:
	var keys : Array[KeyDef]
	func _to_string() -> String: return "enum " + str(symbol)
	func get_children() -> Array: return keys

	class KeyDef extends SymbolDeclaration:
		var expr : Expr
		func _to_string() -> String: return "key " + str(symbol)
		#func get_children() -> Array[ASTNode]: return [expr]


class Var extends SymbolDeclaration:
	var default : String
	func _to_string() -> String: return "var " + str(symbol)


class Const extends Var:
	func _to_string() -> String: return "const " + str(symbol)


class ExportVar extends Var:
	func _to_string() -> String: return "@export var " + str(symbol)


class Parameter extends Var:
	func _to_string() -> String: return "param " + str(symbol)


class Symbol extends ASTNode:
	var path : SymbolTable.SymbolPath
	func _to_string() -> String: return "symbol " + str(path)


class Call extends Symbol:
	var args : Array[Expr]
	func _to_string() -> String: return "call " + str(path)
	func get_children() -> Array: return args


class Expr extends ASTNode:
	var expr : String
	var symbol_paths : Array[SymbolTable.SymbolPath]
	func _to_string() -> String: return expr + " " + str(symbol_paths)
	#func get_children() -> Array[ASTNode]: return components


class Assignment extends ASTNode:
	var target : SymbolTable.SymbolPath
	var op : String
	var expr : Expr
	func _to_string() -> String: return "assign " + str(target) + " " + op
	func get_children() -> Array[ASTNode]: return [expr]
