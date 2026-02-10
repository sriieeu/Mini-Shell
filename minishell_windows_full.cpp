#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <cstring>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <direct.h>
    #include <io.h>
    #define chdir _chdir
    #define getcwd _getcwd
#else

#endif

// ============================================================================
// DATA STRUCTURES
// ============================================================================

/**
 * Command structure to hold parsed command information
 */
struct Command {
    std::vector<std::string> args;     // Command and arguments
    std::string input_file;             // Input redirection file
    std::string output_file;            // Output redirection file
    bool append_output;                 // Append mode for output
    bool background;                    // Run in background
    
    Command() : append_output(false), background(false) {}
};

/**
 * Job structure for background job management
 */
struct Job {
    HANDLE hProcess;                    // Process handle
    DWORD processId;                    // Process ID
    std::string command;                // Command string
    bool completed;                     // Completion status
    
    Job() : hProcess(NULL), processId(0), completed(false) {}
    Job(HANDLE h, DWORD pid, const std::string& cmd) 
        : hProcess(h), processId(pid), command(cmd), completed(false) {}
};

// ============================================================================
// WINDOWS SHELL CLASS
// ============================================================================

class WindowsShell {
private:
    std::map<int, Job> jobs;            // Background jobs map
    int job_counter;                     // Job ID counter
    bool running;                        // Shell running state
    
    // ========================================================================
    // PARSING
    // ========================================================================
    
    /**
     * Parse user input into command structures
     */
    std::vector<Command> parse_input(const std::string& input) {
        std::vector<Command> commands;
        Command current_cmd;
        
        std::istringstream iss(input);
        std::string token;
        bool expect_input_file = false;
        bool expect_output_file = false;
        
        while (iss >> token) {
            if (token == "|") {
                if (!current_cmd.args.empty()) {
                    commands.push_back(current_cmd);
                    current_cmd = Command();
                }
            } else if (token == "<") {
                expect_input_file = true;
            } else if (token == ">") {
                expect_output_file = true;
                current_cmd.append_output = false;
            } else if (token == ">>") {
                expect_output_file = true;
                current_cmd.append_output = true;
            } else if (token == "&") {
                current_cmd.background = true;
            } else if (expect_input_file) {
                current_cmd.input_file = token;
                expect_input_file = false;
            } else if (expect_output_file) {
                current_cmd.output_file = token;
                expect_output_file = false;
            } else {
                current_cmd.args.push_back(token);
            }
        }
        
        if (!current_cmd.args.empty()) {
            commands.push_back(current_cmd);
        }
        
        return commands;
    }
    
    // ========================================================================
    // BUILT-IN COMMANDS
    // ========================================================================
    
    /**
     * Check if command is built-in and handle it
     */
    bool handle_builtin(const Command& cmd) {
        if (cmd.args.empty()) return false;
        
        const std::string& command = cmd.args[0];
        
        if (command == "cd") {
            builtin_cd(cmd.args);
            return true;
        } else if (command == "exit") {
            builtin_exit(cmd.args);
            return true;
        } else if (command == "jobs") {
            builtin_jobs();
            return true;
        } else if (command == "fg") {
            builtin_fg(cmd.args);
            return true;
        } else if (command == "kill") {
            builtin_kill(cmd.args);
            return true;
        } else if (command == "help") {
            builtin_help();
            return true;
        } else if (command == "cls" || command == "clear") {
            system("cls");
            return true;
        }
        
        return false;
    }
    
    void builtin_cd(const std::vector<std::string>& args) {
        const char* path;
        
        if (args.size() < 2) {
            path = getenv("USERPROFILE");
            if (!path) path = getenv("HOME");
            if (!path) {
                std::cerr << "cd: HOME not set\n";
                return;
            }
        } else {
            path = args[1].c_str();
        }
        
        if (chdir(path) != 0) {
            std::cerr << "cd: cannot change directory to " << path << "\n";
        }
    }
    
