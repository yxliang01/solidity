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

#pragma once

#include <libsolidity/formal/SMTEncoder.h>

#include <libsolidity/formal/CHCSolverInterface.h>

namespace dev
{
namespace solidity
{

class CHCModel: public SMTEncoder
{
public:
	CHCModel(smt::EncodingContext& _context, langutil::ErrorReporter& _errorReporter);

	void analyze(SourceUnit const& _sources, std::shared_ptr<langutil::Scanner> const& _scanner);

private:
	/// Visitor functions.
	//@{
	bool visit(ContractDefinition const& _node) override;
	void endVisit(ContractDefinition const& _node) override;
	bool visit(FunctionDefinition const& _node) override;
	void endVisit(FunctionDefinition const& _node) override;

	void visitAssert(FunctionCall const& _funCall);
	//@}

	/// Helpers.
	//@{
	void reset();
	bool shouldVisit(ContractDefinition const& _contract);
	bool shouldVisit(FunctionDefinition const& _function);
	//@}

	/// Sort helpers.
	//@{
	smt::SortPointer functionSort(FunctionDefinition const& _function);
	smt::SortPointer interfaceSort();
	//@}

	/// Predicate helpers.
	//@{
	std::string predicateName(FunctionDefinition const& _function);

	std::shared_ptr<smt::SymbolicFunctionVariable> createBlock(smt::SortPointer _sort, std::string _name);
	void createFunctionBlock(FunctionDefinition const& _function);

	smt::Expression constructor();
	smt::Expression interface();
	smt::Expression error();
	smt::Expression function(FunctionDefinition const& _function, bool _storeParams = false);
	//@}

	/// Solver related.
	//@{
	void query(smt::Expression const& _query, langutil::SourceLocation const& _location, std::string _description);
	void declareSymbols();
	//@}

	/// Predicates.
	//@{
	/// Constructor predicate.
	/// Default constructor sets state vars to 0.
	std::shared_ptr<smt::SymbolicVariable> m_constructorPredicate;

	/// Artificial Interface predicate.
	/// Single entry block for all functions.
	std::shared_ptr<smt::SymbolicVariable> m_interfacePredicate;

	/// Artificial Error predicate.
	/// Single error block for all assertions.
	std::shared_ptr<smt::SymbolicVariable> m_errorPredicate;

	/// Maps AST nodes to their predicates.
	std::unordered_map<ASTNode const*, std::shared_ptr<smt::SymbolicVariable>> m_predicates;
	//@}

	/// Variables.
	//@{
	/// State variables sorts.
	/// Used by all predicates.
	std::vector<smt::SortPointer> m_stateSorts;
	/// State variables.
	/// Used to create all predicates.
	std::vector<VariableDeclaration const*> m_stateVariables;

	/// Input sorts for function predicates.
	std::map<FunctionDefinition const*, smt::SortPointer> m_functionSorts;
	/// Input variables of the latest block related to a function.
	std::map<FunctionDefinition const*, std::vector<smt::Expression>> m_functionInputs;
	//@}

	/// Verification targets.
	//@{
	std::vector<Expression const*> m_verificationTargets;
	//@}

	/// Control-flow.
	//@{
	FunctionDefinition const* m_currentFunction = nullptr;
	//@}

	langutil::ErrorReporter& m_outerErrorReporter;

	/// CHC solver.
	std::unique_ptr<smt::CHCSolverInterface> m_interface;
};

}
}
