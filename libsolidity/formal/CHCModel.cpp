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

CHCModel::CHCModel(
	smt::EncodingContext& _context,
	ErrorReporter& _errorReporter
):
	m_interface(make_unique<smt::Z3CHCInterface>()),
	m_errorReporter(_errorReporter),
	m_context(_context)
{
}

void CHCModel::analyze(SourceUnit const& _source)
{
	if (_source.annotation().experimentalFeatures.count(ExperimentalFeature::SMTChecker))
		_source.accept(*this);
}

bool CHCModel::visit(ContractDefinition const& _contract)
{
	// TODO
	if (!shouldVisit(_contract))
		return false;

	reset();

	shared_ptr<smt::EncodingContext> contractContext = m_context.intermediateContext(&_contract);
	declareSymbols(*contractContext);

	for (auto const& contract: _contract.annotation().linearizedBaseContracts)
		for (auto var: contract->stateVariables())
			if (*contract == _contract || var->isVisibleInDerivedContracts())
				m_stateVariables.push_back(var);

	for (auto const& var: m_stateVariables)
		m_stateSorts.push_back(smt::smtSort(*var->type()));

	auto boolSort = make_shared<smt::Sort>(smt::Kind::Bool);
	auto functionSort = make_shared<smt::FunctionSort>(
		m_stateSorts,
		boolSort
	);

	string constructorName = "constructor_" + _contract.name() + "_" + to_string(_contract.id());
	m_constructorPredicate = make_shared<smt::SymbolicFunctionVariable>(
		functionSort,
		constructorName,
		*m_interface
	);
	m_interface->registerRelation(m_constructorPredicate->currentValue());

	string interfaceName = "interface_" + _contract.name() + "_" + to_string(_contract.id());
	m_interfacePredicate = make_shared<smt::SymbolicFunctionVariable>(
		functionSort,
		interfaceName,
		*m_interface
	);
	m_interface->registerRelation(m_interfacePredicate->currentValue());

	vector<smt::Expression> beforeExprs;
	vector<smt::Expression> afterExprs;
	smt::Expression constraints(true);

	if (FunctionDefinition const* constructor = _contract.constructor())
	{
		shared_ptr<smt::EncodingContext> constructorContext = m_context.intermediateContext(constructor);
		declareSymbols(*constructorContext);

		auto const& ssaIndices = constructorContext->ssaIndices(constructor);
		solAssert(ssaIndices.size() == 2, "");
		auto const& beforeConstructor = ssaIndices.at(0);
		auto const& afterConstructor = ssaIndices.at(1);

		for (auto const& var: m_stateVariables)
		{
			auto const& symbVar = constructorContext->variable(*var);
			beforeExprs.push_back(symbVar->valueAtIndex(beforeConstructor.at(var)));
			afterExprs.push_back(symbVar->valueAtIndex(afterConstructor.at(var)));
		}

		constraints = constructorContext->constraints();
	}
	else
	{
		for (auto const& var: m_stateVariables)
		{
			auto const& symbVar = contractContext->variable(*var);
			beforeExprs.push_back(symbVar->currentValue());
			symbVar->increaseIndex();
			m_interface->declareVariable(symbVar->currentName(), *symbVar->sort());
			contractContext->setZeroValue(*symbVar);
			afterExprs.push_back(symbVar->currentValue());
			// TODO retrieve and add
			// constraints = ...
			if (smt::isNumber(var->type()->category()))
				constraints = constraints && (symbVar->currentValue() == 0);
		}
	}

	smt::Expression constructorAppl = (*m_constructorPredicate)(beforeExprs);
	m_interface->addRule(constructorAppl, constructorName);

	smt::Expression interfaceAppl = (*m_interfacePredicate)(afterExprs);
	smt::Expression constructorInterface = smt::Expression::implies(
		constructorAppl && constraints,
		interfaceAppl
	);

	m_interface->addRule(constructorInterface, constructorName + "_to_" + interfaceName);

	auto errorFunctionSort = make_shared<smt::FunctionSort>(
		vector<smt::SortPointer>(),
		boolSort
	);
	m_errorPredicate = make_shared<smt::SymbolicFunctionVariable>(
		errorFunctionSort,
		"error",
		*m_interface
	);
	m_interface->registerRelation(m_errorPredicate->currentValue());

	/// TODO Initialize state variables goes in constructor block.

	return true;
}

void CHCModel::endVisit(ContractDefinition const& _contract)
{
	if (!shouldVisit(_contract))
		return;

	auto errorAppl = (*m_errorPredicate)({});
	for (auto const& target: m_verificationTargets)
		query(errorAppl, target->location(), "CHC Assertion violation");
}