    void builtin_exit(const std::vector<std::string>& args) {
        int exit_code = 0;
        
        if (args.size() > 1) {
            try {
                exit_code = std::stoi(args[1]);
            } catch (...) {
                std::cerr << "exit: invalid argument\n";
                return;
            }
        }
        
        // Clean up all jobs
        for (auto& job_pair : jobs) {
            if (job_pair.second.hProcess) {
                CloseHandle(job_pair.second.hProcess);
            }
        }
        
        running = false;
        exit(exit_code);
    }
    
    void builtin_jobs() {
        check_background_jobs();
        
        if (jobs.empty()) {
            std::cout << "No background jobs\n";
            return;
        }
        
        for (const auto& job_pair : jobs) {
            if (!job_pair.second.completed) {
                std::cout << "[" << job_pair.first << "] " 
                          << "PID:" << job_pair.second.processId << " "
                          << job_pair.second.command << std::endl;
            }
        }
    }
    
    void builtin_fg(const std::vector<std::string>& args) {
        if (args.size() < 2) {
            std::cerr << "fg: job id required\n";
            return;
        }
        
        int job_id;
        try {
            job_id = std::stoi(args[1]);
        } catch (...) {
            std::cerr << "fg: invalid job id\n";
            return;
        }
        
        auto it = jobs.find(job_id);
        if (it == jobs.end()) {
            std::cerr << "fg: job not found\n";
            return;
        }
        
        HANDLE hProcess = it->second.hProcess;
        std::cout << it->second.command << std::endl;
        
        // Wait for the process
        WaitForSingleObject(hProcess, INFINITE);
        
        CloseHandle(hProcess);
        remove_job(job_id);
    }
    
    void builtin_kill(const std::vector<std::string>& args) {
        if (args.size() < 2) {
            std::cerr << "kill: job id required\n";
            return;
        }
        
        int job_id;
        try {
            job_id = std::stoi(args[1]);
        } catch (...) {
            std::cerr << "kill: invalid job id\n";
            return;
        }
        
        auto it = jobs.find(job_id);
        if (it == jobs.end()) {
            std::cerr << "kill: job not found\n";
            return;
        }
        
        if (TerminateProcess(it->second.hProcess, 1)) {
            std::cout << "[" << job_id << "] Terminated\n";
            CloseHandle(it->second.hProcess);
            remove_job(job_id);
        } else {
            std::cerr << "kill: failed to terminate process\n";
        }
    }
    
    void builtin_help() {
        std::cout << "\n=== Windows Mini Shell Help ===\n\n";
        std::cout << "Built-in Commands:\n";
        std::cout << "  cd [dir]     - Change directory\n";
        std::cout << "  exit [code]  - Exit shell\n";
        std::cout << "  jobs         - List background jobs\n";
        std::cout << "  fg <id>      - Bring job to foreground\n";
        std::cout << "  kill <id>    - Terminate background job\n";
        std::cout << "  cls/clear    - Clear screen\n";
        std::cout << "  help         - Show this help\n\n";
        std::cout << "Features:\n";
        std::cout << "  Pipes:       cmd1 | cmd2 | cmd3\n";
        std::cout << "  Redirect:    cmd < input.txt > output.txt\n";
        std::cout << "  Append:      cmd >> output.txt\n";
        std::cout << "  Background:  cmd &\n\n";
        std::cout << "Examples:\n";
        std::cout << "  dir | findstr .cpp\n";
        std::cout << "  type file.txt | sort > sorted.txt\n";
        std::cout << "  ping google.com -n 10 &\n";
        std::cout << "  jobs\n";
        std::cout << "  kill 1\n\n";
    }
    
    // ========================================================================
    // PROCESS CREATION AND EXECUTION
    // ========================================================================
    
    /**
     * Create command line string from arguments
     */
    std::string build_command_line(const std::vector<std::string>& args) {
        std::string cmdline;
        for (size_t i = 0; i < args.size(); i++) {
            if (i > 0) cmdline += " ";
            
            // Quote arguments with spaces
            if (args[i].find(' ') != std::string::npos) {
                cmdline += "\"" + args[i] + "\"";
            } else {
                cmdline += args[i];
            }
        }
        return cmdline;
    }
    
