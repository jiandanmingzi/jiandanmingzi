#pragma once
#include "Fraction.hpp"
#include <string>
#include <vector>
#include <random>
#include <set>
#include <algorithm>
#include <functional>

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
                    } else if (value == "÷") {
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
                        if (op == "*" || op == "÷") return 2;
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
                        if (parentOp == "÷") {
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
                 * @brief 规范化当前子树：仅对可交换运算（+ 和 *）的左右子树按字符串排序；不展平，从而不使用结合律。
                 * @return 规范化后的子树根指针（原地修改，返回this）
                 */
                ExprNode* normalize() {
                    if (!isOperator) return this;
                    if (left) left = left->normalize();
                    if (right) right = right->normalize();
                    if (is_commutative(value)) {
                        std::string l = left ? left->to_string() : "";
                        std::string r = right ? right->to_string() : "";
                        if (r < l) {
                            std::swap(left, right);
                        }
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
         * @brief 判断分数 a 是否小于分数 b
         */
        static bool frac_less(const Fraction& a, const Fraction& b) {
            long long lhs = static_cast<long long>(a.numerator) * b.denominator;
            long long rhs = static_cast<long long>(b.numerator) * a.denominator;
            return lhs < rhs;
        }

        /**
         * @brief 判断分数是否相等
         */
        static bool frac_equal(const Fraction& a, const Fraction& b) {
            return a.denominator != 0 && b.denominator != 0 &&
                   static_cast<long long>(a.numerator) * b.denominator == static_cast<long long>(b.numerator) * a.denominator;
        }

        /**
         * @brief 判断分数是否大于等于另一个分数
         */
        static bool frac_ge(const Fraction& a, const Fraction& b) {
            return !frac_less(a, b);
        }

        /**
         * @brief 判断分数是否大于零
         */
        static bool frac_gt_zero(const Fraction& a) {
            return a.numerator > 0 && a.denominator > 0;
        }

        /**
         * @brief 判断分数是否为零
         */
        static bool frac_is_zero(const Fraction& a) {
            return a.numerator == 0;
        }

        /**
         * @brief 复制表达式树
         */
        ExprNode* clone_tree(ExprNode* node) {
            if (!node) return nullptr;
            ExprNode* n = new ExprNode(node->value, node->isOperator);
            n->left = clone_tree(node->left);
            n->right = clone_tree(node->right);
            return n;
        }

        /**
         * @brief 释放表达式树内存
         */
        void delete_tree(ExprNode* node) {
            if (!node) return;
            delete_tree(node->left);
            delete_tree(node->right);
            delete node;
        }

        /**
         * @brief 生成指定范围内的随机运算符
         * @note range小时不生成除法运算符
         */
        std::string random_operator(bool allow_div) {
            int pick = allow_div ? random_int(0, 3) : random_int(0, 2);
            if (pick == 0) return "+";
            if (pick == 1) return "-";
            if (pick == 2) return "*";
            return "÷";
        }

        /**
         * @brief 生成一个叶子节点（数字）
         */
        ExprNode* make_leaf() {
            Fraction num = generate_number(range);
            num.simplify();
            return new ExprNode(num.to_string(), false);
        }

        /**
         * @brief 生成一个值大于零的子树
         */
        ExprNode* make_positive_subtree(int ops, bool allow_div) {
            const int MAX_TRY = 200;
            for (int t = 0; t < MAX_TRY; ++t) {
                ExprNode* node = build_random_expr(ops, allow_div);
                Fraction v = node->calculate();
                if (frac_gt_zero(v)) return node;
                delete_tree(node);
            }
            if (range > 1) {
                int val = random_int(1, std::max(1, range - 1));
                return new ExprNode(std::to_string(val), false);
            }
            return new ExprNode("0", false);
        }

        /**
         * @brief 生成随机表达式树
         * @param ops 运算符数量
         * @param allow_div 是否允许除法运算
         * @return 表达式树根节点指针
         */
        ExprNode* build_random_expr(int ops, bool allow_div) {
            if (ops == 0) {
                return make_leaf();
            }
            std::string op = random_operator(allow_div);
            int left_ops = (ops == 1) ? 0 : random_int(0, ops - 1);
            int right_ops = (ops - 1) - left_ops;

            // 为防止反复失败，限定重试次数
            const int MAX_TRY = 200;
            for (int t = 0; t < MAX_TRY; ++t) {
                ExprNode* node = new ExprNode(op, true);
                ExprNode* L = nullptr;
                ExprNode* R = nullptr;
                if (op == "+") {
                    L = build_random_expr(left_ops, allow_div);
                    R = build_random_expr(right_ops, allow_div);
                } else if (op == "*") {
                    L = build_random_expr(left_ops, allow_div);
                    R = build_random_expr(right_ops, allow_div);
                } else if (op == "-") {
                    L = build_random_expr(left_ops, allow_div);
                    R = build_random_expr(right_ops, allow_div);
                    Fraction lv = L->calculate();
                    Fraction rv = R->calculate();
                    if (!frac_ge(lv, rv)) {
                        delete_tree(L);
                        delete_tree(R);
                        delete node;
                        continue;
                    }
                } else {
                    if (!allow_div) {
                        delete node;
                        op = random_operator(false);
                        continue;
                    }
                    R = make_positive_subtree(right_ops, allow_div);
                    Fraction rv = R->calculate();
                    if (frac_is_zero(rv)) {
                        delete_tree(R);
                        delete node;
                        continue;
                    }

                    // 多次尝试生成左子树直到 0 < L < R
                    bool ok = false;
                    for (int tt = 0; tt < MAX_TRY; ++tt) {
                        if (L) { delete_tree(L); L = nullptr; }
                        L = make_positive_subtree(left_ops, allow_div);
                        Fraction lv = L->calculate();
                        if (frac_less(lv, rv) && frac_gt_zero(lv)) { ok = true; break; }
                    }
                    if (!ok) {
                        delete_tree(L);
                        delete_tree(R);
                        delete node;
                        continue;
                    }
                }
                node->left = L;
                node->right = R;

                // 检查左右子树的值是否满足运算符的要求
                if (op == "-") {
                    Fraction lv = L->calculate();
                    Fraction rv = R->calculate();
                    if (!frac_ge(lv, rv)) {
                        delete_tree(node);
                        continue;
                    }
                } else if (op == "÷") {
                    Fraction lv = L->calculate();
                    Fraction rv = R->calculate();
                    if (!frac_gt_zero(rv) || !frac_gt_zero(lv) || !frac_less(lv, rv)) {
                        delete_tree(node);
                        continue;
                    }
                }
                return node;
            }

            // 若多次失败，退化为叶子，避免死循环
            return make_leaf();
        }

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
            for (auto* p : expr_trees) delete_tree(p);
            expr_trees.clear();
            answers.clear();
            std::set<std::string> seen;
            bool allow_div = (range > 2);
            const int MAX_GLOBAL_TRY = std::max(count * 50, 200);
            int attempts = 0;
            while ((int)expr_trees.size() < count && attempts < MAX_GLOBAL_TRY) {
                attempts++;
                int ops = random_int(1, 3);
                ExprNode* root = build_random_expr(ops, allow_div);

                // 验证表达式树是否合法，确保所有÷运算和-运算均满足要求
                std::function<bool(ExprNode*)> validate = [&](ExprNode* n)->bool{
                    if (!n || !n->isOperator) return true;
                    bool okL = validate(n->left);
                    bool okR = validate(n->right);
                    if (!okL || !okR) return false;
                    if (n->value == "-") {
                        Fraction lv = n->left->calculate();
                        Fraction rv = n->right->calculate();
                        if (!frac_ge(lv, rv)) return false;
                    } else if (n->value == "÷") {
                        Fraction lv = n->left->calculate();
                        Fraction rv = n->right->calculate();
                        if (!frac_gt_zero(rv) || !frac_gt_zero(lv) || !frac_less(lv, rv)) return false;
                    }
                    return true;
                };
                if (!validate(root)) {
                    delete_tree(root);
                    continue;
                }

                // 规范化表达式树以检测等价表达式
                ExprNode* norm = clone_tree(root);
                norm = norm->normalize();
                std::string key = norm->to_string();
                delete_tree(norm);
                if (seen.find(key) != seen.end()) {
                    delete_tree(root);
                    continue;
                }
                seen.insert(key);
                expr_trees.push_back(root);
            }

            // 若未能生成足够题目，尝试用更少的运算符生成直到满足数量
            while ((int)expr_trees.size() < count) {
                int ops = 1;
                ExprNode* root = build_random_expr(ops, allow_div);
                ExprNode* norm = clone_tree(root);
                norm = norm->normalize();
                std::string key = norm->to_string();
                delete_tree(norm);
                if (seen.insert(key).second) {
                    expr_trees.push_back(root);
                } else {
                    delete_tree(root);
                }
                // 为防止反复失败，限定重试次数
                if ((int)seen.size() > count * 5) break;
            }
        }

        /**
         * @brief 以字符串形式获取题目
         * @return 题目字符串
         */
        std::string get_counter(){
            std::string result;
            for (int i = 0; i < count && i < (int)expr_trees.size(); ++i) {
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
            for (int i = 0; i < count && i < (int)expr_trees.size(); ++i) {
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