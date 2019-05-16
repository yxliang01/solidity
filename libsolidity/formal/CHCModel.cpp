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

#include <libsolidity/formal/CHCModel.h>

#include <libsolidity/ast/TypeProvider.h>
#include <libsolidity/formal/Z3CHCInterface.h>
#include <libsolidity/formal/SymbolicTypes.h>

#include <libdevcore/StringUtils.h>

#include <boost/range/adaptor/map.hpp>
#include <boost/range/adaptors.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/optional.hpp>

using namespace std;
using namespace dev;
using namespace langutil;
using namespace dev::solidity;

CHCModel::CHCModel(ErrorReporter& _errorReporter, map<h256, std::string> const& _smtlib2Responses):
	SMTChecker(_errorReporter, _smtlib2Responses),
	m_interface(make_unique<smt::Z3CHCInterface>()),
	m_chcErrorReporter(m_chcErrors)
{
	m_encodingOnly = true;
}

void CHCModel::analyze(SourceUnit const& _source, shared_ptr<Scanner> const& _scanner)
{
	if (!_source.annotation().experimentalFeatures.count(ExperimentalFeature::SMTChecker))
		return;

	m_scanner = _scanner;

	_source.accept(*this);
}

bool CHCModel::visit(ContractDefinition const& _contract)
{
	if (!shouldVisit(_contract))
		return false;

	reset();

	if (!SMTChecker::visit(_contract))
		return false;

	for (auto const& contract: _contract.annotation().linearizedBaseContracts)
		for (auto var: contract->stateVariables())
			if (*contract == _contract || var->isVisibleInDerivedContracts())
				m_stateVariables.push_back(var);

	for (auto const& var: m_stateVariables)
		m_stateSorts.push_back(smt::smtSort(*var->type()));

	declareSymbols();

	string interfaceName = "interface_" + _contract.name() + "_" + to_string(_contract.id());
	m_interfacePredicate = createBlock(interfaceSort(),	interfaceName);

	auto boolSort = make_shared<smt::Sort>(smt::Kind::Bool);
	auto errorFunctionSort = make_shared<smt::FunctionSort>(
		vector<smt::SortPointer>(),
		boolSort
	);
	m_errorPredicate = createBlock(errorFunctionSort, "error");

	// If the contract has a constructor it is handled as a function.
	if (!_contract.constructor())
	{
		string constructorName = "constructor_" + _contract.name() + "_" + to_string(_contract.id());
		m_constructorPredicate = createBlock(interfaceSort(), constructorName);

		vector<smt::Expression> paramExprs;
		for (auto const& var: m_stateVariables)
		{
			auto const& symbVar = m_context.variable(*var);
			paramExprs.push_back(symbVar->currentValue());
			symbVar->increaseIndex();
			m_interface->declareVariable(symbVar->currentName(), *symbVar->sort());
			m_context.setZeroValue(*symbVar);
		}

		smt::Expression constructorAppl = (*m_constructorPredicate)(paramExprs);
		m_interface->addRule(constructorAppl, constructorName);

		smt::Expression constructorInterface = smt::Expression::implies(
			constructorAppl && assertions(),
			interface()
		);
		m_interface->addRule(constructorInterface, constructorName + "_to_" + interfaceName);
	}

	return true;
}

void CHCModel::endVisit(ContractDefinition const& _contract)
{
	if (!shouldVisit(_contract))
		return;

	auto errorAppl = (*m_errorPredicate)({});
	for (auto const& target: m_verificationTargets)
		query(errorAppl, target->location(), "CHC Assertion violation");

	SMTChecker::endVisit(_contract);
}

bool CHCModel::visit(FunctionDefinition const& _function)
{
	if (!shouldVisit(_function))
		return false;

	resetEncoding(_function);

	solAssert(!m_currentFunction, "Inlining internal function calls not yet implemented");
	m_currentFunction = &_function;

	declareSymbols();

	createFunctionBlock(_function);

	smt::Expression interfaceFunction = smt::Expression::implies(
		interface(),
		function(_function, true)
	);
	m_interface->addRule(
		interfaceFunction,
		m_interfacePredicate->currentName() + "_to_" + m_predicates.at(&_function)->currentName()
	);

	SMTChecker::visit(_function);

	return false;
}

void CHCModel::endVisit(FunctionDefinition const& _function)
{
	if (!shouldVisit(_function))
		return;

	solAssert(m_currentFunction == &_function, "Inlining internal function calls not yet implemented");

	declareSymbols();

	smt::Expression functionInterface = smt::Expression::implies(
		(*m_predicates.at(&_function))(m_functionInputs.at(&_function)) && assertions(),
		interface()
	);
	m_interface->addRule(
		functionInterface,
		m_predicates.at(&_function)->currentName() + "_to_" + m_interfacePredicate->currentName()
	);

	m_currentFunction = nullptr;

	SMTChecker::endVisit(_function);
}

void CHCModel::visitAssert(FunctionCall const& _funCall)
{
	auto const& args = _funCall.arguments();
	solAssert(args.size() == 1, "");
	solAssert(args.front()->annotation().type->category() == Type::Category::Bool, "");

	solAssert(m_currentFunction, "");

	declareSymbols();

	smt::Expression assertNeg = !(m_context.expression(*args.front())->currentValue());
	smt::Expression assertionError = smt::Expression::implies(
		function(*m_currentFunction) && assertions() && assertNeg,
		error()
	);
	string predicateName = "assert_" + to_string(_funCall.id());
	m_interface->addRule(assertionError, predicateName + "_to_error");

	m_verificationTargets.push_back(&_funCall);
}