    /**
     * Execute a single command using CreateProcess
     */
    void execute_command(const Command& cmd) {
        if (cmd.args.empty()) return;
        
        STARTUPINFOA si;
        PROCESS_INFORMATION pi;
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        ZeroMemory(&pi, sizeof(pi));
        
        // Setup I/O redirection
        HANDLE hInputFile = INVALID_HANDLE_VALUE;
        HANDLE hOutputFile = INVALID_HANDLE_VALUE;
        
        if (!cmd.input_file.empty()) {
            hInputFile = CreateFileA(
                cmd.input_file.c_str(),
                GENERIC_READ,
                FILE_SHARE_READ,
                NULL,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                NULL
            );
            
            if (hInputFile == INVALID_HANDLE_VALUE) {
                std::cerr << "Error: Cannot open input file " << cmd.input_file << "\n";
                return;
            }
            
            si.hStdInput = hInputFile;
            si.dwFlags |= STARTF_USESTDHANDLES;
        }
        
        if (!cmd.output_file.empty()) {
            DWORD creation = cmd.append_output ? OPEN_ALWAYS : CREATE_ALWAYS;
            
            hOutputFile = CreateFileA(
                cmd.output_file.c_str(),
                GENERIC_WRITE,
                FILE_SHARE_WRITE,
                NULL,
                creation,
                FILE_ATTRIBUTE_NORMAL,
                NULL
            );
            
            if (hOutputFile == INVALID_HANDLE_VALUE) {
                std::cerr << "Error: Cannot open output file " << cmd.output_file << "\n";
                if (hInputFile != INVALID_HANDLE_VALUE) CloseHandle(hInputFile);
                return;
            }
            
            if (cmd.append_output) {
                SetFilePointer(hOutputFile, 0, NULL, FILE_END);
            }
            
            si.hStdOutput = hOutputFile;
            si.hStdError = hOutputFile;
            si.dwFlags |= STARTF_USESTDHANDLES;
        }
        
        // Build command line
        std::string cmdline = build_command_line(cmd.args);
        
        // Create process
        BOOL success = CreateProcessA(
            NULL,                           // Application name
            (LPSTR)cmdline.c_str(),        // Command line
            NULL,                           // Process security attributes
            NULL,                           // Thread security attributes
            TRUE,                           // Inherit handles
            0,                              // Creation flags
            NULL,                           // Environment
            NULL,                           // Current directory
            &si,                            // Startup info
            &pi                             // Process information
        );
        
        // Clean up file handles
        if (hInputFile != INVALID_HANDLE_VALUE) CloseHandle(hInputFile);
        if (hOutputFile != INVALID_HANDLE_VALUE) CloseHandle(hOutputFile);
        
        if (!success) {
            std::cerr << "Error: Cannot execute command: " << cmd.args[0] << "\n";
            std::cerr << "Error code: " << GetLastError() << "\n";
            return;
        }
        
        if (cmd.background) {
            // Add to background jobs
            std::string cmd_string = build_command_line(cmd.args);
            add_job(pi.hProcess, pi.dwProcessId, cmd_string);
            std::cout << "[" << job_counter - 1 << "] " << pi.dwProcessId << std::endl;
            CloseHandle(pi.hThread);
        } else {
            // Wait for process to complete
            WaitForSingleObject(pi.hProcess, INFINITE);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
    }
    
    /**
     * Execute a pipeline of commands
     */
    void execute_pipeline(const std::vector<Command>& commands) {
        if (commands.empty()) return;
        
        // Handle single command
        if (commands.size() == 1) {
            if (!handle_builtin(commands[0])) {
                execute_command(commands[0]);
            }
            return;
        }
        
        // ====================================================================
        // PIPELINE EXECUTION
        // ====================================================================
        
        int num_commands = commands.size();
        std::vector<HANDLE> hProcesses;
        std::vector<HANDLE> hReadPipes;
        std::vector<HANDLE> hWritePipes;
        
        // Create pipes for pipeline
        for (int i = 0; i < num_commands - 1; i++) {
            HANDLE hReadPipe, hWritePipe;
            SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};
            
            if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
                std::cerr << "Error: Cannot create pipe\n";
                return;
            }
            
            hReadPipes.push_back(hReadPipe);
            hWritePipes.push_back(hWritePipe);
        }
        
