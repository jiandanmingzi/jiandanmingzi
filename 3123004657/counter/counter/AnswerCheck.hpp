#pragma once
#include "Fraction.hpp"
#include <string>
#include <vector>
#include <map>

/**
 * @brief 检查四则运算答案正确性的类
 * @param wrong 错误题数
 * @param correct 正确题数
 * @param wrong_index 错误题目题号数组
 * @param correct_index 正确题目题号数组
 */
class AnswerCheck{
    private:
        int wrong;
        int correct;
        std::vector<int> wrongIndex;
        std::vector<int> correctIndex;

    public:
        /**
         *   @brief 构造函数，初始化各参数
        */
        AnswerCheck() : wrong(0), correct(0) {}

        /**
         * @brief 增加错误答案记录
         * @param index 题目题号
         */
        void addWrongAnswer(int index) {
            wrong++;
            wrongIndex.push_back(index);
        }

        /**
         * @brief 增加正确答案记录
         * @param index 题目题号
         */
        void addCorrectAnswer(int index) {
            correct++;
            correctIndex.push_back(index);
        }

        /**
         * @brief 获取错误题数
         * @return 错误题数
         */
        int getWrong() const {
            return wrong;
        }

        /**
         * @brief 获取正确题数
         * @return 正确题数
         */
        int getCorrect() const {
            return correct;
        }

        /**
         * @brief 获取错误题号数组
         * @return 错误题号数组
         */
        const std::vector<int>& getWrongIndex() const {
            return wrongIndex;
        }

        /**
         * @brief 获取正确题号数组
         * @return 正确题号数组
         */
        const std::vector<int>& getCorrectIndex() const {
            return correctIndex;
        }

        /**
         * @brief 去除字符串首尾空白字符
         * @param String str 输入字符串
         * @return 去除空白字符后的字符串
         */
        std::string trim(const std::string& str) {
            size_t first = str.find_first_not_of(" \t\n\r");
            if (first == std::string::npos) {
                return "";
            }
            size_t last = str.find_last_not_of(" \t\n\r");
            return str.substr(first, last - first + 1);
        }

        /**
         * @brief 从答案行中提取题号
         * @param String line 答案行字符串
         * @return 题号，若无法提取则返回-1
         */
        int getQuestionNumber(const std::string& line) {
            size_t dotPos = line.find('.');
            if (dotPos != std::string::npos) {
                try {
                    return std::stoi(trim(line.substr(0, dotPos)));
                } catch (...) {
                    return -1;
                }
            }
            return -1;
        }

        /**
         * @brief 从答案行中提取学生答案
         * @param String line 答案行字符串
         * @return 学生答案的分数表示
         */
        Fraction getStudentAnswer(const std::string& line) {
            size_t equalPos = line.find('=');
            if (equalPos != std::string::npos) {
                std::string ansStr = trim(line.substr(equalPos + 1));
                Fraction fracAns = Fraction(ansStr);
                fracAns.simplify();
                return fracAns;
            }
            return Fraction(0, 0); // 默认返回0/0,表示无效答案
        }

        /**
         * @brief 从答案行中提取正确答案
         * @param String line 答案行字符串
         * @return 正确答案的分数表示
         */
        Fraction getCorrectAnswer(const std::string& line) {
            size_t dotPos = line.find('.');
            if (dotPos != std::string::npos) {
                std::string ansStr = trim(line.substr(dotPos + 1));
                Fraction fracAns = Fraction(ansStr);
                fracAns.simplify();
                return fracAns;
            }
            return Fraction(0, 1); // 默认返回0/1
        }

        /**
         * @brief 检查答案是否正确，并记录结果
         * @param String stuAns 学生答案
         * @param String orgAns 正确答案
         */
        void checkAnswer(std::string stuAns, std::string orgAns) {
            std::vector<std::string> stuAnswers;
            std::vector<std::string> orgAnswers;

            std::istringstream stuStream(stuAns);
            std::istringstream orgStream(orgAns);
            std::string line;
            while (std::getline(stuStream, line)) {
                if (!line.empty()) {
                    stuAnswers.push_back(trim(line));
                }
            }
            while (std::getline(orgStream, line)) {
                if (!line.empty()) {
                    orgAnswers.push_back(trim(line));
                }
            }
            if (stuAnswers.size() > orgAnswers.size()) {
                std::cout << "WARNING: Student answers more than the correct answers. Extra answers will be ignored." << std::endl;
            }else if (stuAnswers.size() < orgAnswers.size()) {
                std::cout << "WARNING: Student answers less than the correct answers. Missing answers will be considered wrong." << std::endl;
            }
            std::map<int, Fraction> stuFractions;
            std::map<int, Fraction> orgFractions;
            std::vector<int> allQuestionNumbers;
            for (const auto& ans : stuAnswers) {
                int qNum = getQuestionNumber(ans);
                if (qNum != -1) {
                    stuFractions[qNum] = getStudentAnswer(ans);
                }
            }
            for (const auto& ans : orgAnswers) {
                int qNum = getQuestionNumber(ans);
                if (qNum != -1) {
                    orgFractions[qNum] = getCorrectAnswer(ans);
                    allQuestionNumbers.push_back(qNum);
                }
            }
            for (const auto& qNum : allQuestionNumbers) {
                if (stuFractions.find(qNum) != stuFractions.end()) {
                    if (stuFractions[qNum] == orgFractions[qNum]) {
                        addCorrectAnswer(qNum);
                    } else {
                        addWrongAnswer(qNum);
                    }
                } else {
                    addWrongAnswer(qNum);
                }
            }
        }

        /**
         * @brief 获取结果字符串
         * @return 结果字符串
         */
        std::string get_result() {
            std::string correctContent = "Correct: " + std::to_string(correct) + " (";
            for (size_t i = 0; i < correctIndex.size(); ++i) {
                correctContent += std::to_string(correctIndex[i]);
                if (i != correctIndex.size() - 1) {
                    correctContent += ", ";
                }
            }
            correctContent += ")\n";
            std::string wrongContent = "Wrong: " + std::to_string(wrong) + " (";
            for (size_t i = 0; i < wrongIndex.size(); ++i) {
                wrongContent += std::to_string(wrongIndex[i]);
                if (i != wrongIndex.size() - 1) {
                    wrongContent += ", ";
                }
            }
            wrongContent += ")\n";
            return correctContent + wrongContent;
        }
};