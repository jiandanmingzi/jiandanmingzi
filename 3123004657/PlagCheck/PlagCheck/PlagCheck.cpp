#include "PlagCheck.h"

namespace PlagCheck {

    std::vector<std::string> split_into_words(const std::string& content) {
        std::vector<std::string> words;
        std::istringstream stream(content);
        std::string word;
        while (stream >> word) {
            words.push_back(word);
        }
        return words;
    }

    int lcs_length(const std::vector<std::string>& vec1, const std::vector<std::string>& vec2) {
        int m = vec1.size();
        int n = vec2.size();
        std::vector<std::vector<int>> dp(m + 1, std::vector<int>(n + 1, 0));
        for (int i = 1; i <= m; ++i) {
            for (int j = 1; j <= n; ++j) {
                if (vec1[i - 1] == vec2[j - 1]) {
                    dp[i][j] = dp[i - 1][j - 1] + 1;
                } else {
                    dp[i][j] = std::max(dp[i - 1][j], dp[i][j - 1]);
                }
            }
        }
        return dp[m][n];
    }

    double calcu_simi(const std::string& original, const std::string& copyed) {
        auto org = split_into_words(original);
        auto cop = split_into_words(copyed);
        int lcs_len = lcs_length(org, cop);
        if (org.empty() || cop.empty()) {
            return 0.00;
        }
        double similarity = lcs_len * 1.0 / (org.size() * 1.0);
        return similarity;
    }

}