        // Create processes for each command
        for (int i = 0; i < num_commands; i++) {
            STARTUPINFOA si;
            PROCESS_INFORMATION pi;
            ZeroMemory(&si, sizeof(si));
            si.cb = sizeof(si);
            si.dwFlags = STARTF_USESTDHANDLES;
            ZeroMemory(&pi, sizeof(pi));
            
            // Setup stdin
            if (i == 0) {
                // First command: use input file or stdin
                if (!commands[i].input_file.empty()) {
                    si.hStdInput = CreateFileA(
                        commands[i].input_file.c_str(),
                        GENERIC_READ, FILE_SHARE_READ, NULL,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL
                    );
                } else {
                    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
                }
            } else {
                // Use read end of previous pipe
                si.hStdInput = hReadPipes[i - 1];
            }
            
            // Setup stdout
            if (i == num_commands - 1) {
                // Last command: use output file or stdout
                if (!commands[i].output_file.empty()) {
                    DWORD creation = commands[i].append_output ? OPEN_ALWAYS : CREATE_ALWAYS;
                    si.hStdOutput = CreateFileA(
                        commands[i].output_file.c_str(),
                        GENERIC_WRITE, FILE_SHARE_WRITE, NULL,
                        creation, FILE_ATTRIBUTE_NORMAL, NULL
                    );
                    if (commands[i].append_output && si.hStdOutput != INVALID_HANDLE_VALUE) {
                        SetFilePointer(si.hStdOutput, 0, NULL, FILE_END);
                    }
                } else {
                    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
                }
            } else {
                // Use write end of current pipe
                si.hStdOutput = hWritePipes[i];
            }
            
            si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
            
            // Build command line
            std::string cmdline = build_command_line(commands[i].args);
            
            // Create process
            BOOL success = CreateProcessA(
                NULL, (LPSTR)cmdline.c_str(),
                NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi
            );
            
            if (!success) {
                std::cerr << "Error: Cannot execute " << commands[i].args[0] << "\n";
                continue;
            }
            
            hProcesses.push_back(pi.hProcess);
            CloseHandle(pi.hThread);
        }
        
        // Close all pipe handles in parent
        for (auto h : hReadPipes) CloseHandle(h);
        for (auto h : hWritePipes) CloseHandle(h);
        
        bool background = commands.back().background;
        
        if (background) {
            // Add first process to jobs
            if (!hProcesses.empty()) {
                DWORD pid;
                GetProcessId(hProcesses[0]);
                std::string cmd_string;
                for (const auto& cmd : commands) {
                    cmd_string += build_command_line(cmd.args);
                    if (&cmd != &commands.back()) cmd_string += " | ";
                }
                add_job(hProcesses[0], GetProcessId(hProcesses[0]), cmd_string);
                std::cout << "[" << job_counter - 1 << "] " << GetProcessId(hProcesses[0]) << std::endl;
            }
            
            // Close other handles
            for (size_t i = 1; i < hProcesses.size(); i++) {
                CloseHandle(hProcesses[i]);
            }
        } else {
            // Wait for all processes
            if (!hProcesses.empty()) {
                WaitForMultipleObjects(hProcesses.size(), hProcesses.data(), TRUE, INFINITE);
            }
            
            // Close all process handles
            for (auto h : hProcesses) CloseHandle(h);
        }
    }
    
    // ========================================================================
    // JOB MANAGEMENT
    // ========================================================================
    
    void add_job(HANDLE hProcess, DWORD processId, const std::string& command) {
        jobs[job_counter++] = Job(hProcess, processId, command);
    }
    
    void remove_job(int job_id) {
        jobs.erase(job_id);
    }
    
    void check_background_jobs() {
        std::vector<int> completed_jobs;
        
        for (auto& job_pair : jobs) {
            if (job_pair.second.hProcess) {
                DWORD exitCode;
                if (GetExitCodeProcess(job_pair.second.hProcess, &exitCode)) {
                    if (exitCode != STILL_ACTIVE) {
                        std::cout << "[" << job_pair.first << "] Done    " 
                                  << job_pair.second.command << std::endl;
                        CloseHandle(job_pair.second.hProcess);
                        completed_jobs.push_back(job_pair.first);
                    }
                }
            }
        }
        
        for (int job_id : completed_jobs) {
            remove_job(job_id);
        }
    }
    
    // ========================================================================
    // USER INTERFACE
    // ========================================================================
    
    void display_prompt() {
        char cwd[MAX_PATH];
        if (getcwd(cwd, sizeof(cwd)) != nullptr) {
            std::cout << "shell:" << cwd << "> ";
        } else {
            std::cout << "shell> ";
        }
        std::cout.flush();
    }
    
