/*
	This file is part of solidity.

	solidity is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	solidity is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with solidity.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <test/tools/ossfuzz/protoToYul.h>
#include <test/tools/ossfuzz/yulOptimizerFuzzDictionary.h>

#include <libyul/Exceptions.h>

#include <libdevcore/StringUtils.h>

#include <boost/range/algorithm_ext/erase.hpp>
#include <boost/algorithm/cxx11/all_of.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>

using namespace std;
using namespace yul::test::yul_fuzzer;
using namespace dev;

string ProtoConverter::dictionaryToken(HexPrefix _p)
{
	unsigned indexVar = m_inputSize * m_inputSize + counter();
	std::string token = hexDictionary[indexVar % hexDictionary.size()];
	yulAssert(token.size() <= 64, "Proto Fuzzer: Dictionary token too large");

	return _p == HexPrefix::Add ? "0x" + token : token;
}

string ProtoConverter::createHex(string const& _hexBytes)
{
	string tmp{_hexBytes};
	if (!tmp.empty())
	{
		boost::range::remove_erase_if(tmp, [=](char c) -> bool {
			return !std::isxdigit(c);
		});
		tmp = tmp.substr(0, 64);
	}
	// We need this awkward if case because hex literals cannot be empty.
	// Use a dictionary token.
	if (tmp.empty())
		tmp = dictionaryToken(HexPrefix::DontAdd);
	yulAssert(tmp.size() <= 64, "Proto Fuzzer: Dictionary token too large");
	return tmp;
}

string ProtoConverter::createAlphaNum(string const& _strBytes)
{
	string tmp{_strBytes};
	if (!tmp.empty())
	{
		boost::range::remove_erase_if(tmp, [=](char c) -> bool {
			return !(std::isalpha(c) || std::isdigit(c));
		});
		tmp = tmp.substr(0, 32);
	}
	return tmp;
}

string ProtoConverter::visit(Literal const& _x)
{
	switch (_x.literal_oneof_case())
	{
	case Literal::kIntval:
		return to_string(_x.intval());
	case Literal::kHexval:
		return "0x" + createHex(_x.hexval());
	case Literal::kStrval:
		return "\"" + createAlphaNum(_x.strval()) + "\"";
	case Literal::LITERAL_ONEOF_NOT_SET:
		return dictionaryToken();
	}
}

void ProtoConverter::visit(VarRef const& _x)
{
	yulAssert(m_variables.size() > 0, "Proto fuzzer: No variables to reference.");
	m_output << m_variables[_x.varnum() % m_variables.size()];
}

void ProtoConverter::visit(Expression const& _x)
{
	switch (_x.expr_oneof_case())
	{
	case Expression::kVarref:
		visit(_x.varref());
		break;
	case Expression::kCons:
		m_output << visit(_x.cons());
		break;
	case Expression::kBinop:
		visit(_x.binop());
		break;
	case Expression::kUnop:
		visit(_x.unop());
		break;
	case Expression::kTop:
		visit(_x.top());
		break;
	case Expression::kNop:
		visit(_x.nop());
		break;
	case Expression::kFuncExpr:
		visit(_x.func_expr());
		break;
	case Expression::EXPR_ONEOF_NOT_SET:
		m_output << dictionaryToken();
		break;
	}
}

void ProtoConverter::visit(BinaryOp const& _x)
{
	switch (_x.op())
	{
	case BinaryOp::ADD:
		m_output << "add";
		break;
	case BinaryOp::SUB:
		m_output << "sub";
		break;
	case BinaryOp::MUL:
		m_output << "mul";
		break;
	case BinaryOp::DIV:
		m_output << "div";
		break;
	case BinaryOp::MOD:
		m_output << "mod";
		break;
	case BinaryOp::XOR:
		m_output << "xor";
		break;
	case BinaryOp::AND:
		m_output << "and";
		break;
	case BinaryOp::OR:
		m_output << "or";
		break;
	case BinaryOp::EQ:
		m_output << "eq";
		break;
	case BinaryOp::LT:
		m_output << "lt";
		break;
	case BinaryOp::GT:
		m_output << "gt";
		break;
	case BinaryOp::SHR:
		m_output << "shr";
		break;
	case BinaryOp::SHL:
		m_output << "shl";
		break;
	case BinaryOp::SAR:
		m_output << "sar";
		break;
	case BinaryOp::SDIV:
		m_output << "sdiv";
		break;
	case BinaryOp::SMOD:
		m_output << "smod";
		break;
	case BinaryOp::EXP:
		m_output << "exp";
		break;
	case BinaryOp::SLT:
		m_output << "slt";
		break;
	case BinaryOp::SGT:
		m_output << "sgt";
		break;
	case BinaryOp::BYTE:
		m_output << "byte";
		break;
	case BinaryOp::SI:
		m_output << "signextend";
		break;
	case BinaryOp::KECCAK:
		m_output << "keccak256";
		break;
	}
	m_output << "(";
	visit(_x.left());
	m_output << ",";
	visit(_x.right());
	m_output << ")";
}

void ProtoConverter::visit(VarDecl const& _x)
{
	string varName = "x_" + to_string(counter());
	m_output << "let " << varName << " := ";
	visit(_x.expr());
	m_output << "\n";
	m_scopes.top().insert(varName);
	m_variables.push_back(varName);
}

void ProtoConverter::visit(EmptyVarDecl const&)
{
	string varName = "x_" + to_string(counter());
	m_output << "let " << varName << "\n";
	m_scopes.top().insert(varName);
	m_variables.push_back(varName);
}

void ProtoConverter::visit(MultiVarDecl const& _x)
{
	size_t funcId = (static_cast<size_t>(_x.func_index()) % m_functionVecMultiReturnValue.size());

	unsigned numInParams = m_functionVecMultiReturnValue.at(funcId).first;
	unsigned numOutParams = m_functionVecMultiReturnValue.at(funcId).second;

	// Ensure that the chosen function returns at least 2 and at most 4 values
	yulAssert(
		((numOutParams >= 2) && (numOutParams <= 4)),
		"Proto fuzzer: Multi variable declaration calls a function with either too few or too many output params."
	);

	// Obtain variable name suffix
	unsigned startIdx = counter();
	m_output << "let ";
	vector<string> varsVec = createVars(startIdx, startIdx + numOutParams);
	m_output << " := ";

	// Create RHS of multi var decl
	m_output << "foo_" << functionTypeToString(NumFunctionReturns::Multiple) << "_" << funcId;
	m_output << "(";
	visitFunctionInputParams(_x, numInParams);
	m_output << ")\n";
	// Add newly minted vars in the multidecl statement to current scope
	addToScope(move(varsVec));
}

void ProtoConverter::visit(TypedVarDecl const& _x)
{
	string varName = "x_" + to_string(counter());
	m_output << "let " << varName;
	switch (_x.type())
	{
	case TypedVarDecl::BOOL:
		m_output << ": bool := ";
		visit(_x.expr());
		m_output << " : bool\n";
		break;
	case TypedVarDecl::S8:
		m_output << ": s8 := ";
		visit(_x.expr());
		m_output << " : s8\n";
		break;
	case TypedVarDecl::S32:
		m_output << ": s32 := ";
		visit(_x.expr());
		m_output << " : s32\n";
		break;
	case TypedVarDecl::S64:
		m_output << ": s64 := ";
		visit(_x.expr());
		m_output << " : s64\n";
		break;
	case TypedVarDecl::S128:
		m_output << ": s128 := ";
		visit(_x.expr());
		m_output << " : s128\n";
		break;
	case TypedVarDecl::S256:
		m_output << ": s256 := ";
		visit(_x.expr());
		m_output << " : s256\n";
		break;
	case TypedVarDecl::U8:
		m_output << ": u8 := ";
		visit(_x.expr());
		m_output << " : u8\n";
		break;
	case TypedVarDecl::U32:
		m_output << ": u32 := ";
		visit(_x.expr());
		m_output << " : u32\n";
		break;
	case TypedVarDecl::U64:
		m_output << ": u64 := ";
		visit(_x.expr());
		m_output << " : u64\n";
		break;
	case TypedVarDecl::U128:
		m_output << ": u128 := ";
		visit(_x.expr());
		m_output << " : u128\n";
		break;
	case TypedVarDecl::U256:
		m_output << ": u256 := ";
		visit(_x.expr());
		m_output << " : u256\n";
		break;
	}
	m_scopes.top().insert(varName);
	m_variables.push_back(varName);
}

void ProtoConverter::visit(UnaryOp const& _x)
{
	switch (_x.op())
	{
	case UnaryOp::NOT:
		m_output << "not";
		break;
	case UnaryOp::MLOAD:
		m_output << "mload";
		break;
	case UnaryOp::SLOAD:
		m_output << "sload";
		break;
	case UnaryOp::ISZERO:
		m_output << "iszero";
		break;
	case UnaryOp::CALLDATALOAD:
		m_output << "calldataload";
		break;
	case UnaryOp::EXTCODESIZE:
		m_output << "extcodesize";
		break;
	case UnaryOp::EXTCODEHASH:
		m_output << "extcodehash";
		break;
	}
	m_output << "(";
	visit(_x.operand());
	m_output << ")";
}

void ProtoConverter::visit(TernaryOp const& _x)
{
	switch (_x.op())
	{
	case TernaryOp::ADDM:
		m_output << "addmod";
		break;
	case TernaryOp::MULM:
		m_output << "mulmod";
		break;
	}
	m_output << "(";
	visit(_x.arg1());
	m_output << ", ";
	visit(_x.arg2());
	m_output << ", ";
	visit(_x.arg3());
	m_output << ")";
}

void ProtoConverter::visit(NullaryOp const& _x)
{
	switch (_x.op())
	{
	case NullaryOp::PC:
		m_output << "pc()";
		break;
	case NullaryOp::MSIZE:
		m_output << "msize()";
		break;
	case NullaryOp::GAS:
		m_output << "gas()";
		break;
	case NullaryOp::CALLDATASIZE:
		m_output << "calldatasize()";
		break;
	case NullaryOp::CODESIZE:
		m_output << "codesize()";
		break;
	case NullaryOp::RETURNDATASIZE:
		m_output << "returndatasize()";
		break;
	}
}

void ProtoConverter::visit(CopyFunc const& _x)
{
	switch (_x.ct())
	{
	case CopyFunc::CALLDATA:
		m_output << "calldatacopy";
		break;
	case CopyFunc::CODE:
		m_output << "codecopy";
		break;
	case CopyFunc::RETURNDATA:
		m_output << "returndatacopy";
		break;
	}
	m_output << "(";
	visit(_x.target());
	m_output << ", ";
	visit(_x.source());
	m_output << ", ";
	visit(_x.size());
	m_output << ")\n";
}

void ProtoConverter::visit(ExtCodeCopy const& _x)
{
	m_output << "extcodecopy";
	m_output << "(";
	visit(_x.addr());
	m_output << ", ";
	visit(_x.target());
	m_output << ", ";
	visit(_x.source());
	m_output << ", ";
	visit(_x.size());
	m_output << ")\n";
}

void ProtoConverter::visit(LogFunc const& _x)
{
	switch (_x.num_topics())
	{
	case LogFunc::ZERO:
		m_output << "log0";
		m_output << "(";
		visit(_x.pos());
		m_output << ", ";
		visit(_x.size());
		m_output << ")\n";
		break;
	case LogFunc::ONE:
		m_output << "log1";
		m_output << "(";
		visit(_x.pos());
		m_output << ", ";
		visit(_x.size());
		m_output << ", ";
		visit(_x.t1());
		m_output << ")\n";
		break;
	case LogFunc::TWO:
		m_output << "log2";
		m_output << "(";
		visit(_x.pos());
		m_output << ", ";
		visit(_x.size());
		m_output << ", ";
		visit(_x.t1());
		m_output << ", ";
		visit(_x.t2());
		m_output << ")\n";
		break;
	case LogFunc::THREE:
		m_output << "log3";
		m_output << "(";
		visit(_x.pos());
		m_output << ", ";
		visit(_x.size());
		m_output << ", ";
		visit(_x.t1());
		m_output << ", ";
		visit(_x.t2());
		m_output << ", ";
		visit(_x.t3());
		m_output << ")\n";
		break;
	case LogFunc::FOUR:
		m_output << "log4";
		m_output << "(";
		visit(_x.pos());
		m_output << ", ";
		visit(_x.size());
		m_output << ", ";
		visit(_x.t1());
		m_output << ", ";
		visit(_x.t2());
		m_output << ", ";
		visit(_x.t3());
		m_output << ", ";
		visit(_x.t4());
		m_output << ")\n";
		break;
	}
}

void ProtoConverter::visit(AssignmentStatement const& _x)
{
	visit(_x.ref_id());
	m_output << " := ";
	visit(_x.expr());
	m_output << "\n";
}

// Called at the time function call is being made
template <class T>
void ProtoConverter::visitFunctionInputParams(T const& _x, unsigned _numInputParams)
{
	// We reverse the order of function input visits since it helps keep this switch case concise.
	switch (_numInputParams)
	{
	case 4:
		visit(_x.in_param4());
		m_output << ", ";
		BOOST_FALLTHROUGH;
	case 3:
		visit(_x.in_param3());
		m_output << ", ";
		BOOST_FALLTHROUGH;
	case 2:
		visit(_x.in_param2());
		m_output << ", ";
		BOOST_FALLTHROUGH;
	case 1:
		visit(_x.in_param1());
		BOOST_FALLTHROUGH;
	case 0:
		break;
	default:
		yulAssert(false, "Proto fuzzer: Function call with too many input parameters.");
		break;
	}
}

void ProtoConverter::visit(MultiAssignment const& _x)
{
	size_t funcId = (static_cast<size_t>(_x.func_index()) % m_functionVecMultiReturnValue.size());
	unsigned numInParams = m_functionVecMultiReturnValue.at(funcId).first;
	unsigned numOutParams = m_functionVecMultiReturnValue.at(funcId).second;
	yulAssert(
		((numOutParams >= 2) && (numOutParams <= 4)),
		"Proto fuzzer: Multi assignment calls a function that has either too many or too few output parameters."
	);

	// Convert LHS of multi assignment
	// We reverse the order of out param visits since the order does not matter. This helps reduce the size of this
	// switch statement.
	switch (numOutParams)
	{
	case 4:
		visit(_x.out_param4());
		m_output << ", ";
		BOOST_FALLTHROUGH;
	case 3:
		visit(_x.out_param3());
		m_output << ", ";
		BOOST_FALLTHROUGH;
	case 2:
		visit(_x.out_param2());
		m_output << ", ";
		visit(_x.out_param1());
		break;
	default:
		yulAssert(false, "Proto fuzzer: Function call with too many input parameters.");
		break;
	}
	m_output << " := ";

	// Convert RHS of multi assignment
	m_output << "foo_" << functionTypeToString(NumFunctionReturns::Multiple) << "_" << funcId;
	m_output << "(";
	visitFunctionInputParams(_x, numInParams);
	m_output << ")\n";
}

void ProtoConverter::visit(FunctionCallNoReturnVal const& _x)
{
	size_t funcId = (static_cast<size_t>(_x.func_index()) % m_functionVecNoReturnValue.size());
	unsigned numInParams = m_functionVecNoReturnValue.at(funcId);
	m_output << "foo_" << functionTypeToString(NumFunctionReturns::None) << "_" << funcId;
	m_output << "(";
	visitFunctionInputParams(_x, numInParams);
	m_output << ")\n";
}

void ProtoConverter::visit(FunctionCallSingleReturnVal const& _x)
{
	size_t funcId = (static_cast<size_t>(_x.func_index()) % m_functionVecSingleReturnValue.size());
	unsigned numInParams = m_functionVecSingleReturnValue.at(funcId);
	m_output << "foo_" << functionTypeToString(NumFunctionReturns::Single) << "_" << funcId;
	m_output << "(";
	visitFunctionInputParams(_x, numInParams);
	m_output << ")";
}

void ProtoConverter::visit(FunctionCall const& _x)
{
	switch (_x.functioncall_oneof_case())
	{
	case FunctionCall::kCallZero:
		visit(_x.call_zero());
		break;
	case FunctionCall::kCallMultidecl:
		// Hack: Disallow (multi) variable declarations until scope extension is implemented for "for-init"
		if (!m_inForInitScope)
			visit(_x.call_multidecl());
		break;
	case FunctionCall::kCallMultiassign:
		visit(_x.call_multiassign());
		break;
	case FunctionCall::FUNCTIONCALL_ONEOF_NOT_SET:
		break;
	}
}

void ProtoConverter::visit(IfStmt const& _x)
{
	m_output << "if ";
	visit(_x.cond());
	m_output << " ";
	visit(_x.if_body());
}

void ProtoConverter::visit(StoreFunc const& _x)
{
	switch (_x.st())
	{
	case StoreFunc::MSTORE:
		m_output << "mstore(";
		break;
	case StoreFunc::SSTORE:
		m_output << "sstore(";
		break;
	case StoreFunc::MSTORE8:
		m_output << "mstore8(";
		break;
	}
	visit(_x.loc());
	m_output << ", ";
	visit(_x.val());
	m_output << ")\n";
}

void ProtoConverter::visit(ForStmt const& _x)
{
	bool wasInForBody = m_inForBodyScope;
	bool wasInForInit = m_inForInitScope;
	m_inForBodyScope = false;
	m_inForInitScope = true;
	m_output << "for ";
	visit(_x.for_init());
	m_inForInitScope = false;
	visit(_x.for_cond());
	visit(_x.for_post());
	m_inForBodyScope = true;
	visit(_x.for_body());
	m_inForBodyScope = wasInForBody;
	m_inForInitScope = wasInForInit;
}

void ProtoConverter::visit(BoundedForStmt const& _x)
{
	// Boilerplate for loop that limits the number of iterations to a maximum of 4.
	std::string loopVarName("i_" + std::to_string(m_numNestedForLoops++));
	m_output << "for { let " << loopVarName << " := 0 } "
	       << "lt(" << loopVarName << ", 0x60) "
	       << "{ " << loopVarName << " := add(" << loopVarName << ", 0x20) } ";
	// Store previous for body scope
	bool wasInForBody = m_inForBodyScope;
	bool wasInForInit = m_inForInitScope;
	m_inForBodyScope = true;
	m_inForInitScope = false;
	visit(_x.for_body());
	// Restore previous for body scope and init
	m_inForBodyScope = wasInForBody;
	m_inForInitScope = wasInForInit;
}

void ProtoConverter::visit(CaseStmt const& _x)
{
	string literal = visit(_x.case_lit());
	// u256 value of literal
	u256 literalVal;

	// Convert string to u256 before looking for duplicate case literals
	if (_x.case_lit().has_strval())
	{
		// Since string literals returned by the Literal visitor are enclosed within
		// double quotes (like this "\"<string>\""), their size is at least two in the worst case
		// that <string> is empty. Here we assert this invariant.
		yulAssert(literal.size() >= 2, "Proto fuzzer: String literal too short");
		// This variable stores the <string> part i.e., literal minus the first and last
		// double quote characters. This is used to compute the keccak256 hash of the
		// string literal. The hashing is done to check whether we are about to create
		// a case statement containing a case literal that has already been used in a
		// previous case statement. If the hash (u256 value) matches a previous hash,
		// then we simply don't create a new case statement.
		string noDoubleQuoteStr = "";
		if (literal.size() > 2)
		{
			// Ensure that all characters in the string literal except the first
			// and the last (double quote characters) are alphanumeric.
			yulAssert(
				boost::algorithm::all_of(literal.begin() + 1, literal.end() - 2, [=](char c) -> bool {
					return std::isalpha(c) || std::isdigit(c);
				}),
				"Proto fuzzer: Invalid string literal encountered"
			);

			// Make a copy because literal will need to be used later
			noDoubleQuoteStr = literal.substr(1, literal.size() - 2);
		}
		// Hash the result to check for duplicate case literal strings
		literalVal = u256(h256(noDoubleQuoteStr, h256::FromBinary, h256::AlignLeft));

		// Make sure that an empty string literal evaluates to zero. This is to detect creation of
		// duplicate case literals like so
		// switch (x)
		// {
		//    case "": { x := 0 }
		//    case 0: { x:= 1 } // Case statement with duplicate literal is invalid
		// } // This snippet will not be parsed successfully.
		if (noDoubleQuoteStr.empty())
			yulAssert(literalVal == 0, "Proto fuzzer: Empty string does not evaluate to zero");
	}
	else
		literalVal = u256(literal);

	// Check if set insertion fails (case literal present) or succeeds (case literal
	// absent).
	bool isUnique = m_switchLiteralSetPerScope.top().insert(literalVal).second;

	// It is fine to bail out if we encounter a duplicate case literal because
	// we can be assured that the switch statement is well-formed i.e., contains
	// at least one case statement or a default block.
	if (isUnique)
	{
		m_output << "case " << literal << " ";
		visit(_x.case_block());
	}
}

void ProtoConverter::visit(SwitchStmt const& _x)
{
	if (_x.case_stmt_size() > 0 || _x.has_default_block())
	{
		std::set<dev::u256> s;
		m_switchLiteralSetPerScope.push(s);
		m_output << "switch ";
		visit(_x.switch_expr());
		m_output << "\n";

		for (auto const& caseStmt: _x.case_stmt())
			visit(caseStmt);

		m_switchLiteralSetPerScope.pop();

		if (_x.has_default_block())
		{
			m_output << "default ";
			visit(_x.default_block());
		}
	}
}

void ProtoConverter::visit(StopInvalidStmt const& _x)
{
	switch (_x.stmt())
	{
	case StopInvalidStmt::STOP:
		m_output << "stop()\n";
		break;
	case StopInvalidStmt::INVALID:
		m_output << "invalid()\n";
		break;
	}
}

void ProtoConverter::visit(RetRevStmt const& _x)
{
	switch (_x.stmt())
	{
	case RetRevStmt::RETURN:
		m_output << "return";
		break;
	case RetRevStmt::REVERT:
		m_output << "revert";
		break;
	}
	m_output << "(";
	visit(_x.pos());
	m_output << ", ";
	visit(_x.size());
	m_output << ")\n";
}

void ProtoConverter::visit(SelfDestructStmt const& _x)
{
	m_output << "selfdestruct";
	m_output << "(";
	visit(_x.addr());
	m_output << ")\n";
}

void ProtoConverter::visit(TerminatingStmt const& _x)
{
	switch (_x.term_oneof_case())
	{
	case TerminatingStmt::kStopInvalid:
		visit(_x.stop_invalid());
		break;
	case TerminatingStmt::kRetRev:
		visit(_x.ret_rev());
		break;
	case TerminatingStmt::kSelfDes:
		visit(_x.self_des());
		break;
	case TerminatingStmt::TERM_ONEOF_NOT_SET:
		break;
	}
}

void ProtoConverter::visit(Statement const& _x)
{
	switch (_x.stmt_oneof_case())
	{
	case Statement::kDecl:
		// Hack: Disallow (multi) variable declarations until scope extension is implemented for "for-init"
		if (!m_inForInitScope)
			visit(_x.decl());
		break;
	case Statement::kAssignment:
		visit(_x.assignment());
		break;
	case Statement::kIfstmt:
		visit(_x.ifstmt());
		break;
	case Statement::kStorageFunc:
		visit(_x.storage_func());
		break;
	case Statement::kBlockstmt:
		visit(_x.blockstmt());
		break;
	case Statement::kForstmt:
		visit(_x.forstmt());
		break;
	case Statement::kBoundedforstmt:
		visit(_x.boundedforstmt());
		break;
	case Statement::kSwitchstmt:
		visit(_x.switchstmt());
		break;
	case Statement::kBreakstmt:
		if (m_inForBodyScope)
			m_output << "break\n";
		break;
	case Statement::kContstmt:
		if (m_inForBodyScope)
			m_output << "continue\n";
		break;
	case Statement::kLogFunc:
		visit(_x.log_func());
		break;
	case Statement::kCopyFunc:
		visit(_x.copy_func());
		break;
	case Statement::kExtcodeCopy:
		visit(_x.extcode_copy());
		break;
	case Statement::kTerminatestmt:
		visit(_x.terminatestmt());
		break;
	case Statement::kFunctioncall:
		visit(_x.functioncall());
		break;
	case Statement::STMT_ONEOF_NOT_SET:
		break;
	}
}

void ProtoConverter::openScope(vector<string>&& _x)
{
	m_scopes.push({});
	for (auto const& i: _x)
	{
		m_scopes.top().insert(i);
		m_variables.push_back(i);
	}
}

void ProtoConverter::closeScope()
{
	for (auto const& var: m_scopes.top())
	{
		unsigned numErased = m_variables.size();
		m_variables.erase(remove(m_variables.begin(), m_variables.end(), var), m_variables.end());
		numErased -= m_variables.size();
		yulAssert(numErased == 1, "Proto fuzzer: More than one variable went out of scope");
	}
	m_scopes.pop();
}

void ProtoConverter::addToScope(vector<string>&& _x)
{
	for (string const& i: _x)
	{
		m_variables.push_back(i);
		m_scopes.top().insert(i);
	}
}

void ProtoConverter::visit(Block const& _x, vector<string>&& _y)
{
	openScope(move(_y));
	if (_x.statements_size() > 0)
	{
		m_output << "{\n";
		for (auto const& st: _x.statements())
			visit(st);
		m_output << "}\n";
	}
	else
		m_output << "{}\n";
	closeScope();
}

void ProtoConverter::visit(SpecialBlock const& _x, vector<string>&& _y)
{
	openScope(move(_y));
	m_output << "{\n";
	visit(_x.var());
	if (_x.statements_size() > 0)
		for (auto const& st: _x.statements())
			visit(st);
	m_output << "}\n";
	closeScope();
}

vector<string> ProtoConverter::createVars(unsigned _startIdx, unsigned _endIdx)
{
	yulAssert(_endIdx > _startIdx, "Proto fuzzer: Variable indices not in range");
	string varsStr = dev::suffixedVariableNameList("x_", _startIdx, _endIdx);
	m_output << varsStr;
	vector<string> varsVec;
	boost::split(
		varsVec,
		varsStr,
		boost::algorithm::is_any_of(", "),
		boost::algorithm::token_compress_on
	);

	yulAssert(
		varsVec.size() == (_endIdx - _startIdx),
		"Proto fuzzer: Variable count mismatch during function definition"
	);
	m_counter += varsVec.size();
	return varsVec;
}

template <class T>
void ProtoConverter::createFunctionDefAndCall(T const& _x, unsigned _numInParams, unsigned _numOutParams, NumFunctionReturns _type)
{
	yulAssert(
		((_numInParams <= s_modInputParams - 1) && (_numOutParams <= s_modOutputParams - 1)),
		"Proto fuzzer: Too many function I/O parameters requested."
	);

	// Signature
	// This creates function foo_<noreturn|singlereturn|multireturn>_<m_numFunctionSets>(x_0,...,x_n)
	m_output << "function foo_" << functionTypeToString(_type) << "_" << m_numFunctionSets;
	m_output << "(";
	vector<string> varsVec = {};
	if (_numInParams > 0)
		// Functions must use 0 as the first variable's index until function definition
		// is made a statement. Once function definition as statement is implemented,
		// start index becomes m_counter.
		varsVec = createVars(0, _numInParams);
	m_output << ")";

	vector<string> outVarsVec = {};
	// This creates -> x_n+1,...,x_r
	if (_numOutParams > 0)
	{
		m_output << " -> ";
		if (varsVec.empty())
			varsVec = createVars(_numInParams, _numInParams + _numOutParams);
		else
		{
			outVarsVec = createVars(_numInParams, _numInParams + _numOutParams);
			varsVec.insert(varsVec.end(), outVarsVec.begin(), outVarsVec.end());
		}
	}

	m_output << "\n";

	// Body
	visit(_x.statements(), move(varsVec));

	// Manually create a multi assignment using global variables
	// This prints a_0, ..., a_k-1 for this function that returns "k" values
	if (_numOutParams > 0)
		m_output << dev::suffixedVariableNameList("a_", 0, _numOutParams) << " := ";

	// Call the function with the correct number of input parameters via calls to calldataload with
	// incremental addresses.
	m_output << "foo_" << functionTypeToString(_type) << "_" << std::to_string(m_numFunctionSets);
	m_output << "(";
	for (unsigned i = 0; i < _numInParams; i++)
	{
		m_output << "calldataload(" << std::to_string(i*32) << ")";
		if (i < _numInParams - 1)
			m_output << ",";
	}
	m_output << ")\n";

	for (unsigned i = 0; i < _numOutParams; i++)
		m_output << "sstore(" << std::to_string(i*32) << ", a_" << std::to_string(i) << ")\n";
}

void ProtoConverter::visit(FunctionDefinitionNoReturnVal const& _x)
{
	unsigned numInParams = _x.num_input_params() % s_modInputParams;
	unsigned numOutParams = 0;
	createFunctionDefAndCall(_x, numInParams, numOutParams, NumFunctionReturns::None);
}

void ProtoConverter::visit(FunctionDefinitionSingleReturnVal const& _x)
{
	unsigned numInParams = _x.num_input_params() % s_modInputParams;
	unsigned numOutParams = 1;
	createFunctionDefAndCall(_x, numInParams, numOutParams, NumFunctionReturns::Single);
}

void ProtoConverter::visit(FunctionDefinitionMultiReturnVal const& _x)
{
	unsigned numInParams = _x.num_input_params() % s_modInputParams;
	// Synthesize at least 2 return parameters and at most (s_modOutputParams - 1)
	unsigned numOutParams = std::max<unsigned>(2, _x.num_output_params() % s_modOutputParams);
	createFunctionDefAndCall(_x, numInParams, numOutParams, NumFunctionReturns::Multiple);
}

void ProtoConverter::visit(FunctionDefinition const& _x)
{
	visit(_x.fd_zero());
	visit(_x.fd_one());
	visit(_x.fd_multi());
	m_numFunctionSets++;
}

void ProtoConverter::visit(Program const& _x)
{
	// Initialize input size
	m_inputSize = _x.ByteSizeLong();

	/* Program template is as follows
	 *      Four Globals a_0, a_1, a_2, and a_3 to hold up to four function return values
	 *
	 *      Repeated function definitions followed by function calls of the respective function
	 *          Example: function foo(x_0) -> x_1 {}
	 *                   a_0 := foo(calldataload(0))
	 *                   sstore(0, a_0)
	 */
	m_output << "{\n";
	// Create globals at the beginning
	// This creates let a_0, a_1, a_2, a_3 (followed by a new line)
	m_output << "let " << dev::suffixedVariableNameList("a_", 0, s_modOutputParams - 1) << "\n";
	// Register function interface. Useful while visiting multi var decl/assignment statements.
	for (auto const& f: _x.funcs())
		registerFunction(f);

	for (auto const& f: _x.funcs())
		visit(f);

	yulAssert((unsigned)_x.funcs_size() == m_numFunctionSets, "Proto fuzzer: Functions not correctly registered.");
	m_output << "}\n";
}

string ProtoConverter::programToString(Program const& _input)
{
	visit(_input);
	return m_output.str();
}

void ProtoConverter::registerFunction(FunctionDefinition const& _x)
{
	// No return and single return functions explicitly state the number of values returned
	registerFunction(_x.fd_zero(), NumFunctionReturns::None);
	registerFunction(_x.fd_one(), NumFunctionReturns::Single);
	// A multi return function can have between two and (s_modOutputParams - 1) parameters
	unsigned numOutParams = std::max<unsigned>(2, _x.fd_multi().num_output_params() % s_modOutputParams);
	registerFunction(_x.fd_multi(), NumFunctionReturns::Multiple, numOutParams);
}

std::string ProtoConverter::functionTypeToString(NumFunctionReturns _type)
{
	switch (_type)
	{
	case NumFunctionReturns::None:
		return "noreturn";
	case NumFunctionReturns::Single:
		return "singlereturn";
	case NumFunctionReturns::Multiple:
		return "multireturn";
	}
}