#include "FileMana.hpp"
#include "CounterGenerator.hpp"
#include "AnswerCheck.hpp"

#define WRONG_SITUATION 0
#define CHECK_ANSWER_A 1
#define CHECK_ANSWER_E 10
#define CHECK_ANSWER 11
#define GENERATE_COUNTER 3


int main(int argc, char* argv[]) {
    if (argc < 3){
        std::cout << "At least range parameter is required." << std::endl;
        return 1;
    }

    int flag = WRONG_SITUATION;
    int answerFileIndex = 0;
    int exerciseFileIndex = 0;
    int range = 0;
    int count = 1;          //默认生成1道题
    
    //解析命令行参数
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        std::cout << "Processing argument: " << i << arg << std::endl;
        if (arg == "-r") {
            if (i + 1 < argc) {
                range = std::stoi(argv[i + 1]);
                i++;
                if (flag == CHECK_ANSWER || flag == CHECK_ANSWER_A || flag == CHECK_ANSWER_E){
                    std::cout << "WARNING : GENERATE_COUNTER has higher priority than CHECK_ANSWER." << std::endl;
                }
                flag = GENERATE_COUNTER;
            }
        }else if (arg == "-n") {
            if (i + 1 < argc) {
                count = std::stoi(argv[i + 1]);
                i++;
            }
        }else if (arg == "-e") {
            if (i + 1 < argc) {
                exerciseFileIndex = i + 1;
                i++;
                if (flag == GENERATE_COUNTER){
                    std::cout << "WARNING : GENERATE_COUNTER has higher priority than CHECK_ANSWER." << std::endl;
                }else if (flag == CHECK_ANSWER_A){
                    flag = CHECK_ANSWER;
                }else{
                    flag = CHECK_ANSWER_E;
                }
            }
        }else if (arg == "-a") {
            if (i + 1 < argc) {
                answerFileIndex = i + 1;
                i++;
                if (flag == GENERATE_COUNTER){
                    std::cout << "WARNING : GENERATE_COUNTER has higher priority than CHECK_ANSWER." << std::endl;
                }else if (flag == CHECK_ANSWER_E){
                    flag = CHECK_ANSWER;
                }else{
                    flag = CHECK_ANSWER_A;
                }
            }
        }
    }

    //检查答案参数不完整
    if (flag == CHECK_ANSWER_A){
        std::cout << "WARNING : Missing exercise file parameter for checking answers." << std::endl;
        return 1;
    }
    if (flag == CHECK_ANSWER_E){
        std::cout << "WARNING : Missing answer file parameter for checking answers." << std::endl;
        return 1;
    }

    //生成运算所需命令行参数不足
    if (flag == WRONG_SITUATION){
        std::cout << "ERROR : Couldn't generate without range parameter." << std::endl;
        return 1;
    }

    //多余参数警告
    if (argc > 5){
        std::cout << "WARNING : Too many parameters, ignoring extra parameters." << std::endl;
    }

    //检查答案部分
    if (flag == CHECK_ANSWER){
        std::cout << "Checking answers..." << std::endl;

        //读取题目和答案文件
        std::string exerciseFile = argv[exerciseFileIndex];
        std::string answerFile = argv[answerFileIndex];
        FileManager exerciseFM(exerciseFile, true, false);
        FileManager answerFM(answerFile, true, false);
        std::string exerciseContent = exerciseFM.read_lines();
        std::string answerContent = answerFM.read_lines();

        //检查答案
        AnswerCheck answerCheck = AnswerCheck();
        answerCheck.checkAnswer(exerciseContent, answerContent);

        //将结果写入文件
        std::string result = answerCheck.get_result();
        FileManager resultFM("result.txt", false, true);
        resultFM.write_lines(result);
        return 0;
    }

    //生成题目和答案部分
    if (flag == GENERATE_COUNTER){
        //参数合法性检查
        if (range <= 0){
            std::cout << "ERROR : Range must be positive." << std::endl;
            return 1;
        }
        if (count <= 0 || count > 10000){
            std::cout << "ERROR : Count must be between 1 and 10000." << std::endl;
            return 1;
        }

        std::cout << "Generating " << count << " counters with range " << range << "..." << std::endl;

        //生成题目和答案
        CounterGenerator counterGen = CounterGenerator(count, range);
        counterGen.generate_counters();

        //将题目写入文件
        std::string counters = counterGen.get_counter();
        FileManager counterFM("Exercises.txt", false, true);
        counterFM.write_lines(counters);

        //将答案写入文件
        std::string answers = counterGen.get_answers();
        FileManager answerFM("Answers.txt", false, true);
        answerFM.write_lines(answers);
    }
    return 0;
}