public:
    WindowsShell() : job_counter(1), running(true) {
        std::cout << "\n";
        std::cout << "=============================================================\n";
        std::cout << "  Windows Mini Shell \n";
        std::cout << "=============================================================\n";
        std::cout << "Native Windows shell using Windows API\n";
        std::cout << "Type 'help' for available commands\n";
        std::cout << "Type 'exit' to quit\n\n";
        std::cout << "Features:\n";
        std::cout << "Command execution (CreateProcess)\n";
        std::cout << "  Pipelines (|)\n";
        std::cout << "  I/O Redirection (<, >, >>)\n";
        std::cout << "   Background jobs (&)\n";
        std::cout << "   Job control (jobs, fg, kill)\n";
        std::cout << "=============================================================\n\n";
    }
    
    ~WindowsShell() {
        // Clean up all job handles
        for (auto& job_pair : jobs) {
            if (job_pair.second.hProcess) {
                CloseHandle(job_pair.second.hProcess);
            }
        }
    }
    
    void run() {
        std::string input;
        
        while (running) {
            check_background_jobs();
            display_prompt();
            
            if (!std::getline(std::cin, input)) {
                std::cout << "\nexit\n";
                break;
            }
            
            if (input.empty() || input.find_first_not_of(" \t\n") == std::string::npos) {
                continue;
            }
            
            std::vector<Command> commands = parse_input(input);
            execute_pipeline(commands);
        }
    }
};

// ============================================================================
// MAIN FUNCTION
// ============================================================================

int main() {
    WindowsShell shell;
    shell.run();
    return 0;
}

/*
 * ============================================================================
 * WINDOWS API REFERENCE
 * ============================================================================
 * 
 * Process Management:
 * - CreateProcess()        : Create new process (fork + exec equivalent)
 * - WaitForSingleObject()  : Wait for process completion (waitpid equivalent)
 * - TerminateProcess()     : Kill process
 * - GetExitCodeProcess()   : Get process exit code
 * - GetProcessId()         : Get process ID
 * 
 * Inter-Process Communication:
 * - CreatePipe()          : Create anonymous pipe
 * - ReadFile()            : Read from pipe
 * - WriteFile()           : Write to pipe
 * 
 * File Operations:
 * - CreateFile()          : Open/create file
 * - CloseHandle()         : Close handle
 * - SetFilePointer()      : Seek in file
 * 
 * Handle Management:
 * - DuplicateHandle()     : Duplicate handle
 * - SetHandleInformation(): Set handle properties
 * 
 * ============================================================================
 * COMPILATION
 * ============================================================================
 * 
 * MinGW (GCC on Windows):
 *   g++ -std=c++17 -o shell.exe minishell_windows_full.cpp
 * 
 * Microsoft Visual C++:
 *   cl /EHsc /std:c++17 /Fe:shell.exe minishell_windows_full.cpp
 * 
 * Clang on Windows:
 *   clang++ -std=c++17 -o shell.exe minishell_windows_full.cpp
 * 
 * ============================================================================
 * USAGE EXAMPLES
 * ============================================================================
 * 
 * Basic Commands:
 *   shell> dir
 *   shell> type file.txt
 *   shell> echo Hello World
 * 
 * Pipes:
 *   shell> dir | findstr .cpp
 *   shell> type file.txt | sort
 * 
 * Redirection:
 *   shell> dir > files.txt
 *   shell> type < input.txt
 *   shell> echo Hello >> log.txt
 * 
 * Background:
 *   shell> ping google.com -n 100 &
 *   [1] 12345
 *   shell> jobs
 *   [1] PID:12345 ping google.com -n 100
 *   shell> kill 1
 * 
 * ============================================================================
 */