bool CHCModel::visit(FunctionDefinition const& _function)
{
	if (!shouldVisit(_function))
		return false;

	solAssert(!m_currentFunction, "");
	m_currentFunction = &_function;

	shared_ptr<smt::EncodingContext> functionContext = m_context.intermediateContext(&_function);
	declareSymbols(*functionContext);

	auto boolSort = make_shared<smt::Sort>(smt::Kind::Bool);
	// TODO include local vars.
	auto functionSort = make_shared<smt::FunctionSort>(
		m_stateSorts,
		boolSort
	);

	string functionName = _function.isFallback() ? "fallback" : _function.name();
	string predicateName = "function_" + functionName + "_" + to_string(_function.id());
	m_predicates[&_function] = make_shared<smt::SymbolicFunctionVariable>(
		functionSort,
		predicateName,
		*m_interface
	);
	m_interface->registerRelation(m_predicates.at(&_function)->currentValue());

	smt::EncodingContext::VariableIndices const& beforeFunction = functionContext->ssaIndices(&_function).at(0);
	vector<smt::Expression> stateExprs;
	for (auto const& var: m_stateVariables)
	{
		auto const& symbVar = functionContext->variable(*var);
		int index = beforeFunction.at(var);
		stateExprs.push_back(symbVar->valueAtIndex(index));
	}
	smt::Expression interfaceAppl = (*m_interfacePredicate)(stateExprs);
	smt::Expression functionAppl = (*m_predicates[&_function])(stateExprs);

	smt::Expression interfaceFunction = smt::Expression::implies(
		interfaceAppl,
		functionAppl
	);
	m_interface->addRule(interfaceFunction, "interface_to_" + predicateName);

	return true;
}

void CHCModel::endVisit(FunctionDefinition const& _function)
{
	if (!shouldVisit(_function))
		return;

	shared_ptr<smt::EncodingContext> functionContext = m_context.intermediateContext(&_function);

	auto const& ssaIndices = functionContext->ssaIndices(&_function);
	solAssert(ssaIndices.size() == 2, "");
	auto const& beforeFunction = ssaIndices.at(0);
	auto const& afterFunction = ssaIndices.at(1);

	vector<smt::Expression> stateExprsBefore;
	vector<smt::Expression> stateExprsAfter;
	for (auto const& var: m_stateVariables)
	{
		auto const& symbVar = functionContext->variable(*var);
		stateExprsBefore.push_back(symbVar->valueAtIndex(beforeFunction.at(var)));
		stateExprsAfter.push_back(symbVar->valueAtIndex(afterFunction.at(var)));
	}
	smt::Expression interfaceAppl = (*m_interfacePredicate)(stateExprsAfter);
	smt::Expression functionAppl = (*m_predicates[&_function])(stateExprsBefore);

	smt::Expression interfaceFunction = smt::Expression::implies(
		functionAppl && functionContext->constraints(),
		interfaceAppl
	);
	string predicateName = "function_" + to_string(_function.id());
	m_interface->addRule(interfaceFunction, predicateName + "_to_interface");

	solAssert(m_currentFunction == &_function, "");
	m_currentFunction = nullptr;
}

void CHCModel::endVisit(FunctionCall const& _funCall)
{
	solAssert(_funCall.annotation().kind != FunctionCallKind::Unset, "");

	if (auto funType = dynamic_cast<FunctionType const*>(_funCall.expression().annotation().type))
		if (funType->kind() == FunctionType::Kind::Assert)
			visitAssert(_funCall);
}

void CHCModel::visitAssert(FunctionCall const& _funCall)
{
	auto const& args = _funCall.arguments();
	solAssert(args.size() == 1, "");
	solAssert(args.front()->annotation().type->category() == Type::Category::Bool, "");

	shared_ptr<smt::EncodingContext> assertContext = m_context.intermediateContext(&_funCall);
	auto const& indices = assertContext->ssaIndices(&_funCall).at(0);

	vector<smt::Expression> stateExprs;
	for (auto const& var: m_stateVariables)
	{
		auto const& symbVar = assertContext->variable(*var);
		stateExprs.push_back(symbVar->valueAtIndex(indices.at(var)));
	}

	smt::Expression errorAppl = (*m_errorPredicate)({});
	solAssert(m_currentFunction, "");

	smt::Expression functionAppl = (*m_predicates.at(m_currentFunction))(stateExprs);
	smt::Expression assertNeg = !(assertContext->expression(*args.front())->currentValue());
	smt::Expression assertionError = smt::Expression::implies(
		functionAppl && assertContext->constraints() && assertNeg,
		errorAppl
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
		/*_function.isPublic() &&*/
		_function.isImplemented()
	)
		return true;
	return false;
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
		m_errorReporter.warning(_location, message.str());
		break;
	}
	case smt::CheckResult::UNSATISFIABLE:
		break;
	case smt::CheckResult::UNKNOWN:
		m_errorReporter.warning(_location, _description + " might happen here.");
		break;
	case smt::CheckResult::CONFLICTING:
		m_errorReporter.warning(_location, "At least two SMT solvers provided conflicting answers. Results might not be sound.");
		break;
	case smt::CheckResult::ERROR:
		m_errorReporter.warning(_location, "Error trying to invoke SMT solver.");
		break;
	}
}

void CHCModel::declareSymbols(smt::EncodingContext const& _context)
{
	for (auto const& var: _context.variables())
		for (unsigned i = 0; i <= var.second->index(); ++i)
			m_interface->declareVariable(var.second->nameAtIndex(i), *var.second->sort());
	for (auto const& expr: _context.expressions())
		for (unsigned i = 0; i <= expr.second->index(); ++i)
			m_interface->declareVariable(expr.second->nameAtIndex(i), *expr.second->sort());
	for (auto const& expr: _context.globalSymbols())
		for (unsigned i = 0; i <= expr.second->index(); ++i)
			m_interface->declareVariable(expr.second->nameAtIndex(i), *expr.second->sort());
}
