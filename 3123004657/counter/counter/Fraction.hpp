#pragma once
#include <string>
#include <unordered_map>

/**
 * @brief 分数类
 * @param int numerator 分子
 * @param int denominator 分母
 */
class Fraction{
    public:
        int numerator;
        int denominator;

        /**
         * @brief 四则运算答案检查类
         */
        static std::unordered_map<std::string, int> opPrecedence;

        /**
         * @brief 构造函数
         * @param num 分子
         * @param denom 分母
         */
        Fraction(int num = 0, int denom = 1) : numerator(num), denominator(denom) {}

        /**
         * @brief 从字符串构造分数
         * @param fracStr 分数字符串
         */
        Fraction(std::string fracStr) {
            size_t slashPos = fracStr.find('/');
            if (slashPos != std::string::npos) {
                numerator = std::stoi(fracStr.substr(0, slashPos));
                denominator = std::stoi(fracStr.substr(slashPos + 1));
            } else {
                numerator = std::stoi(fracStr);
                denominator = 1;
            }
        }

        /**
         * @brief 化简分数
         */
        void simplify() {
            int a = numerator;
            int b = denominator;
            while (b != 0) {
                int temp = b;
                b = a % b;
                a = temp;
            }
            numerator /= a;
            denominator /= a;
        }

        /**
         * @brief 将分数转换为字符串
         * @return 分数字符串
         */
        std::string to_string() const {
            if (denominator == 1) {
                return std::to_string(numerator);
            } else {
                return std::to_string(numerator) + "/" + std::to_string(denominator);
            }
        }

        /**
         * @brief 重载等于运算符
         * @param other 另一个分数
         * @return 是否相等
         */
        bool operator==(const Fraction& other) const {
            if (denominator == 0 || other.denominator == 0) {
                return false;
            }
            return numerator * other.denominator == denominator * other.numerator;
        }

        Fraction operator+(const Fraction& other) const {
            int num = numerator * other.denominator + other.numerator * denominator;
            int denom = denominator * other.denominator;
            Fraction result(num, denom);
            result.simplify();
            return result;
        }

        Fraction operator-(const Fraction& other) const {
            int num = numerator * other.denominator - other.numerator * denominator;
            int denom = denominator * other.denominator;
            Fraction result(num, denom);
            result.simplify();
            return result;
        }

        Fraction operator*(const Fraction& other) const {
            int num = numerator * other.numerator;
            int denom = denominator * other.denominator;
            Fraction result(num, denom);
            result.simplify();
            return result;
        }

        Fraction operator/(const Fraction& other) const {
            int num = numerator * other.denominator;
            int denom = denominator * other.numerator;
            Fraction result(num, denom);
            result.simplify();
            return result;
        }
};

std::unordered_map<std::string, int> Fraction::opPrecedence = {
    {"+", 1},
    {"-", 1},
    {"*", 2},
    {"÷", 2},
    {"/", 2},
    {"(", 0},
    {")", 0}
};