#pragma once
#include <string>
#include <vector>
#include <sstream>

namespace PlagCheck {
    /*
        @brief 将字符串内容拆分为单词
        @param content 输入的字符串内容
        @return 返回包含单词的字符串向量
    */
    std::vector<std::string> split_into_words(const std::string& content);

    /*
        @brief 计算两个字符串向量的最长公共子序列长度
        @param vec1 第一个字符串向量
        @param vec2 第二个字符串向量
        @return 返回最长公共子序列的长度
    */
    int lcs_length(const std::vector<std::string>& vec1, const std::vector<std::string>& vec2);

    /*
        @brief 计算两个字符串的相似度
        @param original 原始字符串
        @param copyed 复制字符串
        @return 返回字符串的相似度
    */
    double calcu_simi(const std::string& original, const std::string& copyed);
}