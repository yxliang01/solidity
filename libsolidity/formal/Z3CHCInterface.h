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

#include <libsolidity/formal/CHCSolverInterface.h>
#include <libsolidity/formal/Z3Interface.h>

namespace dev
{
namespace solidity
{
namespace smt
{

class Z3CHCInterface: public CHCSolverInterface
{
public:
	Z3CHCInterface();

	void declareVariable(std::string const& _name, Sort const& _sort);

	void registerRelation(Expression const& _expr) override;

	void addRule(Expression const& _expr, std::string const& _name) override;

	std::pair<CheckResult, std::vector<std::string>> query(Expression const& _expr) override;

	std::shared_ptr<Z3Interface> z3Interface() { return m_z3Interface; }

private:
	// Horn solver.
	std::shared_ptr<z3::context> m_context;
	z3::fixedpoint m_solver;

	// Used to handle variables.
	std::shared_ptr<Z3Interface> m_z3Interface;

	// Used to bound all vars as universally quantified.
	z3::expr_vector m_variables;

	// SMT query timeout in milliseconds.
	static int const queryTimeout = 10000;
};

}
}
}
