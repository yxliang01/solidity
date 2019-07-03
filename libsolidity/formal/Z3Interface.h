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

#include <libsolidity/formal/SolverInterface.h>
#include <boost/noncopyable.hpp>
#include <z3++.h>

namespace dev
{
namespace solidity
{
namespace smt
{

class Z3Interface: public SolverInterface, public boost::noncopyable
{
public:
	Z3Interface();
	Z3Interface(std::shared_ptr<z3::context> _context):
		m_context(_context),
		m_solver(*m_context)
	{}

	void reset() override;

	void push() override;
	void pop() override;

	void declareVariable(std::string const& _name, Sort const& _sort) override;

	void addAssertion(Expression const& _expr) override;
	std::pair<CheckResult, std::vector<std::string>> check(std::vector<Expression> const& _expressionsToEvaluate) override;

	z3::expr toZ3Expr(Expression const& _expr);

	std::map<std::string, z3::expr> constants() const { return m_constants; }
	std::map<std::string, z3::func_decl> functions() const { return m_functions; }

protected:
	void declareFunction(std::string const& _name, Sort const& _sort);

	z3::sort z3Sort(smt::Sort const& _sort);
	z3::sort_vector z3Sort(std::vector<smt::SortPointer> const& _sorts);

	std::shared_ptr<z3::context> m_context;
	std::map<std::string, z3::expr> m_constants;
	std::map<std::string, z3::func_decl> m_functions;

private:
	z3::solver m_solver;
};

}
}
}
