#define _CRT_SECURE_NO_WARNINGS

#pragma once
#include <string>
#include <vector>
#include <bitset>

namespace PlagCheck {
    /*
        @brief 将字符串内容拆分为单词
        @param content 输入的字符串内容
        @return 返回包含单词的字符串向量
    */
    std::vector<std::string> split_into_words(std::string content);

    /*
        @brief 计算字符串的哈希值
        @param str 输入的字符串
        @return 返回字符串的哈希值
    */
    std::size_t string_hash(const std::string& str);

    /*
        @brief 计算字符串的SimHash值
        @param words 输入的单词向量
        @return 返回字符串的SimHash值
    */
    std::bitset<64> compute_simhash(const std::vector<std::string>& words);

    /*
        @brief 计算两个SimHash值之间的汉明距离
        @param hash1 第一个SimHash值
        @param hash2 第二个SimHash值
        @return 返回两个SimHash值之间的汉明距离
    */
    int hamming_distance(const std::bitset<64>& hash1, const std::bitset<64>& hash2);

    /*
        @brief 计算两个字符串的相似度
        @param original 原文字符串
        @param copyed 被查重文章的字符串
        @return 返回字符串的相似度
    */
    double calcu_simi(const std::string& original, const std::string& copyed);
}