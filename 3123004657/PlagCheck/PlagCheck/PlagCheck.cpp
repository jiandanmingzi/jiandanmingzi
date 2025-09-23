#include "PlagCheck.h"
#include <unicode/brkiter.h>
#include <unicode/ustring.h>
#include <unicode/utext.h>
#include <sstream>
#include <functional>
#include <regex>
#include <iostream>

namespace PlagCheck {

    std::vector<std::string> split_into_words(std::string content) {
        if (content.empty()) {
            return {};
        }
        UErrorCode status = U_ZERO_ERROR;
        UText* ut = utext_openUTF8(NULL, content.c_str(), content.length(), &status);
        if (U_FAILURE(status)) {
            return {};
        }
        icu::BreakIterator* bi = icu::BreakIterator::createWordInstance(
            icu::Locale::getChinese(), status);
        if (U_FAILURE(status)) {
            utext_close(ut);
            return {};
        }
        bi->setText(ut, status);
        if (U_FAILURE(status)) {
            delete bi;
            utext_close(ut);
            return {};
        }
        std::vector<std::string> words;
        int32_t start = bi->first();
        int32_t end = bi->next();
        while (end != icu::BreakIterator::DONE) {
            int32_t length = end - start;
            if (length > 0) {
                std::string word = content.substr(start, length);
                bool is_punctuation = true;
                for (char c : word) {
                    if (!std::ispunct(static_cast<unsigned char>(c)) &&
                        !std::isspace(static_cast<unsigned char>(c))) {
                        is_punctuation = false;
                        break;
                    }
                }
                if (!is_punctuation) {
                    words.push_back(word);
                }
            }
            start = end;
            end = bi->next();
        }
        delete bi;
        utext_close(ut);
        std::cout << "Total words extracted: " << words.size() << std::endl;
        return words;
    }

    std::size_t string_hash(const std::string& str) {
        return std::hash<std::string>{}(str);
    }

    std::bitset<64> compute_simhash(const std::vector<std::string>& words) {
        std::vector<int> hash_vector(64, 0);
        for (const auto& word : words) {
            std::size_t hash_val = string_hash(word);
            for (int i = 0; i < 64; ++i) {
                if (hash_val & (1ULL << i)) {
                    hash_vector[i] += 1;
                }
                else {
                    hash_vector[i] -= 1;
                }
            }
        }
        std::bitset<64> simhash;
        for (int i = 0; i < 64; ++i) {
            if (hash_vector[i] > 0) {
                simhash.set(i);
            }
        }
        return simhash;
    }

    int hamming_distance(const std::bitset<64>& hash1, const std::bitset<64>& hash2) {
        return (hash1 ^ hash2).count();
    }

    double calcu_simi(const std::string& original, const std::string& copyed) {
        auto org_words = split_into_words(original);
        auto cop_words = split_into_words(copyed);
        if (org_words.empty() || cop_words.empty()) {
            return 0.00;
        }
        std::bitset<64> hash1 = compute_simhash(org_words);
        std::bitset<64> hash2 = compute_simhash(cop_words);
        int distance = hamming_distance(hash1, hash2);
        double similarity = 1.0 - (static_cast<double>(distance) / 64.0);
        return std::max(0.0, similarity);
    }
}