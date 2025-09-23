#include "FileMana.h"
#include "PlagCheck.h"
#include <iomanip>

int main(int argc, char* argv[]) {
    if (argc < 4){
        std::cout << "three file paths are required as arguments." << std::endl;
        return 1;
    }

    //从命令行中读取文件路径
    std::vector<std::string> filePaths;
    for (int i = 1; i < argc; ++i) {
        filePaths.emplace_back(argv[i]);
    }

    //使用FileManager类处理文件
    FileManager orgPlag(filePaths[0], true, false);
    FileManager copyPlag(filePaths[1], true, false);
    FileManager resultFile(filePaths[2], false, true);
    std::string orgContent = orgPlag.read_lines();
    std::string copyContent = copyPlag.read_lines();

    //简单的相似度检测
    double similarity_rate = PlagCheck::calcu_simi(orgContent, copyContent);

    //将结果写入结果文件
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << similarity_rate;
    std::string result = "repetition rate = " + oss.str() + " \n";
    resultFile.write_lines(result);

    //关闭文件
    orgPlag.close_file();
    copyPlag.close_file();
    resultFile.close_file();
    return 0;
}