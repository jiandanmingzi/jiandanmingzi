#pragma once
#include "Fraction.hpp"
#include <string>
#include <vector>
#include <random>

/**
 * @brief 四则运算表达式生成器类
 * @param int count 生成表达式的数量
 * @param int range 数字范围
 * @param vector<ExprNode*> expr_trees 表达式树节点指针数组
 * @param vector<string> answers 生成表达式答案数组
 */
class CounterGenerator {
    private:
        /**
         * @brief 表示表达式节点的类
         * @param string value 节点的值
         * @param bool isOperator 节点是否为运算符
         * @param ExprNode* left 左子节点指针
         * @param ExprNode* right 右子节点指针
         */
        class ExprNode{
            public:
                std::string value;
                bool isOperator;
                ExprNode* left;
                ExprNode* right;

                /**
                 * @brief 构造函数
                 * @param val 节点的值
                 * @param isOp 节点是否为运算符
                 */
                ExprNode(const std::string& val, bool isOp)
                    : value(val), isOperator(isOp), left(nullptr), right(nullptr) {}

                /**
                 * @brief 计算当前表达式节点的值
                 * @return 计算结果
                 */
                Fraction calculate() {
                    if (!isOperator) {
                        return Fraction(value);
                    }
                    Fraction leftVal = left ? left->calculate() : Fraction(0);
                    Fraction rightVal = right ? right->calculate() : Fraction(0);
                    if (value == "+") {
                        return leftVal + rightVal;
                    } else if (value == "-") {
                        return leftVal - rightVal;
                    } else if (value == "*") {
                        return leftVal * rightVal;
                    } else if (value == "/") {
                        return leftVal / rightVal;
                    }
                    return Fraction(0);
                }

                /**
                 * @brief 以字符串形式表示表达式节点及其子节点
                 * @return 表达式字符串
                 */
                std::string to_string(){
                    if (!isOperator) {
                        return value;
                    }
                    // 辅助：返回运算符优先级（数值/叶子视为最高）
                    auto precedence_of_op = [](const std::string& op) -> int {
                        if (op == "+" || op == "-") return 1;
                        if (op == "*" || op == "/") return 2;
                        return 3;
                    };

                    // 辅助：判定子节点是否需要加括号以保持语义
                    auto need_parentheses = [&](ExprNode* child, const std::string& parentOp, bool isRightChild) -> bool {
                        if (!child || !child->isOperator) return false;

                        int child_prec = precedence_of_op(child->value);
                        int parent_prec = precedence_of_op(parentOp);
                        if (child_prec < parent_prec) return true;
                        if (child_prec > parent_prec) return false;

                        if (parentOp == "+") {
                            return false;
                        }
                        if (parentOp == "*") {
                            return false;
                        }
                        if (parentOp == "-") {
                            return isRightChild;
                        }
                        if (parentOp == "/") {
                            return true;
                        }

                        return false;
                    };

                    std::string l = left ? left->to_string() : "";
                    std::string r = right ? right->to_string() : "";

                    if (left && need_parentheses(left, value, false)) {
                        l = std::string("(") + l + ")";
                    }
                    if (right && need_parentheses(right, value, true)) {
                        r = std::string("(") + r + ")";
                    }

                    return l + " " + value + " " + r;
                }

                /**
                 * @brief 判断运算符是否可交换（例如 +, *）
                 */
                static bool is_commutative(const std::string& op) {
                    return op == "+" || op == "*";
                }

                /**
                 * @brief 判断运算符是否可结合（例如 +, *）
                 */
                static bool is_associative(const std::string& op) {
                    return op == "+" || op == "*";
                }

                /**
                 * @brief 从树中收集与给定运算符相同的操作数（用于展平结合性运算）
                 */
                static void collect_operands(ExprNode* node, const std::string& op, std::vector<ExprNode*>& out) {
                    if (!node) return;
                    if (node->isOperator && node->value == op) {
                        collect_operands(node->left, op, out);
                        collect_operands(node->right, op, out);
                    } else {
                        out.push_back(node);
                    }
                }

                /**
                 * @brief 规范化当前子树：展平结合性且可交换的运算（+ 和 *），并按子表达式字符串排序以获得唯一表示。
                 * @return 规范化后的子树根指针（可能为新分配的节点）
                 */
                ExprNode* normalize() {
                    if (!isOperator) return this;

                    if (left) left = left->normalize();
                    if (right) right = right->normalize();

                    if (is_commutative(value) && is_associative(value)) {
                        std::vector<ExprNode*> ops;
                        collect_operands(this, value, ops);
                        for (auto &p : ops) {
                            if (p) p = p->normalize();
                        }
                        std::sort(ops.begin(), ops.end(), [](ExprNode* a, ExprNode* b){
                            return a->to_string() < b->to_string();
                        });

                        if (ops.empty()) return this;
                        
                        ExprNode* root = ops[0];
                        for (size_t i = 1; i < ops.size(); ++i) {
                            ExprNode* parent = new ExprNode(value, true);
                            parent->left = root;
                            parent->right = ops[i];
                            root = parent;
                        }
                        return root;
                    }
                    return this;
                }
        };

        int count;
        int range;
        std::vector<std::string> answers;
        std::vector<ExprNode*> expr_trees;

    public:
        /**
         * @brief 给出范围和题目个数构造类
         * @param cnt 题目个数
         * @param rng 题目内数值范围
         */
        CounterGenerator(int cnt, int rng)
            : count(cnt), range(rng) {}

        /**
         * @brief 生成[min, max]范围内的随机整数
         * @param min 最小值
         * @param max 最大值
         * @return 生成的随机整数
        */
        int random_int(int min, int max){
            static std::random_device rd;
            static std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(min, max);
            return dis(gen);
        }
        
        /**
         * @brief 生成一个随机真分数
         * @param range 分数的分母范围
         * @return 生成的随机分数
         */
        Fraction generate_fraction(int range){
            int denom = random_int(2, range);
            int numer = random_int(1, denom - 1);
            Fraction frac(numer, denom);
            frac.simplify();
            return frac;
        }

        /**
         * @brief 生成一个随机数，可能是整数或分数
         * @param range 数值范围
         * @return 生成的随机数
         */
        Fraction generate_number(int range){
            if (range > 2) {
                int choice = random_int(1, 2);
                if (choice == 1) {
                    return Fraction(random_int(0, range  - 1), 1);
                } else {
                    return generate_fraction(range - 1);
                }
            }else {
                return Fraction(random_int(0, range - 1), 1);
            }
        }

        /**
         * @brief 生成题目
         */
        void generate_counters() {

        }

        /**
         * @brief 以字符串形式获取题目
         * @return 题目字符串
         */
        std::string get_counter(){
            std::string result;
            for (int i = 0; i < count; ++i) {
                result += std::to_string(i + 1) + ". " + expr_trees[i]->to_string() + " = \n";
            }
            return result;
        }

        /**
         * @brief 获取答案数组
         * @return 答案数组
         */
        std::string get_answers()  {
            Fraction answer;
            answers.clear();
            for (int i = 0; i < count; ++i) {
                answer = expr_trees[i]->calculate();
                answers.push_back(answer.to_string());
            }
            std::string result;
            int i = 1;
            for (const auto& ans : answers) {
                result +=  std::to_string(i) + ". " + ans + "\n";
                i++;
            }
            return result;
        }
};