void CHCModel::reset()
{
	m_predicates.clear();
	m_stateSorts.clear();
	m_stateVariables.clear();
	m_verificationTargets.clear();
	m_interface->reset();
}

bool CHCModel::shouldVisit(ContractDefinition const& _contract)
{
	if (
		_contract.isLibrary() ||
		_contract.isInterface()
	)
		return false;
	return true;
}

bool CHCModel::shouldVisit(FunctionDefinition const& _function)
{
	if (
		_function.isPublic() &&
		_function.isImplemented()
	)
		return true;
	return false;
}

smt::SortPointer CHCModel::functionSort(FunctionDefinition const& _function)
{
	if (m_functionSorts.count(&_function))
		return m_functionSorts.at(&_function);

	auto boolSort = make_shared<smt::Sort>(smt::Kind::Bool);
	vector<smt::SortPointer> localSorts;
	for (auto const& var: _function.parameters() + _function.returnParameters())
		localSorts.push_back(smt::smtSort(*var->type()));
	for (auto const& var: _function.localVariables())
		localSorts.push_back(smt::smtSort(*var->type()));
	auto functionSort = make_shared<smt::FunctionSort>(
		m_stateSorts + localSorts,
		boolSort
	);

	return m_functionSorts[&_function] = move(functionSort);
}

smt::SortPointer CHCModel::interfaceSort()
{
	auto boolSort = make_shared<smt::Sort>(smt::Kind::Bool);
	auto interfaceSort = make_shared<smt::FunctionSort>(
		m_stateSorts,
		boolSort
	);
	return interfaceSort;
}

string CHCModel::predicateName(FunctionDefinition const& _function)
{
	string functionName = _function.isConstructor() ?
		"constructor" :
		_function.isFallback() ?
			"fallback" :
			"function_" + _function.name();
	return functionName + "_" + to_string(_function.id());
}

shared_ptr<smt::SymbolicFunctionVariable> CHCModel::createBlock(smt::SortPointer _sort, string _name)
{
	auto block = make_shared<smt::SymbolicFunctionVariable>(
		_sort,
		_name,
		*m_interface
	);
	m_interface->registerRelation(block->currentValue());
	return block;
}

void CHCModel::createFunctionBlock(FunctionDefinition const& _function)
{
	if (m_predicates.count(&_function))
	{
		m_predicates.at(&_function)->increaseIndex();
		m_interface->registerRelation(m_predicates.at(&_function)->currentValue());
	}
	else
	{
		m_predicates[&_function] = createBlock(
			functionSort(_function),
			predicateName(_function)
		);
	}
}

smt::Expression CHCModel::constructor()
{
	vector<smt::Expression> paramExprs;
	for (auto const& var: m_stateVariables)
		paramExprs.push_back(m_context.variable(*var)->currentValue());
	return (*m_constructorPredicate)(paramExprs);
}

smt::Expression CHCModel::interface()
{
	vector<smt::Expression> paramExprs;
	for (auto const& var: m_stateVariables)
		paramExprs.push_back(m_context.variable(*var)->currentValue());
	return (*m_interfacePredicate)(paramExprs);
}

smt::Expression CHCModel::error()
{
	return (*m_errorPredicate)({});
}

smt::Expression CHCModel::function(FunctionDefinition const& _function, bool _storeParams)
{
	solAssert(m_predicates.count(&_function), "");

	vector<smt::Expression> paramExprs;
	for (auto const& var: m_stateVariables)
		paramExprs.push_back(m_context.variable(*var)->currentValue());
	for (auto const& var: _function.parameters() + _function.returnParameters())
		paramExprs.push_back(m_context.variable(*var)->currentValue());
	for (auto const& var: _function.localVariables())
		paramExprs.push_back(m_context.variable(*var)->currentValue());

	if (_storeParams)
		m_functionInputs[&_function] = paramExprs;

	return (*m_predicates.at(&_function))(move(paramExprs));
}

void CHCModel::query(smt::Expression const& _query, langutil::SourceLocation const& _location, std::string _description)
{
	smt::CheckResult result;
	vector<string> values;
	tie(result, values) = m_interface->query(_query);
	switch (result)
	{
	case smt::CheckResult::SATISFIABLE:
	{
		std::ostringstream message;
		message << _description << " happens here";
		m_chcErrorReporter.warning(_location, message.str());
		break;
	}
	case smt::CheckResult::UNSATISFIABLE:
		break;
	case smt::CheckResult::UNKNOWN:
		m_chcErrorReporter.warning(_location, _description + " might happen here.");
		break;
	case smt::CheckResult::CONFLICTING:
		m_chcErrorReporter.warning(_location, "At least two SMT solvers provided conflicting answers. Results might not be sound.");
		break;
	case smt::CheckResult::ERROR:
		m_chcErrorReporter.warning(_location, "Error trying to invoke SMT solver.");
		break;
	}
}

void CHCModel::declareSymbols()
{
	for (auto const& var: m_context.variables())
		for (unsigned i = 0; i <= var.second->index(); ++i)
			m_interface->declareVariable(var.second->nameAtIndex(i), *var.second->sort());
	for (auto const& expr: m_context.expressions())
		for (unsigned i = 0; i <= expr.second->index(); ++i)
			m_interface->declareVariable(expr.second->nameAtIndex(i), *expr.second->sort());
	for (auto const& expr: m_context.globalSymbols())
		for (unsigned i = 0; i <= expr.second->index(); ++i)
			m_interface->declareVariable(expr.second->nameAtIndex(i), *expr.second->sort());
}
