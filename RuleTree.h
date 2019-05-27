#pragma once

#include "BasicTradingTypes.h"
#include "ParentOrder.h"

using namespace QSpark;

template <class ResultType>
struct RuleTree
{
	class Rule
	{
	public:
		using RuleFunctionType = std::function<bool(ParentOrder& parent)>;

		Rule() = default;

		Rule(const Rule& rule) { m_ruleFunction = rule.m_ruleFunction; }

		template <class FuncType>
		explicit Rule(FuncType function) : m_ruleFunction(function) { }

		virtual bool Check(ParentOrder& parent) { return m_ruleFunction(parent); }

	protected:
		RuleFunctionType m_ruleFunction;

	};

	class Branch;
	class TreeNode : public Named
	{
	public:
		explicit TreeNode(Named::NameString name) : Named(name) { }

		virtual ~TreeNode() = default;

		void AddRule(Rule rule) { m_rules.push_back(rule); }

		void SetParent(Branch* node) { m_parent = node; }

		Branch* GetParent() { return m_parent; }

		virtual boost::optional<ResultType> Get(ParentOrder& parent) = 0;

		virtual TreeNode* GetNodeByName(Named::NameString name) = 0;

		virtual bool IsLeaf() = 0;

		virtual void DumpTree(std::ostream& out, size_t level = 0) = 0;

	protected:
		void indent(std::ostream& out, size_t level)
		{
			for (size_t i = 0; i < level; i++)
			{
				out << "\t";

			}
		}

		bool allRulesPassed(ParentOrder& parent)
		{
			for (auto& rule : m_rules)
			{
				if (!rule.Check(parent))
				{
					return false;
				}
			}
			return true;
		}

		Branch* m_parent;
		std::vector<Rule> m_rules;
	};

	class Branch : public TreeNode
	{
	public:
		using Parent = TreeNode;

		Branch(Named::NameString name) : Parent(name) { }

		bool IsLeaf() override { return false; }

		TreeNode* GetNodeByName(Named::NameString name) override
		{
			if (Parent::GetName() == name)
				return this;

			for (auto* node : m_nodes)
			{
				auto* result = node->GetNodeByName(name);
				if (result != nullptr)
				{
					return result;
				}
			}
			return nullptr;
		}

		void AddNode(TreeNode* node)
		{
			m_nodes.push_back(node);
			node->SetParent(this);
		}

		void ResetAllocations()
		{
			m_lastAllocated = 0;
			for (auto& allocation : m_percentageAllocations)
			{
				allocation = nullptr;
			}

			for (TreeNode* node : m_nodes)
			{
				m_nodesPercentageMap[node] = 0;
				if (node->IsLeaf())
					continue;

				auto* branch = static_cast<Branch*>(node);
				branch->ResetAllocations();
			}
		}

		void SpreadPercentageOnAllNotSetNodes()
		{
			if (m_lastAllocated == 0)
				SpreadPercentage();

			for (TreeNode* node : m_nodes)
			{
				if (node->IsLeaf())
					continue;

				auto* branch = static_cast<Branch*>(node);
				branch->SpreadPercentageOnAllNotSetNodes();
			}
		}

		void SpreadPercentage()
		{
			auto probabilityPerNode = 100 / m_nodes.size();
			for (auto* node : m_nodes)
			{
				AllocatePercentage(probabilityPerNode, node);
			}

			if (m_lastAllocated != 100)
			{
				auto diff = 100 - m_lastAllocated;
				for (auto* node : m_nodes)
				{
					if (diff <= 0)
						return;

					AllocatePercentage(1, node);
					diff--;
				}
			}
		}

		boost::optional<ResultType> Get(ParentOrder& parent) override
		{
			if (!Parent::allRulesPassed(parent))
				return boost::none;

			for (auto i = 0; i < MAX_TRIES; i++)
			{
				auto* node = getRandomNode();
				if (node)
				{
					auto result = node->Get(parent);
					if (result)
						return result;
				}
			}
			return boost::none;
		}

		void AllocatePercentage(size_t percentage, TreeNode* node)
		{
			if (m_lastAllocated + percentage > 100)
				throw Exception(Exception::Stream() << Parent::GetName() << ": Bad percent allocation(" << m_lastAllocated + percentage << ")");

			if (std::find(m_nodes.begin(), m_nodes.end(), node) == m_nodes.end())
				throw Exception(Exception::Stream() << Parent::GetName() << ": Node not in branch!");

			for (size_t i = m_lastAllocated; i < m_lastAllocated + percentage; i++)
			{
				m_percentageAllocations[i] = node;
			}

			m_nodesPercentageMap[node] += percentage;
			m_lastAllocated += percentage;
		}

		void DumpTree(std::ostream& out, size_t level = 0) override
		{
			Parent::indent(out, level);
			out << Parent::GetName() << std::endl;
			for (TreeNode* node : m_nodes)
			{
				Parent::indent(out, level);
				out << m_nodesPercentageMap[node] << " : ";
				node->DumpTree(out, level + 1);
			}
		}

	protected:
		TreeNode* getRandomNode()
		{
			if (m_lastAllocated != 100)
				throw Exception(Exception::Stream() << Parent::GetName() << ": Bad percent allocation! "
						"requesting a random node when sum percent is not 100(" << m_lastAllocated  << ")");

			auto cumPercent = 0;
			auto percent = rand() % 100;
			return m_percentageAllocations[percent];
		}

		static constexpr auto MAX_TRIES = 100000;
		std::vector<TreeNode*> m_nodes;
		std::map<TreeNode*, size_t> m_nodesPercentageMap;
		std::array<TreeNode*, 100> m_percentageAllocations;
		size_t m_lastAllocated = 0;
	};

	class Leaf : public TreeNode
	{
	public:
		using Parent = TreeNode;

		Leaf(Named::NameString name, ResultType val) : Parent(name), m_val(val) { }

		bool IsLeaf() override { return true; }

		void DumpTree(std::ostream& out, size_t level) override
		{
			Parent::indent(out, level);
			out << Parent::GetName() << std::endl;
		}

		boost::optional<ResultType> Get(ParentOrder& parent) override
		{
			if (!Parent::allRulesPassed(parent))
				return boost::none;

			return m_val;
		}

		TreeNode* GetNodeByName(Named::NameString name) override
		{
			if (Parent::GetName() == name)
				return this;

			return nullptr;
		}

	private:
		ResultType m_val;
	};

	using FactoryType = Factory<TreeNode>;
};
