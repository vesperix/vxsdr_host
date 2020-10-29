#ifndef ERRORS_HPP
#define ERRORS_HPP

#include <vector>
#include <string>
/* These are defined as preprocessor macros by the C++ standard
     #define  EXIT_SUCCESS             =   0;
     #define  EXIT_FAILURE             =   1;
*/
constexpr int EXIT_IO_ERROR            =   2;
constexpr int EXIT_CMD_LINE_ERROR      =   3;
constexpr int EXIT_FILE_EXIST_ERROR    =   4;
constexpr int EXIT_FILE_CREATE_ERROR   =   5;
constexpr int EXIT_DATA_FORMAT_EROR    =   6;
constexpr int EXIT_INTERNAL_ERROR      =   7;
constexpr int EXIT_SYSTEM_ERROR        =   8;
constexpr int EXIT_NETWORK_ERROR       =   9;
constexpr int EXIT_PARAMETER_ERROR     =  10;
constexpr int EXIT_MEMORY_ERROR        =  11;
constexpr int EXIT_PERMISSION_ERROR    =  12;
constexpr int EXIT_USER_TERMINATED     =  13;
constexpr int EXIT_SIGNAL_TERMINATED   =  14;

// a program can define additional errors at or above PROGRAM_ERROR_BASE
constexpr int PROGRAM_ERROR_BASE       = 100;

const std::vector<std::string> ERROR_DESCRIPTIONS = {
    "Successful termination",
    "Unspecified error",
    "I/O error",
    "Command line usage error",
    "File does not exist",
    "File cannot be created",
    "Data format error",
    "Internal error",
    "System error",
    "Network error",
    "Parameter error",
    "Memory error",
    "Permission denied",
    "Terminated by user",
    "Terminated by signal"
};

#endif // ERRORS_HPP