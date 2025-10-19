#pragma once
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <stdexcept>
#include <filesystem>

/*
    @brief 文件管理类
    @details 提供文件的打开、读取、写入和关闭功能
    @param filePath 文件路径
    @param fileStream 文件流对象
    @param isOpen 文件是否打开
    @param readable 文件是否可读
    @param writable 文件是否可写
    @method FileManager 构造函数，打开文件
    @method ~FileManager 析构函数，关闭文件
    @method is_file_open 检查文件是否打开
    @method is_file_readable 检查文件是否可读
    @method is_file_writable 检查文件是否可写
    @method read_lines 读取文件内容，返回字符串
    @method write_lines 写入内容到文件
*/
class FileManager
{
private:
    std::string filePath;
    std::fstream fileStream;
    bool isOpen = false;
    bool readable = false;
    bool writable = false;

public:
    /*
    @brief 构造函数，打开文件
    @param path 文件路径
    @param read 是否以读模式打开
    @param write 是否以写模式打开
    @throws invalid_argument 如果读写模式均为false
    @throws runtime_error 如果文件无法打开
    */
    FileManager(const std::string& path, bool read, bool write)
        : filePath(path), readable(read), writable(write)
    {
        std::ios_base::openmode mode = std::ios_base::binary;
        if (readable && writable) {
            mode |= std::ios_base::in | std::ios_base::out;
        }
        else if (readable) {
            mode |= std::ios_base::in;
        }
        else if (writable) {
            mode |= std::ios_base::out | std::ios_base::trunc;
        }
        else {
            throw std::invalid_argument("File must be opened in at least read or write mode.");
        }

        fileStream.open(filePath, mode);
        if (!fileStream.is_open()) {
            throw std::runtime_error("Failed to open file: " + filePath + " in mode " + std::to_string(mode));
        }
        isOpen = true;
    }

    /*
        @brief 析构函数，关闭文件
    */
    ~FileManager()
    {
        if (isOpen) {
            fileStream.close();
        }
    }

    /*
        @brief 检查文件是否打开
        @return 如果文件已打开返回true，否则返回false
    */
    bool is_file_open()
    {
        return isOpen && fileStream.is_open();
    }

    /*
        @brief 检查文件是否可读
        @return 如果文件已打开且可读返回true，否则返回false
    */
    bool is_file_readable()
    {
        return is_file_open() && readable;
    }

    /*
        @brief 检查文件是否可写
        @return 如果文件已打开且可写返回true，否则返回false
    */
    bool is_file_writable()
    {
        return is_file_open() && writable;
    }

    /*
        @brief 读取文件内容，返回字符串
        @return 返回文件内容的字符串
        @throws runtime_error 如果文件未打开或不可读
    */
    std::string read_lines()
    {
        if (!is_file_readable()) {
            throw std::runtime_error("File is not open for reading.");
        }
        std::stringstream buffer;
        buffer << fileStream.rdbuf();
        std::string content = buffer.str();
        return content;
    }

    /*
        @brief 写入内容到文件
        @param content 要写入的内容字符串
        @return 如果写入成功返回true，否则返回false
        @throws runtime_error 如果文件未打开或不可写
    */
    bool write_lines(const std::string& content)
    {
        if (!is_file_writable()) {
            throw std::runtime_error("File is not open for writing.");
        }

        fileStream << content;
        return !fileStream.fail();
    }

    /*
        @brief 关闭文件
    */
    void close_file()
    {
        if (isOpen) {
            fileStream.close();
            isOpen = false;
        }
    }
};